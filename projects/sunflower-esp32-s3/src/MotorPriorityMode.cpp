#include "MotorPriorityMode.h"

#include <Arduino.h>

#include "MotorDriver.h"
#include "MotorPowerGuard.h"

#if ENABLE_MOTOR_PRIORITY_MODE

namespace {

constexpr uint32_t PREPARE_DELAY_MS = 150;
constexpr uint32_t RELEASE_DELAY_MS = 100;

MotorPriorityState state = MotorPriorityState::IDLE;
unsigned long phaseStartMs = 0;
bool audioSuspended = false;

const char *stateName(MotorPriorityState s) {
  switch (s) {
    case MotorPriorityState::IDLE: return "IDLE";
    case MotorPriorityState::PREPARING: return "PREPARING";
    case MotorPriorityState::READY: return "READY";
    case MotorPriorityState::RELEASING: return "RELEASING";
  }
  return "UNKNOWN";
}

}  // namespace

void initMotorPriorityMode() {
  state = MotorPriorityState::IDLE;
  phaseStartMs = 0;
  audioSuspended = false;
}

void requestMotorPriority() {
  if (state == MotorPriorityState::PREPARING || state == MotorPriorityState::READY) return;
  requestMotorPower();  // delegates LED-mute save/force to MotorPowerGuard
  audioSuspended = true;
  state = MotorPriorityState::PREPARING;
  phaseStartMs = millis();
}

bool isMotorPriorityReady() { return state == MotorPriorityState::READY; }

void releaseMotorPriority() {
  if (state == MotorPriorityState::IDLE) return;
  motorStop();  // ensure the motor is stopped before the settle wait even starts
  state = MotorPriorityState::RELEASING;
  phaseStartMs = millis();
}

void releaseMotorPriorityImmediately() {
  if (state == MotorPriorityState::IDLE) return;
  motorStop();
  audioSuspended = false;
  releaseMotorPowerImmediately();
  state = MotorPriorityState::IDLE;
}

void updateMotorPriorityMode() {
  unsigned long now = millis();
  switch (state) {
    case MotorPriorityState::IDLE:
      return;
    case MotorPriorityState::PREPARING:
      if ((now - phaseStartMs >= PREPARE_DELAY_MS) && isMotorPowerReady()) {
        state = MotorPriorityState::READY;
      }
      return;
    case MotorPriorityState::READY:
      return;  // waiting for the caller to drive the motor, then call releaseMotorPriority()
    case MotorPriorityState::RELEASING:
      // Own 100ms settle already elapsed here, so bypass MotorPowerGuard's
      // own 100ms release delay (releaseMotorPowerImmediately(), not
      // releaseMotorPower()) -- otherwise LED restoration would be
      // delayed by 100+100=200ms instead of the intended 100ms.
      if (now - phaseStartMs >= RELEASE_DELAY_MS) {
        audioSuspended = false;
        releaseMotorPowerImmediately();
        state = MotorPriorityState::IDLE;
      }
      return;
  }
}

bool isMotorPriorityActive() { return state != MotorPriorityState::IDLE; }
MotorPriorityState getMotorPriorityState() { return state; }
bool isLedRenderingSuspended() { return state != MotorPriorityState::IDLE; }
bool isAudioProcessingSuspended() { return audioSuspended; }

void printMotorPriorityDebugState() {
  Serial.printf("[MOTOR PRIORITY] enabled=%d state=%s ledSuspended=%d audioSuspended=%d\n",
                ENABLE_MOTOR_PRIORITY_MODE, stateName(state), isLedRenderingSuspended() ? 1 : 0,
                isAudioProcessingSuspended() ? 1 : 0);
}

#else

void initMotorPriorityMode() {}
void requestMotorPriority() {}
bool isMotorPriorityReady() { return true; }
void releaseMotorPriority() {}
void releaseMotorPriorityImmediately() {}
void updateMotorPriorityMode() {}
bool isMotorPriorityActive() { return false; }
MotorPriorityState getMotorPriorityState() { return MotorPriorityState::IDLE; }
bool isLedRenderingSuspended() { return false; }
bool isAudioProcessingSuspended() { return false; }
void printMotorPriorityDebugState() {
  Serial.println(F("[MOTOR PRIORITY] enabled=0"));
}

#endif
