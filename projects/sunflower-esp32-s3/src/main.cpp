#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <driver/i2s.h>

#define LED_PIN 4
#define BUTTON_MODE_PIN 10
#define BUTTON_MUTE_PIN 11
#define BUTTON_BRIGHTNESS_PIN 17
#define BUTTON4_PIN 5
#define NUM_LEDS 58
#define NUM_MODES 13
#define MODE_AUDIO_PULSE 12 // last mode, appended so existing mode numbers 0-11 are unchanged
#define DEBOUNCE_DELAY_MS 40
#define BUTTON4_LONG_PRESS_MS 600 // hold this long to toggle AUDIO_PULSE

// --- INMP441 I2S microphone config ---
#define I2S_PORT I2S_NUM_0
#define I2S_BCLK_PIN 6
#define I2S_WS_PIN 7
// GPIO15 is the mic's SD/DATA pin as physically wired and hardware-verified
// (quiet/speech/clap response confirmed) -- not GPIO8, which was the
// originally planned pin before the board's actual wiring was traced.
#define I2S_DIN_PIN 15
#define I2S_SAMPLE_RATE 16000
#define MIC_PRINT_INTERVAL_MS 120        // ~8Hz, within the requested 5-10Hz
#define MIC_ZERO_BYTE_WARN_THRESHOLD 50  // consecutive empty i2s_read() calls
#define MIC_STUCK_WARN_MS 3000           // how long "stuck" must persist before warning
#define MIC_SATURATION_THRESHOLD 8300000 // near max of the 24-bit signed sample range

// --- AUDIO_PULSE tuning ---
// Values chosen from hardware-observed RMS ranges on this exact mic/board:
// quiet ~8k-20k, taps ~19k-23k, speech ~40k-360k+, claps ~100k-900k+ (often
// 500k+ for a sharp clap specifically).
#define AUDIO_NOISE_FLOOR 20000.0f   // top of the observed quiet-room range; below this, level is 0
#define AUDIO_MAX_RMS 200000.0f      // normalization ceiling; loud speech/music reaches ~1.0 here
#define AUDIO_CLAP_THRESHOLD 400000.0f // above loud speech (~360k), below typical clap RMS (~500k+)
#define AUDIO_ATTACK_SMOOTHING 0.6f  // fast rise: speech/claps register almost immediately
#define AUDIO_RELEASE_SMOOTHING 0.08f // slow fall: avoids visible flicker between samples
#define AUDIO_CLAP_DECAY 0.85f       // per-frame decay of the clap flash (~150-200ms to fade)
#define AUDIO_IDLE_BRIGHTNESS 0.15f  // fraction of full color used for the silent idle glow
#define AUDIO_EDGE_SOFTNESS 0.15f    // normalized-distance falloff width of the expanding pulse edge

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

const char *MODE_NAMES[NUM_MODES] = {
    "Off",
    "Solid Red",
    "Solid Green",
    "Solid Blue",
    "Solid White (dim)",
    "Forward Walking Pixel",
    "Reverse Walking Pixel",
    "Rainbow",
    "Theater Chase",
    "Breathing",
    "Twinkle",
    "Larson Scanner",
    "AUDIO_PULSE",
};

uint8_t currentMode = 0;
bool modeChanged = true;
bool muted = false;

// Conservative brightness levels only -- never full brightness, never
// exceeds the previously-established 15-20 safe range at the top end.
const uint8_t BRIGHTNESS_LEVELS[] = {10, 15, 20};
const uint8_t NUM_BRIGHTNESS_LEVELS = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);
uint8_t brightnessIndex = 1; // starts at 15, matching the previous fixed default

// --- Button debounce state ---
// Edge-triggered on a debounce-stable HIGH->LOW transition only, so a press
// fires exactly once no matter how long the button is held, and the pin
// must return to a stable HIGH (release) before another press can register.
// Each button gets its own independent instance of this state, so four
// buttons can be read every loop iteration without interfering with each
// other's debounce timing.
struct DebouncedButton {
  uint8_t pin;
  int lastRawReading = HIGH;
  int stableState = HIGH;
  unsigned long lastDebounceTime = 0;
  explicit DebouncedButton(uint8_t p) : pin(p) {}
};

