#pragma once
#include <stdint.h>

#include "AudioAnalyzer.h"

// High-level personality-state coordinator. Owns none of MotorDriver,
// MotorPowerGuard, LED pixel rendering, microphone sampling, serial input,
// or buttons -- it orchestrates the existing ExpressiveMotion module
// exclusively through that module's public API (in particular
// requestExpressivePattern() and setExpressiveMotionMode(), see
// ExpressiveMotion.h), the same way every other caller in this codebase
// does. Hardware-independent: nothing here reads a pin or touches `strip`.
// Disabled by default (BehaviorState::MANUAL at boot) -- the user/a future
// Raspberry Pi companion must explicitly select a state. See
// docs/BEHAVIOR_ENGINE_DEVELOPMENT.md for the full design writeup.
//
// Development-branch feature (feature/expressive-motion-v1) -- does not
// touch the v1.0.0-tagged baseline's own commands/behavior.

enum class BehaviorState {
  MANUAL,     // Behavior Engine inert -- user/ExpressiveMotion's own mode has direct, unmediated control
  IDLE,       // calm ambient presence -- delegates to ExpressiveMotion's own IDLE_ALIVE engine
  CURIOUS,    // frequent small investigative movement, occasional longer pauses
  LISTENING,  // one initial gentle lean/nod, then mostly still with rare small nods
  THINKING,   // slow, sparse, gentle movement; long pauses
  EXCITED,    // finite energetic episode, auto-returns to IDLE on expiry
  SLEEPING,   // movement fully at rest; no owned motor activity
};

void initBehaviorEngine();

// Must be called every loop() iteration (unconditionally -- movement
// requests are self-gating via ExpressiveMotion's own diagnostic/mode
// checks, so no additional pause/skip logic is needed here, unlike
// ExpressiveMotion's own pauseExpressiveMotion() gating). Non-blocking; no
// delay(). `features` is read-only, for simple state-local activity
// awareness (see LISTENING's clap nudge in BehaviorEngine.cpp) -- the
// Behavior Engine never reads the microphone or AudioAnalyzer itself.
void updateBehaviorEngine(uint32_t nowMs, const AudioFeatures &features);

// The single transition function -- every state change goes through this.
// Refuses (returns false, state unchanged) if `state` is one of
// CURIOUS/LISTENING/THINKING/EXCITED (the movement-producing states) and a
// motor/LED diagnostic ('2'/'3'/'5'/'6') is currently active, matching
// Button4 long-press's existing refusal precedent (see Controls.cpp). Every
// other transition (MANUAL/IDLE/SLEEPING, or the no-op same-state case) is
// always accepted. On every accepted transition: cancels any in-flight
// Behavior-Engine-owned pattern and resets ExpressiveMotion's own idle
// timing via setExpressiveMotionMode(OFF), releases MotorPowerGuard,
// restores MotorLedPowerMode to FULL_MUTE, resets every Behavior-Engine-
// owned timer/counter, and cancels a running 'behavior demo' if one was
// active. Never changes base LED effect, overlay, brightness, or mute.
bool setBehaviorState(BehaviorState state);

BehaviorState getBehaviorState();
const char *behaviorStateName(BehaviorState state);

// Emergency-stop handling (called from main.cpp's serviceEmergencyStop(),
// alongside cancelExpressiveMotion()): forces BehaviorState to MANUAL and
// resets every Behavior-Engine-owned timer/counter immediately. Does not
// itself touch the motor -- cancelExpressiveMotion() (called separately by
// serviceEmergencyStop()) already stops/releases it regardless of which
// caller started the in-flight pattern. Idempotent.
void stopBehaviorEngine();

// 'behavior demo' -- see docs/BEHAVIOR_ENGINE_DEVELOPMENT.md. Refuses to
// start while any motor/LED diagnostic is active, or while a demo is
// already running. Walks IDLE -> CURIOUS -> LISTENING -> THINKING ->
// EXCITED -> SLEEPING -> MANUAL, a fixed dwell per state (see Config.h's
// BEHAVIOR_DEMO_*_MS), cancelable at any point via 'k'.
bool startBehaviorDemo();
bool isBehaviorDemoActive();

// For the '?' status command and the 'behavior status' word command.
void printBehaviorStatus();
