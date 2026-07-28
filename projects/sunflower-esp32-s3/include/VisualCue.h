#pragma once

#include <Arduino.h>
#include "LedEffects.h"

// A short, non-blocking full-strip flash that confirms an audio-overlay
// enable/disable action, independent of whatever the base effect and
// selected overlay are doing. Purely millis()-driven -- no delay().
enum class VisualCueType {
  NONE,
  OVERLAY_ENABLED,           // one soft green flash -- LED audio-reactive overlay ON
  OVERLAY_DISABLED,          // two soft red flashes -- LED audio-reactive overlay OFF
  MOTOR_AUDIO_REACTIVE_ON,   // one purple flash -- expressive AUDIO_REACTIVE motor mode ON
  MOTOR_AUDIO_REACTIVE_OFF,  // one gold flash -- expressive AUDIO_REACTIVE motor mode OFF
};

// Arms a new cue starting now. Replaces any cue already in progress.
void startVisualCue(VisualCueType type, uint32_t now);

// If a cue is currently active, renders this instant's flash state into
// buf[0..NUM_LEDS) and returns true (caller should skip normal base+overlay
// rendering this frame and use the cue's own brightness cap instead of the
// user-selected brightness). Returns false once the cue has finished, or if
// none is active -- the caller should then render normally.
bool renderVisualCue(uint32_t now, RGB8 *buf);
