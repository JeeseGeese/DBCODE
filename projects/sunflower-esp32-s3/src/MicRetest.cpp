// BEGIN MIC RETEST
// See include/MicRetest.h for scope/removal instructions.
//
// This reads raw I2S samples directly off I2S_PORT, which is already
// installed/configured by AudioAnalyzer::initAudioAnalyzer() earlier in
// setup() (BCLK=6, WS=7, DIN=15, 16kHz, 32-bit word, ONLY_LEFT, standard
// I2S, DMA 4x256 -- verified unchanged before writing this file). This file
// does NOT call i2s_driver_install or i2s_set_pin again.
//
// Deliberately independent of AudioAnalyzer's production RMS/envelope/DC-
// correction pipeline: this reports raw, uncorrected sample statistics so
// the raw ADC/I2S data path can be judged on its own. It does not read or
// modify AUDIO_* calibration constants.
#include "MicRetest.h"

#if ENABLE_MIC_RETEST

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <stdint.h>

#include "Config.h"

namespace {

constexpr uint32_t PHASE_DURATION_MS = 6000;  // ~5s requested per phase; 5 phases x 6s = 30s total (meets "at least 30s")
constexpr uint32_t REPORT_INTERVAL_MS = 125;  // 8 reports/sec, within the requested 5-10/s
const TickType_t READ_TIMEOUT_TICKS = pdMS_TO_TICKS(20);  // same read timeout AudioAnalyzer.cpp uses

// 24-bit signed sample range is -8,388,608..8,388,607. Reuses the existing
// MIC_SATURATION_THRESHOLD constant (Config.h: "near max of the 24-bit
// signed sample range") as the clip cutoff rather than inventing a new
// threshold, per instructions not to touch calibration values.
constexpr int32_t CLIP_THRESHOLD = MIC_SATURATION_THRESHOLD;

struct WindowStats {
  int32_t rawMin = INT32_MAX;
  int32_t rawMax = INT32_MIN;
  int32_t peak = 0;
  double sumSquares = 0.0;
  uint32_t samples = 0;
  uint32_t zeroSamples = 0;
  uint32_t clippedSamples = 0;
  uint32_t readErrors = 0;
  uint32_t reads = 0;
  uint32_t fullZeroReads = 0;  // i2s_read calls where every decoded sample was exactly 0
};

struct PhaseTotals {
  int32_t rawMin = INT32_MAX;
  int32_t rawMax = INT32_MIN;
  int32_t peak = 0;
  uint32_t rmsMin = UINT32_MAX;
  uint32_t rmsMax = 0;
  uint32_t samples = 0;
  uint32_t zeroSamples = 0;
  uint32_t clippedSamples = 0;
  uint32_t readErrors = 0;
  uint32_t reads = 0;
  uint32_t fullZeroReads = 0;
};

float smoothedRms = 0.0f;
constexpr float SMOOTH_ALPHA = 0.3f;              // diagnostic-only smoothing; independent of production AUDIO_ATTACK/RELEASE_SMOOTHING
constexpr float NORMALIZE_REFERENCE = 400000.0f;  // diagnostic-only 0..1 scale; independent of production AUDIO_MAX_RMS

void mergeIntoPhase(PhaseTotals &p, const WindowStats &w, uint32_t rmsThisReport) {
  if (w.samples == 0 && w.readErrors == 0) return;
  if (w.rawMin < p.rawMin) p.rawMin = w.rawMin;
  if (w.rawMax > p.rawMax) p.rawMax = w.rawMax;
  if (w.peak > p.peak) p.peak = w.peak;
  if (w.samples > 0) {
    if (rmsThisReport < p.rmsMin) p.rmsMin = rmsThisReport;
    if (rmsThisReport > p.rmsMax) p.rmsMax = rmsThisReport;
  }
  p.samples += w.samples;
  p.zeroSamples += w.zeroSamples;
  p.clippedSamples += w.clippedSamples;
  p.readErrors += w.readErrors;
  p.reads += w.reads;
  p.fullZeroReads += w.fullZeroReads;
}

void printPhaseSummary(const char *phaseName, const PhaseTotals &p) {
  Serial.printf(
      "[MIC RETEST] %s summary: samples=%lu reads=%lu rawMin=%ld rawMax=%ld peak=%ld "
      "rmsRange=[%lu..%lu] zero=%lu (fullZeroReads=%lu/%lu) clipped=%lu errors=%lu\n",
      phaseName, (unsigned long)p.samples, (unsigned long)p.reads, (long)p.rawMin, (long)p.rawMax, (long)p.peak,
      (unsigned long)(p.rmsMin == UINT32_MAX ? 0 : p.rmsMin), (unsigned long)p.rmsMax, (unsigned long)p.zeroSamples,
      (unsigned long)p.fullZeroReads, (unsigned long)p.reads, (unsigned long)p.clippedSamples,
      (unsigned long)p.readErrors);
}

// Runs one test phase for durationMs, printing a [MIC TEST] line every
// REPORT_INTERVAL_MS (~5-10Hz), and returns the phase's aggregate stats.
PhaseTotals runPhase(const char *phaseName, uint32_t durationMs) {
  Serial.printf("[MIC RETEST] ---- %s (%lus) ----\n", phaseName, (unsigned long)(durationMs / 1000));

  PhaseTotals phaseTotals;
  WindowStats win;
  int32_t sampleBuf[256];  // matches the DMA buf len (4 x 256) configured in AudioAnalyzer::initAudioAnalyzer()

  unsigned long phaseStart = millis();
  unsigned long lastReport = phaseStart;

  while (millis() - phaseStart < durationMs) {
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT, sampleBuf, sizeof(sampleBuf), &bytesRead, READ_TIMEOUT_TICKS);
    win.reads++;

    if (err != ESP_OK || bytesRead == 0) {
      win.readErrors++;
    } else {
      int samples = bytesRead / sizeof(int32_t);
      bool allZeroThisRead = true;

      for (int i = 0; i < samples; i++) {
        // Same conversion as AudioAnalyzer.cpp's captureSamples(): the
        // INMP441 left-justifies its 24-bit sample in the top 24 bits of
        // the 32-bit I2S word (bottom 8 bits are zero padding), so the raw
        // word already equals originalValue << 8. Arithmetic right-shift
        // by 8 undoes that AND sign-extends correctly in the same step,
        // because operator>> on a signed type is an arithmetic shift on
        // this toolchain (GCC xtensa/riscv ESP32 targets, always, for
        // built-in signed integer types). sampleBuf is int32_t (signed)
        // throughout -- no accidental unsigned conversion.
        int32_t s = sampleBuf[i] >> 8;

        if (s < win.rawMin) win.rawMin = s;
        if (s > win.rawMax) win.rawMax = s;

        // By construction, s is bounded to the true 24-bit signed range
        // (-8,388,608..8,388,607) -- the most-negative case lands exactly
        // on -2^23, not INT32_MIN, so a plain negate-if-negative cannot hit
        // the classic abs(INT32_MIN) overflow trap. Guarded explicitly
        // anyway so the invariant holds even if this snippet is ever
        // reused against a wider raw value.
        int32_t mag = (s == INT32_MIN) ? INT32_MAX : (s < 0 ? -s : s);
        if (mag > win.peak) win.peak = mag;

        // double: (8.4e6)^2 summed over any realistic window is nowhere
        // close to overflowing even a float's mantissa, let alone a double.
        win.sumSquares += (double)s * (double)s;

        if (s == 0) {
          win.zeroSamples++;
        } else {
          allZeroThisRead = false;
        }

        if (mag >= CLIP_THRESHOLD) win.clippedSamples++;

        win.samples++;
      }

      if (allZeroThisRead) win.fullZeroReads++;
    }

    unsigned long now = millis();
    if (now - lastReport >= REPORT_INTERVAL_MS) {
      lastReport = now;

      uint32_t rms = 0;
      float level = 0.0f;
      if (win.samples > 0) {
        rms = (uint32_t)sqrt(win.sumSquares / win.samples);
        smoothedRms += ((float)rms - smoothedRms) * SMOOTH_ALPHA;
        level = constrain((float)rms / NORMALIZE_REFERENCE, 0.0f, 1.0f);
      } else {
        smoothedRms += (0.0f - smoothedRms) * SMOOTH_ALPHA;
      }

      Serial.printf(
          "[MIC TEST] samples=%lu min=%ld max=%ld peak=%ld rms=%lu smooth=%lu level=%.2f zero=%lu clipped=%lu errors=%lu\n",
          (unsigned long)win.samples, (long)(win.samples ? win.rawMin : 0), (long)(win.samples ? win.rawMax : 0),
          (long)win.peak, (unsigned long)rms, (unsigned long)smoothedRms, level, (unsigned long)win.zeroSamples,
          (unsigned long)win.clippedSamples, (unsigned long)win.readErrors);

      mergeIntoPhase(phaseTotals, win, rms);
      win = WindowStats();
    }
  }

