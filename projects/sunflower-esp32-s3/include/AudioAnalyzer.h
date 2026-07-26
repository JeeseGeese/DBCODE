#pragma once

#include <Arduino.h>
#include "Config.h"

// Snapshot of the current audio analysis state, recomputed once per
// analysis window (~MIC_PRINT_INTERVAL_MS). Overlays read this; they never
// touch the I2S/capture internals directly.
struct AudioFeatures {
  float rms;                // raw DC-corrected RMS, this window
  float normalized;         // 0..1 against the adaptive noise floor / AUDIO_MAX_RMS ceiling
  float envelope;           // attack/release-smoothed `normalized`
  float transientStrength;  // normalized units/second rise rate of `envelope`
  float lowFrequencyEnergy; // 0..1 low-frequency proxy (see AUDIO_BASS_* in Config.h) -- not true FFT bass
  bool clap;                // edge-triggered, cooldown-gated: a sharp loud event just happened
  bool transient;           // edge-triggered, cooldown-gated: a fast envelope rise just happened
};

// I2S init (same verified INMP441 config as before: I2S_NUM_0, 16kHz,
// 32-bit word / 24-bit left-justified, LEFT channel). Prints its own
// success/failure diagnostics. Returns true on success.
bool initAudioAnalyzer();

// Must be called every loop() iteration, unconditionally (including while
// muted) -- this is what keeps I2S capture and AudioFeatures fresh
// regardless of what's being rendered.
void updateAudioAnalyzer();

bool isMicReady();
const AudioFeatures &getAudioFeatures();
float getNoiseFloorEstimate();

// Full diagnostic dump: current AudioFeatures plus the active tuning
// constants. Used by Button4 double-press, the 'd' serial command, and
// folded into 'status'.
void printAudioDiagnostics();
