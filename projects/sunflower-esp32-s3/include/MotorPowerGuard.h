#pragma once
#include <stdint.h>
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

// ----------------------------------------------------------------------------
// Experimental: motor+LED coexistence testing (see
// docs/DRV8833_MOTOR_BRINGUP.md, "42-LED assembly" section). FULL_MUTE
// above is the existing, default, production-safe behavior and is
// completely unchanged by this addition -- '2'/'3' never touch this and
// always get FULL_MUTE. DIM_DURING_MOTION is a new, explicitly
// experimental alternative used only by the dedicated motor+LED test
// command: instead of muting, it keeps the currently-selected base effect
// running (unreplaced, unpaused) but ramps overall brightness down to a
// low test level for the duration of motor engagement, and suppresses the
// audio overlay (the highest-current, least predictable rendering
// component) meanwhile.
// ----------------------------------------------------------------------------
enum class MotorLedPowerMode {
  FULL_MUTE,          // existing/default: LEDs fully muted during motor engagement
  DIM_DURING_MOTION,  // experimental: LEDs stay on, ramped down to a low test brightness
};

// Selects which mode requestMotorPower() uses on its next call. Only takes
// effect while IDLE (no-op mid-cycle, so a running request/release can't
// change mode underneath itself). Defaults to FULL_MUTE.
void setMotorLedPowerMode(MotorLedPowerMode mode);
MotorLedPowerMode getMotorLedPowerMode();

// True whenever DIM_DURING_MOTION is the active mode and a request is not
// IDLE. main.cpp's render loop consults this to substitute
// getMotorDimBrightnessRaw() for Controls.cpp's getBrightnessRaw(), and to
// suspend the audio overlay -- the base effect itself keeps rendering
// normally either way.
bool isDimRenderActive();

// Current ramped brightness override (0-255), meaningful only while
// isDimRenderActive() is true. Ramps from the brightness in effect at
// requestMotorPower() time down to the selected test level (see
// cycleMotorDimTestLevel()), and back up symmetrically after release.
uint8_t getMotorDimBrightnessRaw();

// Cycles the DIM_DURING_MOTION test brightness through a small fixed set
// {0, 4, 8, 12, 16}, printing the new selection. Deliberately capped at
// 16/255 for the initial motor coexistence diagnostic -- do not raise this
// range until physical testing at these levels has been reviewed.
void cycleMotorDimTestLevel();
uint8_t getMotorDimTestLevel();

// For the '?' status command.
void printMotorLedPowerDebugState();
