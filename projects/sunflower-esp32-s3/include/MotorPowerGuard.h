#pragma once
// Temporary bench-development workaround, NOT a production fix: the
// DRV8833 currently shares the ESP32's power supply with the LED strip
// (see docs/DRV8833_MOTOR_BRINGUP.md), and motor engagement visibly
// disturbs LED output. This module coordinates the two by muting LEDs
// immediately before the motor engages and restoring them shortly after
// it stops, so software development on both can continue in the meantime.
// The real fix is a dedicated external motor supply with common ground.
//
// Uses only Controls.h's central isMuted()/setMuted() LED-mute API --
// never touches the NeoPixel object, brightness, base effect, or
// audio-overlay state directly, and never duplicates LED-control logic.
//
// Set to 0 to disable entirely: isMotorPowerReady() then always returns
// true immediately and no LED muting/restoring happens.
#define ENABLE_MOTOR_LED_POWER_GUARD 1

enum class MotorPowerGuardState {
  IDLE,       // no motor power requested; LEDs untouched by this module
  PREPARING,  // LED state saved + muted, waiting out the 50ms settle delay
  READY,      // 50ms elapsed -- the motor may energize
  RELEASING,  // motor stopped, waiting out the 100ms settle delay before restoring LEDs
};

// Resets to IDLE with nothing saved. Call once during setup().
void initMotorPowerGuard();

// Saves the current LED mute state (once per request cycle -- repeated
// calls while already PREPARING/READY are a no-op, so they can never
// overwrite the originally saved state) and mutes the LEDs if not already
// muted, then starts the 50ms settle delay.
void requestMotorPower();

// True once the 50ms preparation delay has elapsed. The motor must never
// energize before this returns true.
bool isMotorPowerReady();

// Call once the motor has been commanded to stop. Starts the 100ms settle
// delay; the previously-saved LED state is restored once it elapses (via
// updateMotorPowerGuard()).
void releaseMotorPower();

// Bypasses the 100ms delay and restores the LED state immediately. Used
// for emergency stop and MotorBehavior mode changes, where leaving LEDs
// muted for another 100ms is worse than skipping the settle delay.
void releaseMotorPowerImmediately();

// Advances the internal timer (PREPARING->READY, RELEASING->restore->IDLE).
// Must be called every loop() iteration, regardless of MotorBehavior mode.
void updateMotorPowerGuard();

MotorPowerGuardState getMotorPowerGuardState();

// For the '?' status command.
bool isPreviousLedStateSaved();
bool wasLedManuallyMutedBeforeRequest();