DebouncedButton modeButton{BUTTON_MODE_PIN};
DebouncedButton muteButton{BUTTON_MUTE_PIN};
DebouncedButton brightnessButton{BUTTON_BRIGHTNESS_PIN};
DebouncedButton button4{BUTTON4_PIN};

// Button 4 long-press tracking: short press cycles audio-reactive modes
// (currently just AUDIO_PULSE -- a no-op until more exist); long press
// (>= BUTTON4_LONG_PRESS_MS) toggles AUDIO_PULSE on/off directly.
unsigned long button4PressStartTime = 0;
bool button4LongPressFired = false;
uint8_t modeBeforeAudioPulse = 0; // restored when long-press exits AUDIO_PULSE

// Returns true exactly once per debounced press (stable HIGH->LOW edge).
bool buttonPressedEdge(DebouncedButton &b) {
  int reading = digitalRead(b.pin);

  if (reading != b.lastRawReading) {
    b.lastDebounceTime = millis();
  }

  bool pressed = false;
  if ((millis() - b.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) {
        pressed = true;
      }
    }
  }

  b.lastRawReading = reading;
  return pressed;
}

// --- INMP441 microphone diagnostic state ---
bool micReady = false;
// Button 4 no longer toggles this (it now controls AUDIO_PULSE) -- default
// to on so the [MIC] diagnostic output isn't lost, just no longer gated by
// a button. toggleMicDiagnostic() is kept available for future rewiring.
bool micDiagnosticEnabled = true;
const char *MIC_CHANNEL_NAME = "LEFT"; // kept in sync with channel_format below

// Latest completed window's DC-corrected RMS, in raw sample units. Updated
// every ~MIC_PRINT_INTERVAL_MS by updateMicDiagnostic() regardless of
// whether diagnostic printing is enabled -- AUDIO_PULSE (and any future
// audio-reactive mode) reads this directly, reusing the one existing I2S
// capture path instead of a second driver/init.
float micLatestRms = 0.0f;

// Per-window accumulators (reset every MIC_PRINT_INTERVAL_MS)
uint32_t micBytesThisWindow = 0;
int32_t micRawMin = 0;
int32_t micRawMax = 0;
int32_t micPeakCorrected = 0;
int64_t micSumSquaresCorrected = 0;
uint32_t micSampleCount = 0;
unsigned long micLastPrintTime = 0;

// Streaming DC-blocker: slow-moving estimate of the signal's DC offset,
// subtracted from each sample before peak/RMS accumulation so a constant
// bias can't mask (or fake) real AC audio content.
float micDcEstimate = 0.0f;

unsigned long micZeroByteStreak = 0;
bool micZeroByteWarned = false;

unsigned long micLastNonZeroSampleTime = 0;
bool micZeroSampleWarned = false;

unsigned long micLastNonConstantTime = 0;
bool micStuckWarned = false;

unsigned long micLastNonSaturatedTime = 0;
bool micSaturatedWarned = false;

bool initMic() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      // INMP441 outputs on the LEFT I2S slot when its L/R pin is tied to
      // GND (it would output on RIGHT if L/R were tied to VDD instead).
      // Hardware-verified: LEFT produces correct quiet/speech/clap response.
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
  Serial.printf("[MIC] Selected channel: %s\n", MIC_CHANNEL_NAME);

  return true;
}

// Called every print cycle (~120ms): resets only the per-window
// accumulators. Does NOT touch the multi-second "stuck" tracking
// timestamps below -- those must persist across many windows to detect a
// condition that holds for MIC_STUCK_WARN_MS.
void resetMicWindow(unsigned long now) {
  micBytesThisWindow = 0;
  micRawMin = INT32_MAX;
  micRawMax = INT32_MIN;
  micPeakCorrected = 0;
  micSumSquaresCorrected = 0;
  micSampleCount = 0;
  micLastPrintTime = now;
}

