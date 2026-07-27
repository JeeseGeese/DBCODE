#include "MotorBehavior.h"

#include <Arduino.h>

#include "MotorDriver.h"
#include "MotorPowerGuard.h"

namespace {

MotorBehaviorMode currentMode = MotorBehaviorMode::OFF;

// IDLE_SWAY timing -- calibration in progress, physical testing ongoing.
// See docs/DRV8833_MOTOR_BRINGUP.md section 16: 120ms buzzed without
// motion; 300ms was a partial pass (~80% start reliability, occasional
// buzz-without-turn requiring a manual nudge). 500ms is the next and
// intended final pulse-duration calibration value before classifying any
// remaining unreliability as a motor-power/mechanical-starting limitation
// rather than a timing one. Named constants so future calibration passes
// only touch this block.
constexpr uint32_t IDLE_SWAY_FORWARD_MS = 500;
constexpr uint32_t IDLE_SWAY_FORWARD_REST_MS = 700;
constexpr uint32_t IDLE_SWAY_REVERSE_MS = 500;
constexpr uint32_t IDLE_SWAY_REVERSE_REST_MS = 1200;

// REQUEST_POWER_* phases wait on MotorPowerGuard's isMotorPowerReady()
// before energizing -- their own wait time (up to the guard's 50ms
// prepare delay) is NOT counted as part of the FORWARD/REVERSE pulse,
// since phaseStartMs is reset fresh when each of those phases is entered.
enum class IdleSwayPhase {
  REQUEST_POWER_FORWARD,
  FORWARD,
  STOP_AFTER_FORWARD,
  REQUEST_POWER_REVERSE,
  REVERSE,
  STOP_AFTER_REVERSE,
};

IdleSwayPhase idleSwayPhase = IdleSwayPhase::REQUEST_POWER_FORWARD;
unsigned long phaseStartMs = 0;

// Generic safety net, independent of any individual behavior's own state
// machine: no single energized (forward/reverse) segment may run longer
// than this, no matter which behavior is active or what bug might exist
// in its timing logic. 0 means "not currently energized". IDLE_SWAY's own
// phase durations (all <=1200ms, energized segments <=IDLE_SWAY_FORWARD_MS/
// IDLE_SWAY_REVERSE_MS) should never actually trigger this -- it exists
// for future behaviors too.
constexpr uint32_t MAX_ENERGIZED_MS = 2000;
unsigned long energizedSinceMs = 0;

void behaviorForward() {
  motorForward();
  energizedSinceMs = millis();
}

void behaviorReverse() {
  motorReverse();
  energizedSinceMs = millis();
}

void behaviorStop() {
  motorStop();
  energizedSinceMs = 0;
}

void enforceMaxRuntime(unsigned long now) {
  if (energizedSinceMs != 0 && (now - energizedSinceMs) >= MAX_ENERGIZED_MS) {
    Serial.println(F("[MOTOR BEHAVIOR] Safety: max energized runtime exceeded -- forcing stop"));
    behaviorStop();
    releaseMotorPowerImmediately();
  }
}

// Kicks off a fresh IDLE_SWAY cycle at its very first step: request power,
// then wait for isMotorPowerReady() before driving forward.
void enterIdleSway(unsigned long now) {
  idleSwayPhase = IdleSwayPhase::REQUEST_POWER_FORWARD;
  phaseStartMs = now;
  requestMotorPower();
}

// STOP_AFTER_FORWARD and STOP_AFTER_REVERSE are separate phases (not one
// shared "stopped" phase) so forward and reverse can never become directly
// adjacent -- each direction change always passes through its own bounded
// stop segment, plus a REQUEST_POWER_* phase, before the next direction.
void updateIdleSway(unsigned long now) {
  unsigned long elapsed = now - phaseStartMs;
  switch (idleSwayPhase) {
    case IdleSwayPhase::REQUEST_POWER_FORWARD:
      if (isMotorPowerReady()) {
        behaviorForward();
        idleSwayPhase = IdleSwayPhase::FORWARD;
        phaseStartMs = now;
      }
      break;
    case IdleSwayPhase::FORWARD:
      if (elapsed >= IDLE_SWAY_FORWARD_MS) {
        behaviorStop();
        releaseMotorPower();
        idleSwayPhase = IdleSwayPhase::STOP_AFTER_FORWARD;
        phaseStartMs = now;
      }
      break;
    case IdleSwayPhase::STOP_AFTER_FORWARD:
      if (elapsed >= IDLE_SWAY_FORWARD_REST_MS) {
        idleSwayPhase = IdleSwayPhase::REQUEST_POWER_REVERSE;
        phaseStartMs = now;
        requestMotorPower();
      }
      break;
    case IdleSwayPhase::REQUEST_POWER_REVERSE:
      if (isMotorPowerReady()) {
        behaviorReverse();
        idleSwayPhase = IdleSwayPhase::REVERSE;
        phaseStartMs = now;
      }
      break;
    case IdleSwayPhase::REVERSE:
      if (elapsed >= IDLE_SWAY_REVERSE_MS) {
        behaviorStop();
        releaseMotorPower();
        idleSwayPhase = IdleSwayPhase::STOP_AFTER_REVERSE;
        phaseStartMs = now;
      }
      break;
    case IdleSwayPhase::STOP_AFTER_REVERSE:
      if (elapsed >= IDLE_SWAY_REVERSE_REST_MS) {
        idleSwayPhase = IdleSwayPhase::REQUEST_POWER_FORWARD;
        phaseStartMs = now;
        requestMotorPower();
      }
      break;
  }
}

bool gentleNodNoticePrinted = false;
bool danceBasicNoticePrinted = false;

const char *modeName(MotorBehaviorMode mode) {
  switch (mode) {
    case MotorBehaviorMode::OFF: return "OFF";
    case MotorBehaviorMode::IDLE_SWAY: return "IDLE_SWAY";
    case MotorBehaviorMode::GENTLE_NOD: return "GENTLE_NOD";
    case MotorBehaviorMode::DANCE_BASIC: return "DANCE_BASIC";
  }
  return "UNKNOWN";
}

const char *idleSwayPhaseName(IdleSwayPhase phase) {
  switch (phase) {
    case IdleSwayPhase::REQUEST_POWER_FORWARD: return "REQUEST_POWER_FORWARD";
    case IdleSwayPhase::FORWARD: return "FORWARD";
    case IdleSwayPhase::STOP_AFTER_FORWARD: return "STOP_AFTER_FORWARD";
    case IdleSwayPhase::REQUEST_POWER_REVERSE: return "REQUEST_POWER_REVERSE";
    case IdleSwayPhase::REVERSE: return "REVERSE";
    case IdleSwayPhase::STOP_AFTER_REVERSE: return "STOP_AFTER_REVERSE";
  }
  return "UNKNOWN";
}

const char *powerGuardStateName(MotorPowerGuardState s) {
  switch (s) {
    case MotorPowerGuardState::IDLE: return "IDLE";
    case MotorPowerGuardState::PREPARING: return "PREPARING";
    case MotorPowerGuardState::READY: return "READY";
    case MotorPowerGuardState::RELEASING: return "RELEASING";
  }
  return "UNKNOWN";
}

}  // namespace

