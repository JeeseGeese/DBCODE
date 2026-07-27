#include "AudioAnalyzer.h"
#include <driver/i2s.h>
#include <math.h>

static bool micReady = false;
static AudioFeatures features = {};

// --- Per-window accumulators (reset every MIC_PRINT_INTERVAL_MS) ---
static uint32_t bytesThisWindow = 0;
static int32_t rawMin = 0;
static int32_t rawMax = 0;
static int32_t peakCorrected = 0;
static int64_t sumSquaresCorrected = 0;
static int64_t bassSumSquares = 0;
static uint32_t sampleCount = 0;
static unsigned long lastWindowTime = 0;

// --- Streaming filter state (persists across windows) ---
static float dcEstimate = 0.0f;
static float bassLpState = 0.0f;

// --- Adaptive noise floor ---
static float noiseFloorEstimate = AUDIO_NOISE_FLOOR;

// --- Transient / clap edge + cooldown state ---
static float prevEnvelope = 0.0f;
static unsigned long lastClapEventMs = 0;
static bool clapArmed = true; // re-arms once rms drops back below threshold
static unsigned long lastTransientEventMs = 0;

// --- Fault detection (unchanged behavior from the original diagnostic) ---
static unsigned long zeroByteStreak = 0;
static bool zeroByteWarned = false;
static unsigned long lastNonZeroSampleTime = 0;
static bool zeroSampleWarned = false;
static unsigned long lastNonConstantTime = 0;
static bool stuckWarned = false;
static unsigned long lastNonSaturatedTime = 0;
static bool saturatedWarned = false;

static unsigned long lastHeartbeatPrint = 0;

// See setAudioLogEnabled() in AudioAnalyzer.h -- gates only the continuous
// heartbeat/fault-warning output below, never sampling/computation itself.
// Defaults to false (quiet) per the serial-readability request.
static bool audioLogEnabled = false;

void setAudioLogEnabled(bool enabled) {
  audioLogEnabled = enabled;
  Serial.println(enabled ? F("[AUDIO LOG] ON") : F("[AUDIO LOG] OFF"));
}
bool isAudioLogEnabled() { return audioLogEnabled; }

static void resetWindow(unsigned long now) {
  bytesThisWindow = 0;
  rawMin = INT32_MAX;
  rawMax = INT32_MIN;
  peakCorrected = 0;
  sumSquaresCorrected = 0;
  bassSumSquares = 0;
  sampleCount = 0;
  lastWindowTime = now;
}

bool initAudioAnalyzer() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      // INMP441 outputs on the LEFT I2S slot when its L/R pin is tied to
      // GND. Hardware-verified.
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[MIC] ERROR: I2S driver install failed (err=%d)\n", (int)err);
    return false;
  }

  i2s_pin_config_t pin_config = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = I2S_BCLK_PIN,
      .ws_io_num = I2S_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_DIN_PIN,
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[MIC] ERROR: I2S set pin failed (err=%d)\n", (int)err);
    return false;
  }

  Serial.println(F("[MIC] I2S initialization: SUCCESS"));
  Serial.printf("[MIC] Pins: BCLK=%d WS=%d DIN=%d\n", I2S_BCLK_PIN, I2S_WS_PIN, I2S_DIN_PIN);
  Serial.printf("[MIC] Sample rate: %d Hz\n", I2S_SAMPLE_RATE);
  Serial.println(F("[MIC] Bit depth: 32-bit I2S word (24-bit sample left-justified)"));
  Serial.println(F("[MIC] Selected channel: LEFT"));

  micReady = true;
  unsigned long now = millis();
  resetWindow(now);
  zeroByteStreak = 0;
  zeroByteWarned = false;
  lastNonZeroSampleTime = now;
  zeroSampleWarned = false;
  lastNonConstantTime = now;
  stuckWarned = false;
  lastNonSaturatedTime = now;
  saturatedWarned = false;
  dcEstimate = 0.0f;
  bassLpState = 0.0f;
  noiseFloorEstimate = AUDIO_NOISE_FLOOR;
  return true;
}

bool isMicReady() { return micReady; }
const AudioFeatures &getAudioFeatures() { return features; }
float getNoiseFloorEstimate() { return noiseFloorEstimate; }