// Called once per diagnostic session (on enable): resets everything,
// including the longer-horizon "stuck" detection state and the DC
// tracker, so a fresh session doesn't inherit stale state.
void resetMicSession(unsigned long now) {
  resetMicWindow(now);
  micZeroByteStreak = 0;
  micZeroByteWarned = false;
  micLastNonZeroSampleTime = now;
  micZeroSampleWarned = false;
  micLastNonConstantTime = now;
  micStuckWarned = false;
  micLastNonSaturatedTime = now;
  micSaturatedWarned = false;
  micDcEstimate = 0.0f;
}

void toggleMicDiagnostic() {
  micDiagnosticEnabled = !micDiagnosticEnabled;

  if (micDiagnosticEnabled && !micReady) {
    Serial.println(F("[MIC] ERROR: I2S not initialized, cannot start diagnostic"));
    micDiagnosticEnabled = false;
    return;
  }

  if (micDiagnosticEnabled) {
    Serial.println(F("[MIC] Diagnostic enabled"));
    resetMicSession(millis());
  } else {
    Serial.println(F("[MIC] Diagnostic disabled"));
  }
}

// Always runs the I2S capture/accumulation (so micLatestRms stays fresh for
// audio-reactive modes even when diagnostic printing is off); only the
// Serial output below is gated by micDiagnosticEnabled, preserving Button
// 4's existing observable toggle behavior exactly.
void updateMicDiagnostic() {
  if (!micReady) return;

  int32_t sampleBuf[128];
  size_t bytesRead = 0;
  // A short but non-zero timeout: a pure 0-tick poll can race the DMA's
  // buffer-ready signal and falsely report 0 bytes even when the
  // peripheral is working correctly. This stays short enough to keep the
  // button/animation loop responsive.
  esp_err_t err = i2s_read(I2S_PORT, sampleBuf, sizeof(sampleBuf), &bytesRead, pdMS_TO_TICKS(20));

  if (err == ESP_OK && bytesRead > 0) {
    micZeroByteStreak = 0;
    micZeroByteWarned = false;
    micBytesThisWindow += bytesRead;

    int samples = bytesRead / sizeof(int32_t);
    bool windowHasNonZero = false;
    bool windowHasNonSaturated = false;

    for (int i = 0; i < samples; i++) {
      // INMP441 left-justifies a 24-bit sample in the 32-bit I2S word;
      // shift out the zero-padded low byte to recover the true value.
      int32_t s = sampleBuf[i] >> 8;

      if (s < micRawMin) micRawMin = s;
      if (s > micRawMax) micRawMax = s;

      // Slow exponential DC tracker, then use the corrected (AC-only)
      // value for peak/RMS so a constant offset can't hide real audio.
      micDcEstimate += ((float)s - micDcEstimate) * 0.01f;
      float corrected = (float)s - micDcEstimate;
      int32_t correctedMag = (int32_t)fabsf(corrected);

      if (correctedMag > micPeakCorrected) micPeakCorrected = correctedMag;
      micSumSquaresCorrected += (int64_t)(corrected * corrected);
      micSampleCount++;

      int32_t mag = (s < 0) ? -s : s;
      if (mag != 0) windowHasNonZero = true;
      if (mag < MIC_SATURATION_THRESHOLD) windowHasNonSaturated = true;
    }

    if (windowHasNonZero) {
      micLastNonZeroSampleTime = millis();
      micZeroSampleWarned = false;
    }
    if (windowHasNonSaturated) {
      micLastNonSaturatedTime = millis();
      micSaturatedWarned = false;
    }
  } else {
    micZeroByteStreak++;
    if (micDiagnosticEnabled && micZeroByteStreak > MIC_ZERO_BYTE_WARN_THRESHOLD && !micZeroByteWarned) {
      Serial.println(F("[MIC] WARN: I2S reads returning 0 bytes repeatedly - check wiring/init"));
      micZeroByteWarned = true;
    }
  }

  unsigned long now = millis();
  if (now - micLastPrintTime >= MIC_PRINT_INTERVAL_MS) {
    bool windowConstant = (micSampleCount > 0) && (micRawMin == micRawMax);
    if (!windowConstant) micLastNonConstantTime = now;

    if (micSampleCount > 0) {
      double meanSquare = (double)micSumSquaresCorrected / (double)micSampleCount;
      long rms = (long)sqrt(meanSquare);
      micLatestRms = (float)rms; // always updated, regardless of diagnostic print state
      if (micDiagnosticEnabled) {
        Serial.printf("[MIC] bytes=%lu rawMin=%ld rawMax=%ld peak=%ld rms=%ld\n",
                      (unsigned long)micBytesThisWindow, (long)micRawMin, (long)micRawMax,
                      (long)micPeakCorrected, rms);
      }
    } else if (micDiagnosticEnabled) {
      Serial.printf("[MIC] bytes=%lu (no samples this window)\n", (unsigned long)micBytesThisWindow);
    }

    if (micDiagnosticEnabled && now - micLastNonZeroSampleTime > MIC_STUCK_WARN_MS && !micZeroSampleWarned) {
      Serial.println(F("[MIC] WARN: samples have been exactly zero for several seconds"));
      Serial.println(F("[MIC] HINT: clock/DMA is running but data is silent -- try the other"));
      Serial.println(F("[MIC]       I2S channel slot (LEFT vs RIGHT); this is the classic"));
      Serial.println(F("[MIC]       symptom of reading an inactive channel."));
      micZeroSampleWarned = true;
    }
    if (micDiagnosticEnabled && now - micLastNonConstantTime > MIC_STUCK_WARN_MS && !micStuckWarned) {
      Serial.println(F("[MIC] WARN: raw samples appear constant/stuck for several seconds"));
      micStuckWarned = true;
    }
    if (micDiagnosticEnabled && now - micLastNonSaturatedTime > MIC_STUCK_WARN_MS && !micSaturatedWarned) {
      Serial.println(F("[MIC] WARN: samples appear saturated (clipping) for several seconds"));
      micSaturatedWarned = true;
    }

    resetMicWindow(now);
  }
}

