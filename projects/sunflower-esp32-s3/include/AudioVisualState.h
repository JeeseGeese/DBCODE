#pragma once

#include <Arduino.h>
#include "AudioAnalyzer.h"

// A richer, centralized view over AudioFeatures for the audio overlays to
// render from, so they're not all driven off one scalar (the old
// envelope-only brightness modulation). Recomputed once per rendered
// frame in main.cpp; overlays never touch AudioFeatures/AudioAnalyzer
// directly.
//
// IMPORTANT -- signal provenance: this project has no FFT or filter bank.
// `bass` is the existing single-pole low-pass PROXY from AudioAnalyzer
// (see AUDIO_BASS_* in Config.h) -- a real (if simple) low-pass filter,
// but not a frequency-selective measurement in the FFT-bin sense.
// `lowRange`/`midRange`/`highRange` are further DERIVED animation-control
// heuristics built from bass/transient/envelope, NOT real frequency
// bands: `lowRange` reuses `bass` directly, `highRange` is a decaying
// "spike" signal seeded by transient/clap edges (not a measurement of
// high-frequency content, just something that behaves like one
// visually -- sharp/percussive sounds *do* tend to trigger transients),
// and `midRange` is the broadband envelope with the low/high
// contributions subtracted out. Treat these as three independently
// varying control channels for animation, not as a spectrum analyzer.
struct AudioVisualState {
  float level;              // == AudioFeatures::envelope (0..1, attack/release smoothed)
  float envelope;            // alias of level (kept as its own field for API clarity)
  float bass;                 // == AudioFeatures::lowFrequencyEnergy (low-pass proxy, 0..1)
  float transient;            // decaying 0..1 "spike": jumps to 1 on a transient/clap edge, decays over time
  float transientStrength;   // raw passthrough of AudioFeatures::transientStrength (instantaneous rise rate)
  bool clap;                  // passthrough of AudioFeatures::clap (single-frame edge)

  // Derived control bands -- see provenance note above. NOT real frequency bands.
  float lowRange;
  float midRange;
  float highRange;

  // 0-255 scaled copies of level/bass/transient, for cheap integer math
  // in per-LED inner loops where a float multiply isn't needed.
  uint8_t energy8;
  uint8_t bass8;
  uint8_t transient8;
};

// Recomputes the shared AudioVisualState from the latest AudioFeatures.
// Call exactly once per rendered frame, before rendering any overlay.
void updateAudioVisualState(const AudioFeatures &features, unsigned long now);

const AudioVisualState &getAudioVisualState();