static void captureSamples() {
  int32_t sampleBuf[128];
  size_t bytesRead = 0;
  // Short but non-zero timeout: a pure 0-tick poll can race the DMA's
  // buffer-ready signal. 20ms stays short enough to keep buttons/frames
  // responsive.
  esp_err_t err = i2s_read(I2S_PORT, sampleBuf, sizeof(sampleBuf), &bytesRead, pdMS_TO_TICKS(20));

  if (err != ESP_OK || bytesRead == 0) {
    zeroByteStreak++;
    if (zeroByteStreak > MIC_ZERO_BYTE_WARN_THRESHOLD && !zeroByteWarned) {
      if (audioLogEnabled) Serial.println(F("[MIC] WARN: I2S reads returning 0 bytes repeatedly - check wiring/init"));
      zeroByteWarned = true;
    }
    return;
  }

  zeroByteStreak = 0;
  zeroByteWarned = false;
  bytesThisWindow += bytesRead;

  int samples = bytesRead / sizeof(int32_t);
  bool windowHasNonZero = false;
  bool windowHasNonSaturated = false;

  for (int i = 0; i < samples; i++) {
    // INMP441 left-justifies a 24-bit sample in the 32-bit I2S word.
    int32_t s = sampleBuf[i] >> 8;

    if (s < rawMin) rawMin = s;
    if (s > rawMax) rawMax = s;

    dcEstimate += ((float)s - dcEstimate) * 0.01f;
    float corrected = (float)s - dcEstimate;
    int32_t correctedMag = (int32_t)fabsf(corrected);

    if (correctedMag > peakCorrected) peakCorrected = correctedMag;
    sumSquaresCorrected += (int64_t)(corrected * corrected);

    // Low-frequency proxy: single-pole low-pass on the same corrected
    // stream (see AUDIO_BASS_LP_ALPHA comment in Config.h).
    bassLpState += (corrected - bassLpState) * AUDIO_BASS_LP_ALPHA;
    bassSumSquares += (int64_t)(bassLpState * bassLpState);

    sampleCount++;

    int32_t mag = (s < 0) ? -s : s;
    if (mag != 0) windowHasNonZero = true;
    if (mag < MIC_SATURATION_THRESHOLD) windowHasNonSaturated = true;
  }

  if (windowHasNonZero) {
    lastNonZeroSampleTime = millis();
    zeroSampleWarned = false;
  }
  if (windowHasNonSaturated) {
    lastNonSaturatedTime = millis();
    saturatedWarned = false;
  }
}

static void runFaultChecks(unsigned long now) {
  bool windowConstant = (sampleCount > 0) && (rawMin == rawMax);
  if (!windowConstant) lastNonConstantTime = now;

  if (now - lastNonZeroSampleTime > MIC_STUCK_WARN_MS && !zeroSampleWarned) {
    if (audioLogEnabled) {
      Serial.println(F("[MIC] WARN: samples have been exactly zero for several seconds"));
      Serial.println(F("[MIC] HINT: try the other I2S channel slot (LEFT vs RIGHT)"));
    }
    zeroSampleWarned = true;
  }
  if (now - lastNonConstantTime > MIC_STUCK_WARN_MS && !stuckWarned) {
    if (audioLogEnabled) Serial.println(F("[MIC] WARN: raw samples appear constant/stuck for several seconds"));
    stuckWarned = true;
  }
  if (now - lastNonSaturatedTime > MIC_STUCK_WARN_MS && !saturatedWarned) {
    if (audioLogEnabled) Serial.println(F("[MIC] WARN: samples appear saturated (clipping) for several seconds"));
    saturatedWarned = true;
  }
}

