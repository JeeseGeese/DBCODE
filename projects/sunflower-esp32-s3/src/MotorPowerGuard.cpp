#include "MotorPowerGuard.h"

#include <Arduino.h>

#include "Controls.h"

namespace {

constexpr uint32_t PREPARE_DELAY_MS = 50;
constexpr uint32_t RELEASE_DELAY_MS = 100;

#if ENABLE_MOTOR_LED_POWER_GUARD

MotorPowerGuardState state = MotorPowerGuardState::IDLE;
unsigned long phaseStartMs = 0;

// stateSaved guards against repeated requestMotorPower()/releaseMotorPower()
// cycles overwriting the originally-captured mute state -- savedMuted is
// only ever written while !stateSaved, and only read/cleared on restore.
bool stateSaved = false;
bool savedMuted = false;

void doRestore() {
  if (stateSaved) {
    setMuted(savedMuted);
    stateSaved = false;
  }
  state = MotorPowerGuardState::IDLE;
}

#endif

}  // namespace

void initMotorPowerGuard() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  state = MotorPowerGuardState::IDLE;
  phaseStartMs = 0;
  stateSaved = false;
  savedMuted = false;
#endif
}

void requestMotorPower() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  // Already prepared/preparing for this cycle -- don't re-save or restart
  // the timer. This is what makes repeated calls safe.
  if (state == MotorPowerGuardState::PREPARING || state == MotorPowerGuardState::READY) return;

  if (!stateSaved) {
    savedMuted = isMuted();
    stateSaved = true;
  }
  if (!isMuted()) setMuted(true);

  state = MotorPowerGuardState::PREPARING;
  phaseStartMs = millis();
#endif
}

bool isMotorPowerReady() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return state == MotorPowerGuardState::READY;
#else
  return true;
#endif
}

void releaseMotorPower() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  if (state == MotorPowerGuardState::IDLE) return;  // nothing outstanding to release
  state = MotorPowerGuardState::RELEASING;
  phaseStartMs = millis();
#endif
}

void releaseMotorPowerImmediately() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  if (state == MotorPowerGuardState::IDLE) return;
  doRestore();
#endif
}

void updateMotorPowerGuard() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  unsigned long now = millis();
  switch (state) {
    case MotorPowerGuardState::IDLE:
      return;
    case MotorPowerGuardState::PREPARING:
      if (now - phaseStartMs >= PREPARE_DELAY_MS) state = MotorPowerGuardState::READY;
      return;
    case MotorPowerGuardState::READY:
      return;  // waiting for the caller to drive the motor, then call releaseMotorPower()
    case MotorPowerGuardState::RELEASING:
      if (now - phaseStartMs >= RELEASE_DELAY_MS) doRestore();
      return;
  }
#endif
}

MotorPowerGuardState getMotorPowerGuardState() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return state;
#else
  return MotorPowerGuardState::IDLE;
#endif
}

bool isPreviousLedStateSaved() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return stateSaved;
#else
  return false;
#endif
}

bool wasLedManuallyMutedBeforeRequest() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return stateSaved && savedMuted;
#else
  return false;
#endif
}
