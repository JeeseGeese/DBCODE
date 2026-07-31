#pragma once
// Temporary, non-blocking PWM calibration test for the DRV8833 motor -- see
// docs/DRV8833_MOTOR_BRINGUP.md. Built strictly on top of MotorDriver's
// motorPWM*()/initMotorPWM()/deinitMotorPWM() (see include/MotorDriver.h) --
// this module never touches GPIO8/GPIO9 itself. Extends the existing
// MotorDriver/MotorBehavior architecture rather than replacing it: while
// active, it takes exclusive motor ownership the same way the other
// temporary diagnostics in main.cpp do (setMotorBehavior(OFF), mutual
// exclusion with those diagnostics and with expressive motion), and returns
// ownership cleanly (MotorBehavior left at OFF, PWM detached) when the test
// ends or is cancelled.
//
// Goal: determine, by hand, on the installed motor:
//   1. lowest duty that starts it reliably
//   2. lowest duty that keeps it moving once started
//   3. which duties give useful slow/medium/fast/max "dancing"
//   4. whether forward and reverse behave similarly
//   5. whether ramped acceleration looks better than instant speed changes
//
// No delay() anywhere in this module -- updateMotorPwmCalibration() must be
// called every loop() iteration and drives a millis()-based state machine,
// internally rate-limited to update the physical duty roughly every 10-20ms
// (see MOTOR_CAL_TICK_MS in the .cpp).

#include <stdint.h>

enum class MotorCalDirection { FORWARD, REVERSE };
enum class MotorCalRoutine { NONE, MANUAL, RAMP, CYCLE };

// Resets all state (test inactive, direction=FORWARD selected, startup kick
// enabled). Does not touch GPIO8/GPIO9 -- PWM is only attached lazily, the
// first time a test command actually runs. Call once during setup(), after
// initMotor().
void initMotorPwmCalibration();

// Advances any in-progress kick/ramp/hold/coast/routine timing. Non-blocking;
// no-op when no test is active. Must be called every loop() iteration.
void updateMotorPwmCalibration();

// True whenever this module currently owns the motor (a manual hold, mramp,
// or mcycle is active or in a transitional phase). Consulted by main.cpp's
// isAnyMotorDiagnosticActive() and by the other motor/LED diagnostics' own
// start guards, so nothing else can drive the motor concurrently.
bool isMotorPwmCalibrationActive();

// --- Serial-facing commands (see Controls.cpp's dispatchCommand()) ---

// 'mf'/'mr': selects forward/reverse. Takes exclusive motor ownership
// immediately (refused if another motor diagnostic or expressive motion is
// active). If a manual speed is already running, immediately re-drives at
// the same requested percentage in the new direction (through the safe
// reversal sequence below) -- otherwise just records the selection for the
// next manual speed command.
void motorCalSelectDirection(MotorCalDirection direction);

// 'm20'..'m100' (parsed as "m" + 1-100 in Controls.cpp): runs the currently
// selected direction continuously at `percent` until another motor command,
// mstop, or emergency stop. Applies the startup kick (if enabled) when
// starting from a dead stop below 70%.
void motorCalManualSpeed(uint8_t percent);

// 'mstop': coasts the motor immediately (GPIO8/GPIO9 -> LOW) and cancels
// every pending ramp/kick/hold/reversal and any running routine, releasing
// motor ownership back to MotorBehavior (left at OFF).
void motorCalStop();

// Called from main.cpp's serviceEmergencyStop() for 'k' -- identical effect
// to motorCalStop() but silent (the emergency-stop path prints its own
// messages) and always safe to call even when no test is active (no-op).
void motorCalEmergencyCancel();

// 'mramp': non-blocking automatic PWM ramp test -- forward 20%..100% in
// 10% steps (2s each), coast 500ms, then the same 20%..100% progression in
// reverse, then stop. See src/MotorPwmCalibration.cpp for the exact table.
void motorCalStartRamp();

// 'mcycle': non-blocking automatic "dance" test exercising slow/medium/fast
// speeds, sustained rotation, a quick safe reversal, and ramped
// acceleration/deceleration in both directions. See src/MotorPwmCalibration.cpp
// for the exact 13-step table.
void motorCalStartCycle();

// 'mkick': toggles the startup-kick feature on/off, so kick-enabled and
// kick-disabled behavior can be compared directly.
void motorCalToggleKick();

// 'mstatus': prints full status -- see this module's .cpp for the exact
// field list (matches the SERIAL COMMANDS / STATUS section of the
// calibration-test spec: active/routine/direction/current+target duty,
// kick enabled+active, routine step + remaining time, pending reversal,
// emergency-stop state, GPIO8/GPIO9 commanded state).
void motorCalPrintStatus();

// Configured LEDC frequency/resolution, for the boot/help/build report.
constexpr uint32_t MOTOR_CAL_PWM_FREQUENCY_HZ = 19000;
constexpr uint8_t MOTOR_CAL_PWM_RESOLUTION_BITS = 8;
