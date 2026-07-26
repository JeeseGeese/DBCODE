#pragma once

#include <Arduino.h>
#include "AudioVisualState.h"
#include "Config.h"
#include "LedEffects.h"

enum class AudioOverlay : uint8_t {
  OFF,
  PULSE,
  RIPPLE,
  SPARK,
  LIGHTNING,
  BASS_BLOOM,
  SPECTRUM_WAVE,
  COLOR_FLOOD,
  COMET_BURST,
  COUNT,
};
constexpr uint8_t NUM_AUDIO_OVERLAYS = static_cast<uint8_t>(AudioOverlay::COUNT);

extern const char *AUDIO_OVERLAY_NAMES[NUM_AUDIO_OVERLAYS];

// Clears an overlay's persistent state (active ripples/sparks/comets/flash
// timers) so switching overlays doesn't leave stale visual artifacts armed.
void resetAudioOverlayState(AudioOverlay overlay, unsigned long now);

// Blends `overlay` onto buf[0..NUM_LEDS) in place, on top of whatever the
// base effect already rendered there. OFF is a no-op. Never calls
// strip.show() or touches brightness/power limiting -- purely a frame
// buffer transform. Reads the richer AudioVisualState (level/bass/
// transient/derived low-mid-high bands) rather than raw AudioFeatures, so
// overlays aren't all driven off one scalar.
void applyAudioOverlay(AudioOverlay overlay, RGB8 *buf, const AudioVisualState &audio, unsigned long now);

// Diagnostic counts for 'status'/'v' -- how many pool slots are currently
// active in the overlay that owns a pool (ripples/sparks/comets). Returns
// 0 for overlays without a pool or when a different overlay is selected.
uint8_t getActiveRippleCount();
uint8_t getActiveSparkCount();
uint8_t getActiveCometCount();
