#include "VisualCue.h"

struct CuePhase {
  bool on;
  uint32_t durationMs;
  const char *label; // for the [CUE] transition diagnostics only
};

// ENABLED: leading black (cuts cleanly away from whatever the base effect
// was showing, so the cue can't visually blend into the prior frame),
// then one deliberate green flash, then a short recovery gap.
static const CuePhase ENABLED_PHASES[] = {
    {false, 60, "OFF"},
    {true, 300, "GREEN"},
    {false, 120, "OFF"},
};
// DISABLED: same leading black, then two distinct red flashes separated
// by a clearly-off gap, then a short recovery gap.
static const CuePhase DISABLED_PHASES[] = {
    {false, 60, "OFF"},
    {true, 180, "RED"},
    {false, 140, "OFF"},
    {true, 180, "RED"},
    {false, 120, "OFF"},
};

// Fully saturated -- physical testing showed the previous desaturated
// tones ({40,200,90} / {210,45,35}) read as dim/muddy rather than clearly
// green/red, especially at the (then-lower) brightness cap.
static const RGB8 ENABLED_COLOR = {0, 255, 0};  // pure green
static const RGB8 DISABLED_COLOR = {255, 0, 0}; // pure red

struct VisualCueState {
  VisualCueType type = VisualCueType::NONE;
  uint8_t phase = 0;
  uint32_t phaseStartedMs = 0;
  bool active = false;
};
static VisualCueState cue;

static const CuePhase *phasesFor(VisualCueType type, uint8_t &count) {
  if (type == VisualCueType::OVERLAY_ENABLED) {
    count = sizeof(ENABLED_PHASES) / sizeof(ENABLED_PHASES[0]);
    return ENABLED_PHASES;
  }
  if (type == VisualCueType::OVERLAY_DISABLED) {
    count = sizeof(DISABLED_PHASES) / sizeof(DISABLED_PHASES[0]);
    return DISABLED_PHASES;
  }
  count = 0;
  return nullptr;
}

static const char *cueTypeName(VisualCueType type) {
  return (type == VisualCueType::OVERLAY_ENABLED) ? "AUDIO_ENABLED" : "AUDIO_DISABLED";
}

// Retrigger semantics: unconditionally overwrites any in-progress cue --
// there is exactly one cue slot, never a queue, so the newest toggle
// always wins and starts clean from phase 0.
void startVisualCue(VisualCueType type, uint32_t now) {
  if (type == VisualCueType::NONE) {
    cue.active = false;
    return;
  }

  uint8_t count;
  const CuePhase *phases = phasesFor(type, count);

  cue.type = type;
  cue.phase = 0;
  cue.phaseStartedMs = now;
  cue.active = true;

  Serial.printf("[CUE] Start: %s\n", cueTypeName(type));
  if (phases && count > 0) {
    Serial.printf("[CUE] Phase 0: %s %ums\n", phases[0].label, (unsigned)phases[0].durationMs);
  }
}

bool renderVisualCue(uint32_t now, RGB8 *buf) {
  if (!cue.active) return false;

  uint8_t count;
  const CuePhase *phases = phasesFor(cue.type, count);
  if (!phases) {
    cue.active = false;
    return false;
  }

  // Wrap-safe unsigned elapsed time; advances through (and prints) every
  // phase boundary crossed, carrying overshoot forward into the next
  // phase's baseline rather than resetting to `now` -- so timing doesn't
  // drift even if a call is late (e.g. right after an unmute).
  uint32_t elapsed = now - cue.phaseStartedMs;
  while (cue.phase < count && elapsed >= phases[cue.phase].durationMs) {
    elapsed -= phases[cue.phase].durationMs;
    cue.phaseStartedMs += phases[cue.phase].durationMs;
    cue.phase++;

    if (cue.phase < count) {
      Serial.printf("[CUE] Phase %u: %s %ums\n", cue.phase, phases[cue.phase].label,
                    (unsigned)phases[cue.phase].durationMs);
    } else {
      Serial.println(F("[CUE] Complete"));
    }
  }

  if (cue.phase >= count) {
    cue.active = false;
    return false;
  }

  RGB8 color = (cue.type == VisualCueType::OVERLAY_ENABLED) ? ENABLED_COLOR : DISABLED_COLOR;
  bool on = phases[cue.phase].on;
  RGB8 frameColor = on ? color : RGB8{0, 0, 0};
  // Every LED explicitly written every cue frame, including OFF phases --
  // never leaves a stale base-effect pixel showing through.
  for (int i = 0; i < NUM_LEDS; i++) buf[i] = frameColor;
  return true;
}