// --- Per-mode animation state (reset whenever the mode changes) ---
unsigned long lastStepTime = 0;
uint16_t walkPos = 0;
uint16_t rainbowHue = 0;
uint8_t chaseOffset = 0;
float breathPhase = 0.0f;
uint8_t twinkleLevel[NUM_LEDS];
uint8_t larsonLevel[NUM_LEDS];
int larsonPos = 0;
int larsonDir = 1;

// AUDIO_PULSE state
float audioNormalizedLevel = 0.0f; // instantaneous 0..1, before attack/release smoothing
float audioSmoothedLevel = 0.0f;   // attack/release-smoothed 0..1, drives the visual pulse radius
float audioClapFlash = 0.0f;       // 0..1, decays each frame; brief near-full-brightness overlay
unsigned long audioLastDiagPrint = 0;

// Forward declaration: defined near the other step functions below, but
// resetModeState() (just ahead) needs to call it for immediate mode entry.
void renderAudioIdleGlow();

void printModeAnnouncement() {
  Serial.printf("[MODE] %d - %s\n", currentMode, MODE_NAMES[currentMode]);
}

void advanceMode() {
  currentMode = (currentMode + 1) % NUM_MODES;
  modeChanged = true;
  printModeAnnouncement();
}

// Button 4 long press: jump into AUDIO_PULSE, remembering the mode to
// return to; pressing long again exits back to that remembered mode.
void toggleAudioPulseMode() {
  if (currentMode != MODE_AUDIO_PULSE) {
    modeBeforeAudioPulse = currentMode;
    currentMode = MODE_AUDIO_PULSE;
  } else {
    currentMode = modeBeforeAudioPulse;
  }
  modeChanged = true;
  printModeAnnouncement();
}

// Button 4 short press: cycles among audio-reactive modes. Only AUDIO_PULSE
// exists today, so this is a documented no-op placeholder for when more are
// added, and only does anything while an audio-reactive mode is active.
void audioPulseShortPress() {
  if (currentMode == MODE_AUDIO_PULSE) {
    Serial.println(F("[BUTTON] Button 4 short press (only one audio-reactive mode exists yet)"));
  }
}

