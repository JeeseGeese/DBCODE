#include "BehaviorEngine.h"

#include <Arduino.h>

#include "Config.h"
#include "Controls.h"
#include "ExpressiveMotion.h"
#include "MotorPowerGuard.h"

namespace {

BehaviorState state = BehaviorState::MANUAL;
unsigned long stateEnteredMs = 0;

// --- Generic movement scheduler, shared by CURIOUS/LISTENING/THINKING/
// EXCITED -- each state periodically requests one of a small set of
// candidate patterns from ExpressiveMotion via requestExpressivePattern(),
// on its own randomized interval (see Config.h's BEHAVIOR_* constants).
// Entirely independent of ExpressiveMotion's own IDLE_ALIVE/AUDIO_REACTIVE
// idle-timer/audio-trigger logic, which stays inert (mode==OFF) for the
// whole time any of these four states is active.
unsigned long movementDeadlineMs = 0;
bool movementDeadlineSet = false;
ExpressivePattern lastRequestedPattern = ExpressivePattern::NONE;

// --- EXCITED: bounded episode duration, auto-returns to IDLE on expiry ---
unsigned long excitedEpisodeDeadlineMs = 0;
bool excitedEpisodeDeadlineSet = false;

struct MovementProfile {
  const ExpressivePattern *candidates;
  uint8_t count;
  uint32_t minMs;
  uint32_t maxMs;
};

constexpr ExpressivePattern CURIOUS_PATTERNS[] = {
    ExpressivePattern::FORWARD_REVERSE_NOD,
    ExpressivePattern::DOUBLE_TWITCH,
    ExpressivePattern::GENTLE_SWAY,
};
constexpr ExpressivePattern LISTENING_PATTERNS[] = {
    ExpressivePattern::GENTLE_SWAY,
    ExpressivePattern::FORWARD_REVERSE_NOD,
};
constexpr ExpressivePattern THINKING_PATTERNS[] = {
    ExpressivePattern::GENTLE_SWAY,
    ExpressivePattern::SETTLE,
};
// No AUDIO_CLAP_RECOIL here -- EXCITED is a deliberately-entered episode,
// not itself an audio reaction (that remains ExpressiveMotionMode::
// AUDIO_REACTIVE's job, unrelated to the Behavior Engine).
constexpr ExpressivePattern EXCITED_PATTERNS[] = {
    ExpressivePattern::EXCITED_TRIPLE,
    ExpressivePattern::AUDIO_STRONG_BURST,
    ExpressivePattern::DRAMATIC_SWEEP,
    ExpressivePattern::FORWARD_REVERSE_NOD,
};

constexpr MovementProfile CURIOUS_PROFILE{CURIOUS_PATTERNS, 3, BEHAVIOR_CURIOUS_ACTION_MIN_MS,
                                           BEHAVIOR_CURIOUS_ACTION_MAX_MS};
constexpr MovementProfile LISTENING_PROFILE{LISTENING_PATTERNS, 2, BEHAVIOR_LISTENING_NOD_MIN_MS,
                                             BEHAVIOR_LISTENING_NOD_MAX_MS};
constexpr MovementProfile THINKING_PROFILE{THINKING_PATTERNS, 2, BEHAVIOR_THINKING_ACTION_MIN_MS,
                                            BEHAVIOR_THINKING_ACTION_MAX_MS};
constexpr MovementProfile EXCITED_PROFILE{EXCITED_PATTERNS, 4, BEHAVIOR_EXCITED_ACTION_MIN_MS,
                                           BEHAVIOR_EXCITED_ACTION_MAX_MS};

bool isMovementProducing(BehaviorState s) {
  return s == BehaviorState::CURIOUS || s == BehaviorState::LISTENING || s == BehaviorState::THINKING ||
         s == BehaviorState::EXCITED;
}

const MovementProfile *profileFor(BehaviorState s) {
  switch (s) {
    case BehaviorState::CURIOUS: return &CURIOUS_PROFILE;
    case BehaviorState::LISTENING: return &LISTENING_PROFILE;
    case BehaviorState::THINKING: return &THINKING_PROFILE;
    case BehaviorState::EXCITED: return &EXCITED_PROFILE;
    default: return nullptr;
  }
}

uint32_t randomRange(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

void armMovementDeadline(unsigned long now, const MovementProfile &profile) {
  movementDeadlineMs = now + randomRange(profile.minMs, profile.maxMs);
  movementDeadlineSet = true;
}

void resetMovementTimers() {
  movementDeadlineSet = false;
  excitedEpisodeDeadlineSet = false;
}

// Attempts one pattern request when the current deadline elapses; re-arms a
// fresh randomized interval on success, or retries much sooner
// (BEHAVIOR_MOVEMENT_RETRY_MS) if ExpressiveMotion refused (still finishing
// a previous pattern, or a diagnostic started concurrently -- see
// requestExpressivePattern()'s contract).
void updateScheduledMovement(unsigned long now, const MovementProfile &profile) {
  if (!movementDeadlineSet) {
    armMovementDeadline(now, profile);
    return;
  }
  if ((long)(now - movementDeadlineMs) < 0) return;

  ExpressivePattern chosen = profile.candidates[random(0, profile.count)];
  if (requestExpressivePattern(chosen)) {
    lastRequestedPattern = chosen;
    armMovementDeadline(now, profile);
  } else {
    movementDeadlineMs = now + BEHAVIOR_MOVEMENT_RETRY_MS;
  }
}

// --- 'behavior demo' ---
struct DemoStep {
  BehaviorState state;
  uint32_t durationMs;
};
constexpr DemoStep DEMO_SEQUENCE[] = {
    {BehaviorState::IDLE, BEHAVIOR_DEMO_IDLE_MS},
    {BehaviorState::CURIOUS, BEHAVIOR_DEMO_CURIOUS_MS},
    {BehaviorState::LISTENING, BEHAVIOR_DEMO_LISTENING_MS},
    {BehaviorState::THINKING, BEHAVIOR_DEMO_THINKING_MS},
    {BehaviorState::EXCITED, BEHAVIOR_DEMO_EXCITED_MS},
    {BehaviorState::SLEEPING, BEHAVIOR_DEMO_SLEEPING_MS},
};
constexpr uint8_t DEMO_SEQUENCE_COUNT = sizeof(DEMO_SEQUENCE) / sizeof(DEMO_SEQUENCE[0]);

bool demoActive = false;
uint8_t demoStepIndex = 0;
unsigned long demoStepStartMs = 0;

// The actual transition body, shared by the public setBehaviorState() and
// the demo's own advancement (updateBehaviorDemo() calls this directly so
// its own scheduled transitions don't re-cancel the demo it belongs to --
// see setBehaviorState()'s doc comment on why it cancels a running demo).
bool transitionTo(BehaviorState newState) {
  if (newState == state) {
    Serial.printf("[BEHAVIOR] %s (unchanged)\n", behaviorStateName(newState));
    return true;
  }
  if (isMovementProducing(newState) && isAnyMotorDiagnosticActive()) {
    Serial.printf("[BEHAVIOR] Refused: %s requires movement and a motor diagnostic is active\n",
                  behaviorStateName(newState));
    return false;
  }

  BehaviorState oldState = state;

  // Cancels any in-flight pattern (regardless of who started it -- the
  // Behavior Engine's own scheduler above, or the user's prior manual
  // mode), releases MotorPowerGuard, restores MotorLedPowerMode to
  // FULL_MUTE, and resets ExpressiveMotion's own idle timing -- see
  // ExpressiveMotion.cpp's forceReleaseOrdinaryMovement(), which
  // setExpressiveMotionMode() calls internally on every transition.
  // Deliberately NOT cancelExpressiveMotion(): that is the emergency-stop
  // path (prints "Emergency stop", latches ExpressiveMotion's own
  // emergency-stopped flag) and would misreport a routine personality
  // change as an emergency.
  setExpressiveMotionMode(ExpressiveMotionMode::OFF);
  resetMovementTimers();

  state = newState;
  stateEnteredMs = millis();

  switch (newState) {
    case BehaviorState::IDLE:
      // Suggested behavior: delegate to ExpressiveMotion's own native
      // weighted-random idle engine rather than a second scheduler.
      setExpressiveMotionMode(ExpressiveMotionMode::IDLE_ALIVE);
      break;
    case BehaviorState::EXCITED:
      excitedEpisodeDeadlineMs =
          stateEnteredMs + randomRange(BEHAVIOR_EXCITED_EPISODE_MIN_MS, BEHAVIOR_EXCITED_EPISODE_MAX_MS);
      excitedEpisodeDeadlineSet = true;
      break;
    case BehaviorState::LISTENING: {
      // "Begin with one gentle forward-style lean or nod" -- attempt
      // immediately rather than waiting for the first scheduled interval.
      ExpressivePattern first =
          (random(0, 2) == 0) ? ExpressivePattern::GENTLE_SWAY : ExpressivePattern::FORWARD_REVERSE_NOD;
      if (requestExpressivePattern(first)) lastRequestedPattern = first;
      armMovementDeadline(stateEnteredMs, LISTENING_PROFILE);
      break;
    }
    default:
      break;  // MANUAL, CURIOUS, THINKING, SLEEPING need no extra entry action
  }

  Serial.printf("[BEHAVIOR] %s -> %s\n", behaviorStateName(oldState), behaviorStateName(newState));
  return true;
}

void updateBehaviorDemo(unsigned long now) {
  if (!demoActive) return;
  if (now - demoStepStartMs < DEMO_SEQUENCE[demoStepIndex].durationMs) return;

  demoStepIndex++;
  if (demoStepIndex >= DEMO_SEQUENCE_COUNT) {
    Serial.println(F("[BEHAVIOR DEMO] Complete"));
    demoActive = false;
    transitionTo(BehaviorState::MANUAL);
    return;
  }
  Serial.printf("[BEHAVIOR DEMO] State: %s\n", behaviorStateName(DEMO_SEQUENCE[demoStepIndex].state));
  transitionTo(DEMO_SEQUENCE[demoStepIndex].state);
  demoStepStartMs = now;
}

}  // namespace

const char *behaviorStateName(BehaviorState s) {
  switch (s) {
    case BehaviorState::MANUAL: return "MANUAL";
    case BehaviorState::IDLE: return "IDLE";
    case BehaviorState::CURIOUS: return "CURIOUS";
    case BehaviorState::LISTENING: return "LISTENING";
    case BehaviorState::THINKING: return "THINKING";
    case BehaviorState::EXCITED: return "EXCITED";
    case BehaviorState::SLEEPING: return "SLEEPING";
  }
  return "UNKNOWN";
}

void initBehaviorEngine() {
  state = BehaviorState::MANUAL;
  stateEnteredMs = millis();
  resetMovementTimers();
  demoActive = false;
  demoStepIndex = 0;
  lastRequestedPattern = ExpressivePattern::NONE;
}

void updateBehaviorEngine(uint32_t nowMs, const AudioFeatures &features) {
  updateBehaviorDemo(nowMs);
  if (demoActive) return;  // the demo owns state transitions exclusively while running

  switch (state) {
    case BehaviorState::MANUAL:
    case BehaviorState::SLEEPING:
    case BehaviorState::IDLE:
      return;  // no Behavior-Engine-owned scheduler; IDLE's movement comes from ExpressiveMotion's own engine
    case BehaviorState::CURIOUS:
      updateScheduledMovement(nowMs, CURIOUS_PROFILE);
      return;
    case BehaviorState::LISTENING: {
      // Simple, bounded "activity awareness": a recent clap pulls the next
      // occasional nod in sooner (never fires instantly), rather than
      // reading the microphone or AudioAnalyzer independently.
      if (features.clap) {
        unsigned long nudged = nowMs + BEHAVIOR_LISTENING_CLAP_NUDGE_MS;
        if (!movementDeadlineSet || (long)(movementDeadlineMs - nudged) > 0) {
          movementDeadlineMs = nudged;
          movementDeadlineSet = true;
        }
      }
      updateScheduledMovement(nowMs, LISTENING_PROFILE);
      return;
    }
    case BehaviorState::THINKING:
      updateScheduledMovement(nowMs, THINKING_PROFILE);
      return;
    case BehaviorState::EXCITED:
      if (excitedEpisodeDeadlineSet && (long)(nowMs - excitedEpisodeDeadlineMs) >= 0) {
        Serial.println(F("[BEHAVIOR] Excited episode complete -- returning to IDLE"));
        transitionTo(BehaviorState::IDLE);
        return;
      }
      updateScheduledMovement(nowMs, EXCITED_PROFILE);
      return;
  }
}

bool setBehaviorState(BehaviorState newState) {
  // A direct state request always takes priority over a running demo --
  // otherwise the demo would silently override it at its next step
  // boundary, which would be confusing for both a human at the serial
  // console and a future Raspberry Pi caller.
  if (demoActive) {
    demoActive = false;
    Serial.println(F("[BEHAVIOR DEMO] Cancelled -- explicit state change requested"));
  }
  return transitionTo(newState);
}

BehaviorState getBehaviorState() { return state; }

void stopBehaviorEngine() {
  bool wasActive = (state != BehaviorState::MANUAL) || demoActive;
  demoActive = false;
  state = BehaviorState::MANUAL;
  stateEnteredMs = millis();
  resetMovementTimers();
  if (wasActive) Serial.println(F("[BEHAVIOR] Emergency stop -- MANUAL"));
}

bool startBehaviorDemo() {
  if (demoActive) return false;
  if (isAnyMotorDiagnosticActive()) {
    Serial.println(F("[BEHAVIOR DEMO] Refused -- a motor diagnostic is active"));
    return false;
  }
  demoActive = true;
  demoStepIndex = 0;
  Serial.println(F("[BEHAVIOR DEMO] Start"));
  Serial.printf("[BEHAVIOR DEMO] State: %s\n", behaviorStateName(DEMO_SEQUENCE[0].state));
  transitionTo(DEMO_SEQUENCE[0].state);
  demoStepStartMs = millis();
  return true;
}

bool isBehaviorDemoActive() { return demoActive; }

void printBehaviorStatus() {
  unsigned long now = millis();
  unsigned long timeInStateMs = now - stateEnteredMs;
  long excitedRemainingMs = -1;
  if (state == BehaviorState::EXCITED && excitedEpisodeDeadlineSet) {
    excitedRemainingMs = (long)(excitedEpisodeDeadlineMs - now);
    if (excitedRemainingMs < 0) excitedRemainingMs = 0;
  }
  Serial.printf(
      "[BEHAVIOR] state=%s timeInStateMs=%lu movementOwned=%d expressiveMode=%d expressiveMoving=%d "
      "lastRequestedPattern=%d excitedRemainingMs=%ld diagnosticActive=%d demoActive=%d overlayEnabled=%d "
      "powerGuardState=%d\n",
      behaviorStateName(state), (unsigned long)timeInStateMs, isMovementProducing(state) ? 1 : 0,
      (int)getExpressiveMotionMode(), isExpressiveMotionMoving() ? 1 : 0, (int)lastRequestedPattern,
      excitedRemainingMs, isAnyMotorDiagnosticActive() ? 1 : 0, demoActive ? 1 : 0, isAudioOverlayEnabled() ? 1 : 0,
      (int)getMotorPowerGuardState());
}
