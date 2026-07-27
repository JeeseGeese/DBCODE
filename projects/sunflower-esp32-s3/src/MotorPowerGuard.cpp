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

// --- Experimental DIM_DURING_MOTION support (see MotorPowerGuard.h) ---
constexpr uint32_t DIM_RAMP_MS = 300; // brightness ramp duration, each direction
const uint8_t DIM_TEST_LEVELS[] = {0, 4, 8, 12, 16};
constexpr uint8_t NUM_DIM_TEST_LEVELS = sizeof(DIM_TEST_LEVELS) / sizeof(DIM_TEST_LEVELS[0]);
uint8_t dimTestLevelIndex = 2; // 8/255, the initial motion-brightness target

MotorLedPowerMode ledPowerMode = MotorLedPowerMode::FULL_MUTE;
uint8_t savedBrightnessRaw = 0;
uint8_t dimCurrentRaw = 0;
unsigned long dimRampStartMs = 0;
bool dimRampingUp = false; // true once RELEASING has started ramping back toward savedBrightnessRaw

uint8_t lerp8(uint8_t from, uint8_t to, float t) {
  if (t <= 0.0f) return from;
  if (t >= 1.0f) return to;
  return (uint8_t)(from + ((int)to - (int)from) * t);
}

void doRestore() {
  if (stateSaved) {
    if (ledPowerMode == MotorLedPowerMode::FULL_MUTE) {
      setMuted(savedMuted);
    }
    // DIM_DURING_MOTION never touches Controls.cpp's mute/brightness state
    // -- there's nothing to restore there beyond deactivating the override
    // itself (state -> IDLE below), which main.cpp's render loop uses to
    // fall straight back to getBrightnessRaw().
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
  ledPowerMode = MotorLedPowerMode::FULL_MUTE;
  dimTestLevelIndex = 2;
  savedBrightnessRaw = 0;
  dimCurrentRaw = 0;
  dimRampStartMs = 0;
  dimRampingUp = false;
#endif
}

void requestMotorPower() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  // Already prepared/preparing for this cycle -- don't re-save or restart
  // the timer. This is what makes repeated calls safe.
  if (state == MotorPowerGuardState::PREPARING || state == MotorPowerGuardState::READY) return;

  if (ledPowerMode == MotorLedPowerMode::FULL_MUTE) {
    if (!stateSaved) {
      savedMuted = isMuted();
      stateSaved = true;
    }
    if (!isMuted()) setMuted(true);
  } else {
    if (!stateSaved) {
      savedBrightnessRaw = getBrightnessRaw();
      stateSaved = true;
    }
    dimCurrentRaw = savedBrightnessRaw;
    dimRampStartMs = millis();
    dimRampingUp = false;
  }

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
  dimRampingUp = false;  // the RELEASING case below starts the up-ramp itself, after RELEASE_DELAY_MS
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
      if (ledPowerMode == MotorLedPowerMode::DIM_DURING_MOTION) {
        float t = (float)(now - dimRampStartMs) / (float)DIM_RAMP_MS;
        dimCurrentRaw = lerp8(savedBrightnessRaw, DIM_TEST_LEVELS[dimTestLevelIndex], t);
        // Ready only once BOTH the existing 50ms settle delay and the
        // brightness ramp itself have completed -- never energize the
        // motor mid-ramp.
        if (now - phaseStartMs >= PREPARE_DELAY_MS && t >= 1.0f) state = MotorPowerGuardState::READY;
        return;
      }
      if (now - phaseStartMs >= PREPARE_DELAY_MS) state = MotorPowerGuardState::READY;
      return;
    case MotorPowerGuardState::READY:
      return;  // waiting for the caller to drive the motor, then call releaseMotorPower()
    case MotorPowerGuardState::RELEASING:
      if (ledPowerMode == MotorLedPowerMode::DIM_DURING_MOTION) {
        // Hold at the dim test level for the same RELEASE_DELAY_MS settle
        // used by FULL_MUTE, then ramp back up to the pre-motor brightness.
        if (now - phaseStartMs < RELEASE_DELAY_MS) return;
        if (!dimRampingUp) {
          dimRampingUp = true;
          dimRampStartMs = now;
        }
        float t = (float)(now - dimRampStartMs) / (float)DIM_RAMP_MS;
        dimCurrentRaw = lerp8(DIM_TEST_LEVELS[dimTestLevelIndex], savedBrightnessRaw, t);
        if (t >= 1.0f) doRestore();
        return;
      }
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

void setMotorLedPowerMode(MotorLedPowerMode mode) {
#if ENABLE_MOTOR_LED_POWER_GUARD
  if (state != MotorPowerGuardState::IDLE) return;  // don't swap modes mid-cycle
  ledPowerMode = mode;
#endif
}

MotorLedPowerMode getMotorLedPowerMode() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return ledPowerMode;
#else
  return MotorLedPowerMode::FULL_MUTE;
#endif
}

bool isDimRenderActive() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return ledPowerMode == MotorLedPowerMode::DIM_DURING_MOTION && state != MotorPowerGuardState::IDLE;
#else
  return false;
#endif
}

uint8_t getMotorDimBrightnessRaw() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return dimCurrentRaw;
#else
  return 255;
#endif
}

void cycleMotorDimTestLevel() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  dimTestLevelIndex = (dimTestLevelIndex + 1) % NUM_DIM_TEST_LEVELS;
  Serial.printf("[MOTOR LED POWER] Motion-test brightness level: %u/255\n", DIM_TEST_LEVELS[dimTestLevelIndex]);
#endif
}

uint8_t getMotorDimTestLevel() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  return DIM_TEST_LEVELS[dimTestLevelIndex];
#else
  return 0;
#endif
}

void printMotorLedPowerDebugState() {
#if ENABLE_MOTOR_LED_POWER_GUARD
  Serial.printf(
      "[MOTOR LED POWER] mode=%s dimRenderActive=%d dimCurrentRaw=%u testLevel=%u savedBrightnessRaw=%u\n",
      ledPowerMode == MotorLedPowerMode::FULL_MUTE ? "FULL_MUTE" : "DIM_DURING_MOTION", isDimRenderActive() ? 1 : 0,
      dimCurrentRaw, DIM_TEST_LEVELS[dimTestLevelIndex], savedBrightnessRaw);
#else
  Serial.println(F("[MOTOR LED POWER] enabled=0"));
#endif
}