// Distinct from buttonPressedEdge()'s single press-edge signal: this needs
// both press-start (to time the hold) and release (to fire a short press),
// so it manages button4's debounce fields directly rather than reusing the
// simpler helper used by the other three buttons.
void handleButton4() {
  int reading = digitalRead(button4.pin);

  if (reading != button4.lastRawReading) {
    button4.lastDebounceTime = millis();
  }

  if ((millis() - button4.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != button4.stableState) {
      button4.stableState = reading;
      if (button4.stableState == LOW) {
        button4PressStartTime = millis();
        button4LongPressFired = false;
      } else if (!button4LongPressFired) {
        audioPulseShortPress();
      }
    }
  }

  if (button4.stableState == LOW && !button4LongPressFired &&
      (millis() - button4PressStartTime) >= BUTTON4_LONG_PRESS_MS) {
    button4LongPressFired = true;
    toggleAudioPulseMode();
  }

  button4.lastRawReading = reading;
}

void toggleMute() {
  muted = !muted;
  Serial.println(F("[BUTTON] Mute pressed"));
  if (muted) {
    // Every other mode blanks to black on mute. AUDIO_PULSE instead holds
    // its normal idle glow and ignores audio while muted, per spec.
    if (currentMode == MODE_AUDIO_PULSE) {
      renderAudioIdleGlow();
    } else {
      strip.clear();
      strip.show();
    }
    Serial.println(F("[SYSTEM] Muted"));
  } else {
    Serial.println(F("[SYSTEM] Unmuted"));
    modeChanged = true; // force a clean re-render of the current mode
  }
}

void cycleBrightness() {
  brightnessIndex = (brightnessIndex + 1) % NUM_BRIGHTNESS_LEVELS;
  strip.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
  Serial.println(F("[BUTTON] Brightness pressed"));
  Serial.printf("[SYSTEM] Brightness: %d\n", BRIGHTNESS_LEVELS[brightnessIndex]);
  // Adafruit_NeoPixel::setBrightness() rescales the stored pixel buffer
  // in place, which can drift on repeated calls -- force a full redraw
  // from canonical mode colors instead of relying on that rescale.
  modeChanged = true;
}

void resetModeState() {
  lastStepTime = millis();
  walkPos = 0;
  rainbowHue = 0;
  chaseOffset = 0;
  breathPhase = 0.0f;
  larsonPos = 0;
  larsonDir = 1;
  memset(twinkleLevel, 0, sizeof(twinkleLevel));
  memset(larsonLevel, 0, sizeof(larsonLevel));
  audioNormalizedLevel = 0.0f;
  audioSmoothedLevel = 0.0f;
  audioClapFlash = 0.0f;

  // Static (non-animated) modes render once here; animated modes render
  // on their own timing inside updateAnimation().
  switch (currentMode) {
    case 0: // Off
      strip.clear();
      strip.show();
      break;
    case 1: // Solid Red
      strip.fill(strip.Color(255, 0, 0));
      strip.show();
      break;
    case 2: // Solid Green
      strip.fill(strip.Color(0, 255, 0));
      strip.show();
      break;
    case 3: // Solid Blue
      strip.fill(strip.Color(0, 0, 255));
      strip.show();
      break;
    case 4: // Solid White (dim)
      strip.fill(strip.Color(255, 255, 255)); // dimmed by global BRIGHTNESS
      strip.show();
      break;
    case MODE_AUDIO_PULSE:
      renderAudioIdleGlow(); // immediate idle glow; stepAudioPulse() takes over next frame
      break;
    default:
      strip.clear();
      strip.show();
      break;
  }
}

void stepForwardWalk() {
  const unsigned long interval = 50;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.clear();
  strip.setPixelColor(walkPos, strip.Color(255, 90, 0)); // amber
  strip.show();
  walkPos = (walkPos + 1) % NUM_LEDS;
}

