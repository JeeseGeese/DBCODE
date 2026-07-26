#pragma once

#include <Arduino.h>
#include "LedEffects.h"

// AUTO_SHOWCASE is a base LED mode (BaseEffect::AUTO_SHOWCASE) that
// automatically rotates through every OTHER real base effect. The
// rotation list is derived from NUM_REAL_BASE_EFFECTS (LedEffects.h --
// every enum value before AUTO_SHOWCASE itself), not a manually
// duplicated list, so it can never select AUTO_SHOWCASE internally and
// automatically includes any real effect added above it in the enum.
// None of the current 8 real effects are diagnostic/test-only/unsafe for
// unattended cycling, so no further exclusions are needed today.

// Starts/restarts the rotation from the first eligible effect. Call when
// the user selects AUTO_SHOWCASE (Controls.cpp's setBaseEffect()).
void startAutoShowcase(unsigned long now);

// Stops the timer. Call when the user leaves AUTO_SHOWCASE for a normal
// effect. AUTO_SHOWCASE isn't rendered while not selected regardless, but
// this keeps its internal state tidy for when it's re-entered.
void stopAutoShowcase();

// Advances the internal timer/effect selection -- non-blocking,
// millis()-based, no delay(). Call once per rendered frame while
// AUTO_SHOWCASE is the active base mode, before renderAutoShowcase().
void updateAutoShowcase(unsigned long now);

// Renders the current internal effect into buf, crossfading with the
// next effect during the AUTO_SHOWCASE_TRANSITION_MS window.
void renderAutoShowcase(RGB8 *buf, unsigned long now);

// Forces an immediate transition to the next eligible effect (serial 'c').
void autoShowcaseForceNext(unsigned long now);

BaseEffect getAutoShowcaseCurrentEffect();

// Milliseconds remaining before the next automatic transition begins (0
// while a transition is already in progress).
uint32_t getAutoShowcaseMsRemaining(unsigned long now);