static void computeFeatures(unsigned long now, float dtSeconds) {
  if (sampleCount == 0) {
    features.rms = 0.0f;
  } else {
    double meanSquare = (double)sumSquaresCorrected / (double)sampleCount;
    features.rms = (float)sqrt(meanSquare);
    // BEGIN HARDWARE TEST SUPPORT -- see AudioFeatures::peak in AudioAnalyzer.h
    features.peak = (float)peakCorrected;
    // END HARDWARE TEST SUPPORT

    double bassMeanSquare = (double)bassSumSquares / (double)sampleCount;
    float bassRms = (float)sqrt(bassMeanSquare);
    float bassNorm = (bassRms - AUDIO_BASS_NOISE_FLOOR) / (AUDIO_BASS_MAX_RMS - AUDIO_BASS_NOISE_FLOOR);
    features.lowFrequencyEnergy = constrain(bassNorm, 0.0f, 1.0f);
  }

  // Slow ambient adaptation: only drift the floor while things are
  // already quiet, and only within a bounded range, so a loud sustained
  // passage of music can't become "the new silence".
  if (features.rms < noiseFloorEstimate * AUDIO_NOISE_FLOOR_ADAPT_MARGIN) {
    noiseFloorEstimate += (features.rms - noiseFloorEstimate) * AUDIO_NOISE_FLOOR_ADAPT_RATE;
    noiseFloorEstimate = constrain(noiseFloorEstimate, AUDIO_NOISE_FLOOR_MIN, AUDIO_NOISE_FLOOR_MAX);
  }

  float norm = (features.rms - noiseFloorEstimate) / (AUDIO_MAX_RMS - noiseFloorEstimate);
  features.normalized = constrain(norm, 0.0f, 1.0f);

  float rate = (features.normalized > features.envelope) ? AUDIO_ATTACK_SMOOTHING : AUDIO_RELEASE_SMOOTHING;
  features.envelope += (features.normalized - features.envelope) * rate;

  float riseRate = (dtSeconds > 0.0f) ? (features.envelope - prevEnvelope) / dtSeconds : 0.0f;
  features.transientStrength = max(0.0f, riseRate);
  prevEnvelope = features.envelope;

  features.transient = false;
  if (features.transientStrength > AUDIO_TRANSIENT_RISE_THRESHOLD &&
      now - lastTransientEventMs > AUDIO_CLAP_COOLDOWN_MS) {
    features.transient = true;
    lastTransientEventMs = now;
  }

  // Clap: edge-triggered (crossing up through the threshold) plus a
  // cooldown, so one sustained loud passage fires a single event instead
  // of re-triggering every analysis window.
  features.clap = false;
  bool aboveThreshold = features.rms > AUDIO_CLAP_THRESHOLD;
  if (aboveThreshold && clapArmed && now - lastClapEventMs > AUDIO_CLAP_COOLDOWN_MS) {
    features.clap = true;
    lastClapEventMs = now;
    clapArmed = false;
  } else if (!aboveThreshold) {
    clapArmed = true;
  }
}

void updateAudioAnalyzer() {
  if (!micReady) return;

  captureSamples();

  unsigned long now = millis();
  if (now - lastWindowTime < MIC_PRINT_INTERVAL_MS) return;

  float dtSeconds = (now - lastWindowTime) / 1000.0f;
  runFaultChecks(now);
  computeFeatures(now, dtSeconds);
  resetWindow(now);

  if (now - lastHeartbeatPrint >= AUDIO_DIAG_PRINT_INTERVAL_MS) {
    // Timer keeps ticking regardless of the log toggle, so re-enabling
    // logging later doesn't immediately fire a burst from a stale timestamp.
    lastHeartbeatPrint = now;
    if (audioLogEnabled) {
      Serial.printf("[AUDIO] rms=%.0f norm=%.2f env=%.2f floor=%.0f bass=%.2f clap=%d transient=%d\n",
                    features.rms, features.normalized, features.envelope, noiseFloorEstimate,
                    features.lowFrequencyEnergy, features.clap ? 1 : 0, features.transient ? 1 : 0);
    }
  }
}

void printAudioDiagnostics() {
  Serial.println(F("[AUDIO DIAG] --- current features ---"));
  Serial.printf("  rms=%.0f normalized=%.3f envelope=%.3f\n", features.rms, features.normalized, features.envelope);
  Serial.printf("  transientStrength=%.3f transient=%d clap=%d\n", features.transientStrength,
                features.transient ? 1 : 0, features.clap ? 1 : 0);
  Serial.printf("  lowFrequencyEnergy=%.3f (proxy, not true FFT bass)\n", features.lowFrequencyEnergy);
  Serial.printf("  noiseFloorEstimate=%.0f (bounds [%.0f, %.0f])\n", noiseFloorEstimate,
                (float)AUDIO_NOISE_FLOOR_MIN, (float)AUDIO_NOISE_FLOOR_MAX);
  Serial.println(F("[AUDIO DIAG] --- tuning constants ---"));
  Serial.printf("  AUDIO_MAX_RMS=%.0f AUDIO_CLAP_THRESHOLD=%.0f\n", (float)AUDIO_MAX_RMS, (float)AUDIO_CLAP_THRESHOLD);
  Serial.printf("  ATTACK=%.2f RELEASE=%.2f CLAP_DECAY=%.2f\n", (float)AUDIO_ATTACK_SMOOTHING,
                (float)AUDIO_RELEASE_SMOOTHING, (float)AUDIO_CLAP_DECAY);
  Serial.printf("  TRANSIENT_RISE_THRESHOLD=%.2f/s CLAP_COOLDOWN=%dms LIGHTNING_COOLDOWN=%dms\n",
                (float)AUDIO_TRANSIENT_RISE_THRESHOLD, (int)AUDIO_CLAP_COOLDOWN_MS, (int)AUDIO_LIGHTNING_COOLDOWN_MS);
}