void stepReverseWalk() {
  const unsigned long interval = 50;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.clear();
  strip.setPixelColor(walkPos, strip.Color(255, 90, 0)); // amber
  strip.show();
  walkPos = (walkPos == 0) ? (NUM_LEDS - 1) : (walkPos - 1);
}

void stepRainbow() {
  const unsigned long interval = 40;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.rainbow(rainbowHue, 1, 255, 255, true);
  strip.show();
  rainbowHue += 200;
}

void stepTheaterChase() {
  const unsigned long interval = 100;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.clear();
  for (int i = chaseOffset; i < NUM_LEDS; i += 3) {
    strip.setPixelColor(i, strip.Color(200, 0, 200)); // magenta
  }
  strip.show();
  chaseOffset = (chaseOffset + 1) % 3;
}

void stepBreathing() {
  const unsigned long interval = 20;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  // Sine-driven brightness scaling of a fixed color, magnitude only
  // (0..255 on the color channels themselves) -- global BRIGHTNESS stays
  // fixed, so peak output is still capped by strip.setBrightness().
  float level = (sinf(breathPhase) + 1.0f) * 0.5f; // 0..1
  uint8_t v = (uint8_t)(level * 255);
  strip.fill(strip.Color(v, v, v));
  strip.show();
  breathPhase += 0.06f;
  if (breathPhase > TWO_PI) breathPhase -= TWO_PI;
}

void stepTwinkle() {
  const unsigned long interval = 40;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  // Decay existing twinkles, occasionally ignite a new random pixel.
  for (int i = 0; i < NUM_LEDS; i++) {
    if (twinkleLevel[i] > 10) {
      twinkleLevel[i] -= 10;
    } else {
      twinkleLevel[i] = 0;
    }
  }
  if (random(0, 100) < 25) {
    int p = random(0, NUM_LEDS);
    twinkleLevel[p] = 255;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t v = twinkleLevel[i];
    strip.setPixelColor(i, strip.Color(v, v, v));
  }
  strip.show();
}

void stepLarson() {
  const unsigned long interval = 30;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  for (int i = 0; i < NUM_LEDS; i++) {
    larsonLevel[i] = (uint8_t)(larsonLevel[i] * 0.75f);
  }
  larsonLevel[larsonPos] = 255;

  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(larsonLevel[i], 0, 0)); // red
  }
  strip.show();

  larsonPos += larsonDir;
  if (larsonPos >= NUM_LEDS - 1 || larsonPos <= 0) {
    larsonDir = -larsonDir;
  }
}

// Warm, dim, uniform glow -- the AUDIO_PULSE baseline shown in silence and
// (unlike every other mode) also shown in place of blanking while muted.
void renderAudioIdleGlow() {
  uint8_t r = (uint8_t)(255 * AUDIO_IDLE_BRIGHTNESS);
  uint8_t g = (uint8_t)(120 * AUDIO_IDLE_BRIGHTNESS);
  uint8_t b = (uint8_t)(30 * AUDIO_IDLE_BRIGHTNESS);
  strip.fill(strip.Color(r, g, b));
  strip.show();
}

