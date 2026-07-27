#pragma once
// Temporary bench-diagnostic coordination layer: recreates the quiet
// system state present during the boot-time motor verification (LED
// rendering suspended, microphone/audio processing suspended) so runtime
// motor commands can be tested under boot-equivalent conditions. See
// docs/DRV8833_MOTOR_BRINGUP.md for why this was needed -- IDLE_SWAY at
// 300/500ms buzzed without moving at runtime despite moving cleanly
// during boot, and the two differ mainly in what else the system is
// doing concurrently.
//
// Delegates ALL LED-mute save/force/restore to MotorPowerGuard (see
// include/MotorPowerGuard.h) rather than duplicating that state --
// MotorPriorityMode only adds audio-processing suspension and a longer,
// independent 150ms settle wait on top. MotorDriver remains the only
// module that touches GPIO8/GPIO9; this file never does.
#define ENABLE_MOTOR_PRIORITY_MODE 1

enum class MotorPriorityState {
  IDLE,       // not requested; LEDs/audio run normally
  PREPARING,  // LEDs forced muted, audio suspended, waiting out the 150ms settle delay
  READY,      // settle delay elapsed -- motor may energize
  RELEASING,  // motor stopped, waiting out the 100ms settle delay before resuming audio + restoring LEDs
};

void initMotorPriorityMode();

// Delegates to MotorPowerGuard's requestMotorPower() (preserves + forces
// LED mute) and additionally suspends audio processing, then starts a
// 150ms settle wait. Safe to call repeatedly while already
// PREPARING/READY -- does not restart the timer or re-request.
void requestMotorPriority();

// True once the 150ms settle delay has elapsed AND MotorPowerGuard
// reports ready. The motor must never energize before this returns true.
bool isMotorPriorityReady();

// Ensures the motor is stopped, then starts a 100ms settle wait; once it
// elapses, resumes audio processing and restores LEDs (via
// MotorPowerGuard::releaseMotorPowerImmediately() -- not
// releaseMotorPower(), so the wait isn't doubled).
void releaseMotorPriority();

// Ensures the motor is stopped and restores everything (audio + LEDs)
// immediately, bypassing the 100ms delay. Used for emergency stop.
void releaseMotorPriorityImmediately();

// Advances the internal timer. Must be called every loop() iteration.
void updateMotorPriorityMode();

// True whenever priority mode is not IDLE.
bool isMotorPriorityActive();

MotorPriorityState getMotorPriorityState();
bool isLedRenderingSuspended();
bool isAudioProcessingSuspended();

// For the '?' status command.
void printMotorPriorityDebugState();
