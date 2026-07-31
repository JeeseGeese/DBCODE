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
  // BEGIN HARDWARE TEST SUPPORT -- exposes the peak magnitude already
  // computed internally each window (previously computed, never stored).
  // Remove this field + its one assignment in AudioAnalyzer.cpp alongside
  // HardwareTest.h/.cpp when the hardware test is removed.
  float peak; // raw DC-corrected peak sample magnitude, this window
  // END HARDWARE TEST SUPPORT
};

// Verifies the shared full-duplex I2S bus (see include/SharedI2S.h,
// I2S_NUM_0, 16kHz, 32-bit-per-slot, stereo RIGHT_LEFT) is ready and sets
// up microphone processing state -- does NOT install or configure the I2S
// driver itself; call initSharedI2S() first. Extracts only the mic's
// active slot (Config.h's MIC_I2S_SLOT_INDEX) from each stereo RX frame,
// preserving the original 24-bit-left-justified sample interpretation.
// Prints its own success/failure diagnostics. Returns true on success.
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
// folded into 'status'. Always prints when called -- this is a deliberate,
// on-demand pull, not the continuous background output gated below, so it
// is NOT affected by setAudioLogEnabled().
void printAudioDiagnostics();

// Gates the CONTINUOUS/repeating audio output only: the periodic [AUDIO]
// rms/env heartbeat and the mic fault-check WARN/HINT messages (samples
// stuck/saturated/zero, I2S reads returning 0 repeatedly) -- these are what
// make the serial monitor hard to read during normal operation. Does NOT
// affect: I2S init success/error banners (one-time, always print),
// microphone sampling, envelope/RMS computation, audio-reactive LED
// rendering, or on-demand dumps (printAudioDiagnostics()/'status') --
// those are separate concepts (see main.cpp's '7' command comment).
// Defaults to disabled (quiet) -- see initAudioAnalyzer().
void setAudioLogEnabled(bool enabled);
bool isAudioLogEnabled();