void stepAudioPulse() {
  const unsigned long interval = 30; // ~33Hz, smooth enough for a pulse effect
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  float rawRms = micLatestRms;

  // Normalize to 0..1 against the hardware-observed noise floor and a
  // ceiling roughly at the top of normal loud speech/music.
  float norm = (rawRms - AUDIO_NOISE_FLOOR) / (AUDIO_MAX_RMS - AUDIO_NOISE_FLOOR);
  norm = constrain(norm, 0.0f, 1.0f);
  audioNormalizedLevel = norm;

  // Fast attack / slow release: the visual jumps up quickly on speech or a
  // clap, but eases back down instead of flickering between samples.
  float rate = (norm > audioSmoothedLevel) ? AUDIO_ATTACK_SMOOTHING : AUDIO_RELEASE_SMOOTHING;
  audioSmoothedLevel += (norm - audioSmoothedLevel) * rate;

  // Clap/transient flash: a distinct threshold well above normal loud
  // speech (see AUDIO_CLAP_THRESHOLD comment), decaying quickly on its own
  // timeline rather than through the slow release smoothing above.
  bool clapNow = rawRms > AUDIO_CLAP_THRESHOLD;
  if (clapNow) {
    audioClapFlash = 1.0f;
  } else {
    audioClapFlash *= AUDIO_CLAP_DECAY;
  }

  // Render: idle glow everywhere, with a brighter region expanding outward
  // from the center as audioSmoothedLevel rises, plus a brief near-full
  // flash overlay on claps. Never calls strip.setBrightness() here, so the
  // user's currently-selected global brightness level is left untouched.
  const float center = (NUM_LEDS - 1) / 2.0f;
  for (int i = 0; i < NUM_LEDS; i++) {
    float dist = fabsf(i - center) / center; // 0 at center .. 1 at the ends
    float pulse = (audioSmoothedLevel - dist) / AUDIO_EDGE_SOFTNESS;
    pulse = constrain(pulse, 0.0f, 1.0f);

    float level = AUDIO_IDLE_BRIGHTNESS + pulse * (1.0f - AUDIO_IDLE_BRIGHTNESS);
    if (audioClapFlash > level) level = audioClapFlash;

    uint8_t r = (uint8_t)(255 * level);
    uint8_t g = (uint8_t)(120 * level);
    uint8_t b = (uint8_t)(30 * level);
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();

  unsigned long now = millis();
  if (now - audioLastDiagPrint >= 200) {
    audioLastDiagPrint = now;
    Serial.printf("[AUDIO] rms=%.0f norm=%.2f smoothed=%.2f clap=%d\n",
                  rawRms, audioNormalizedLevel, audioSmoothedLevel, clapNow ? 1 : 0);
  }
}

void updateAnimation() {
  switch (currentMode) {
    case 5: stepForwardWalk(); break;
    case 6: stepReverseWalk(); break;
    case 7: stepRainbow(); break;
    case 8: stepTheaterChase(); break;
    case 9: stepBreathing(); break;
    case 10: stepTwinkle(); break;
    case 11: stepLarson(); break;
    case MODE_AUDIO_PULSE: stepAudioPulse(); break;
    default: break; // modes 0-4 are static, already rendered on entry
  }
}

void setup() {
  Serial.begin(115200);

  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) {
    delay(10);
  }
  delay(300); // brief startup delay so a host-side terminal can attach

  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MUTE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BRIGHTNESS_PIN, INPUT_PULLUP);
  pinMode(BUTTON4_PIN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
  strip.clear();
  strip.show();

  Serial.println(F("[SYSTEM] LED controller started"));
  Serial.println(F("[BUTTON] GPIO10 using INPUT_PULLUP (Mode)"));
  Serial.println(F("[BUTTON] GPIO11 using INPUT_PULLUP (Mute)"));
  Serial.println(F("[BUTTON] GPIO17 using INPUT_PULLUP (Brightness)"));
  Serial.println(F("[BUTTON] GPIO5 using INPUT_PULLUP (Button 4 - hold: toggle AUDIO_PULSE, press: cycle audio modes)"));

  micReady = initMic(); // initMic() itself prints full boot diagnostics
  if (micReady) {
    // Capture runs continuously from boot (independent of the diagnostic
    // print toggle) so audio-reactive modes always have fresh RMS data.
    resetMicSession(millis());
  }

  currentMode = 0;
  modeChanged = true;
  printModeAnnouncement(); // one-time startup announcement; advanceMode()
                            // handles every subsequent mode-change print
}

void loop() {
  if (buttonPressedEdge(modeButton)) advanceMode();
  if (buttonPressedEdge(muteButton)) toggleMute();
  if (buttonPressedEdge(brightnessButton)) cycleBrightness();
  handleButton4(); // short press = cycle audio modes, long press = toggle AUDIO_PULSE

  updateMicDiagnostic(); // independent of LED mute state -- always serviced

  if (muted) return; // suppress LED rendering only; buttons/mic above still update

  if (modeChanged) {
    modeChanged = false;
    resetModeState();
  }

  updateAnimation();
}