  // Flush a final partial window shorter than REPORT_INTERVAL_MS.
  if (win.samples > 0 || win.readErrors > 0) {
    uint32_t rms = win.samples > 0 ? (uint32_t)sqrt(win.sumSquares / win.samples) : 0;
    mergeIntoPhase(phaseTotals, win, rms);
  }

  printPhaseSummary(phaseName, phaseTotals);
  return phaseTotals;
}

}  // namespace

void runMicRetest() {
  Serial.println(F("[MIC RETEST] ==================================="));
  Serial.println(F("[MIC RETEST] Dedicated microphone retest start"));
  Serial.printf("[MIC RETEST] Reusing existing I2S init: BCLK=%d WS=%d DIN=%d rate=%dHz word=32-bit channel=LEFT\n",
                I2S_BCLK_PIN, I2S_WS_PIN, I2S_DIN_PIN, I2S_SAMPLE_RATE);
  Serial.println(F("[MIC RETEST] (I2S driver already installed by AudioAnalyzer::initAudioAnalyzer(); not reinitialized here)"));

  PhaseTotals quiet1 = runPhase("Phase 1: Quiet room", PHASE_DURATION_MS);
  PhaseTotals speaking = runPhase("Phase 2: Normal speaking", PHASE_DURATION_MS);
  PhaseTotals clap = runPhase("Phase 3: Clap / finger snap", PHASE_DURATION_MS);
  PhaseTotals quiet2 = runPhase("Phase 4: Quiet room again", PHASE_DURATION_MS);
  PhaseTotals handling = runPhase("Phase 5: Gentle handling / tapping", PHASE_DURATION_MS);

  int32_t overallMax = max(max(quiet1.rawMax, speaking.rawMax), max(max(clap.rawMax, quiet2.rawMax), handling.rawMax));
  int32_t overallMin = min(min(quiet1.rawMin, speaking.rawMin), min(min(clap.rawMin, quiet2.rawMin), handling.rawMin));
  uint32_t totalZero =
      quiet1.zeroSamples + speaking.zeroSamples + clap.zeroSamples + quiet2.zeroSamples + handling.zeroSamples;
  uint32_t totalClipped =
      quiet1.clippedSamples + speaking.clippedSamples + clap.clippedSamples + quiet2.clippedSamples + handling.clippedSamples;
  uint32_t totalErrors =
      quiet1.readErrors + speaking.readErrors + clap.readErrors + quiet2.readErrors + handling.readErrors;
  uint32_t totalFullZeroReads =
      quiet1.fullZeroReads + speaking.fullZeroReads + clap.fullZeroReads + quiet2.fullZeroReads + handling.fullZeroReads;
  uint32_t totalReads = quiet1.reads + speaking.reads + clap.reads + quiet2.reads + handling.reads;

  Serial.println(F("[MIC RETEST] --- overall ---"));
  Serial.printf(
      "[MIC RETEST] overallRawRange=[%ld..%ld] totalZeroSamples=%lu totalFullZeroReads=%lu/%lu totalClipped=%lu totalErrors=%lu\n",
      (long)overallMin, (long)overallMax, (unsigned long)totalZero, (unsigned long)totalFullZeroReads,
      (unsigned long)totalReads, (unsigned long)totalClipped, (unsigned long)totalErrors);

  Serial.println(F("[MIC RETEST] Dedicated microphone retest end"));
  Serial.println(F("[MIC RETEST] ==================================="));
}

#else

void runMicRetest() {}

#endif
// END MIC RETEST