void initMotorBehavior() {
  currentMode = MotorBehaviorMode::OFF;
  idleSwayPhase = IdleSwayPhase::REQUEST_POWER_FORWARD;
  phaseStartMs = 0;
  behaviorStop();
  gentleNodNoticePrinted = false;
  danceBasicNoticePrinted = false;
}

void setMotorBehavior(MotorBehaviorMode mode) {
  // Always stop first, regardless of current or new mode, and release the
  // power guard immediately (not the normal 100ms-delayed path) so a mode
  // change can never leave LEDs stuck muted.
  behaviorStop();
  releaseMotorPowerImmediately();
  currentMode = mode;

  switch (mode) {
    case MotorBehaviorMode::OFF:
      break;
    case MotorBehaviorMode::IDLE_SWAY:
      enterIdleSway(millis());
      break;
    case MotorBehaviorMode::GENTLE_NOD:
      if (!gentleNodNoticePrinted) {
        Serial.println(F("[MOTOR BEHAVIOR] GENTLE_NOD not implemented -- motor remains stopped"));
        gentleNodNoticePrinted = true;
      }
      break;
    case MotorBehaviorMode::DANCE_BASIC:
      if (!danceBasicNoticePrinted) {
        Serial.println(F("[MOTOR BEHAVIOR] DANCE_BASIC not implemented -- motor remains stopped"));
        danceBasicNoticePrinted = true;
      }
      break;
  }
}

MotorBehaviorMode getMotorBehavior() { return currentMode; }

void updateMotorBehavior() {
  unsigned long now = millis();
  enforceMaxRuntime(now);

  switch (currentMode) {
    case MotorBehaviorMode::OFF:
    case MotorBehaviorMode::GENTLE_NOD:
    case MotorBehaviorMode::DANCE_BASIC:
      return;  // motor stays stopped; nothing to advance
    case MotorBehaviorMode::IDLE_SWAY:
      updateIdleSway(now);
      return;
  }
}

void stopMotorBehavior() {
  behaviorStop();
  releaseMotorPowerImmediately();
  currentMode = MotorBehaviorMode::OFF;
}

void printMotorBehaviorDebugState() {
  Serial.printf("[MOTOR BEHAVIOR] State: mode=%s", modeName(currentMode));
  if (currentMode == MotorBehaviorMode::IDLE_SWAY) {
    Serial.printf(" phase=%s elapsedMs=%lu", idleSwayPhaseName(idleSwayPhase),
                  (unsigned long)(millis() - phaseStartMs));
  }
  Serial.println();
  Serial.printf("[MOTOR BEHAVIOR] PowerGuard: enabled=%d state=%s savedLedState=%d ledWasManuallyMuted=%d\n",
                ENABLE_MOTOR_LED_POWER_GUARD, powerGuardStateName(getMotorPowerGuardState()),
                isPreviousLedStateSaved() ? 1 : 0, wasLedManuallyMutedBeforeRequest() ? 1 : 0);
}
