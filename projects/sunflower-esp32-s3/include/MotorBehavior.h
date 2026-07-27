#pragma once
// Non-blocking motor behavior layer, built strictly on top of MotorDriver.
// MotorDriver remains the only module allowed to touch GPIO8/GPIO9 --
// everything here calls MotorDriver functions only (motorForward/
// motorReverse/motorStop/motorBrake). No delay() is used anywhere in this
// module: updateMotorBehavior() must be called every loop() iteration and
// drives a millis()-based state machine.
//
// See docs/DRV8833_MOTOR_BRINGUP.md section 13 for the development plan
// this layer is part of.

enum class MotorBehaviorMode {
  OFF,
  IDLE_SWAY,
  GENTLE_NOD,
  DANCE_BASIC,
};

// Resets to MotorBehaviorMode::OFF (motor stopped). Call once during
// setup(), after initMotor(). Does not enable any behavior automatically.
void initMotorBehavior();

// Switches to `mode`. Always stops the motor first -- regardless of the
// current or new mode -- before anything else happens, so no mode change
// can leave a previous behavior's drive state active. GENTLE_NOD and
// DANCE_BASIC are currently safe placeholders: selecting them keeps the
// motor stopped and prints a one-time "not implemented" message.
void setMotorBehavior(MotorBehaviorMode mode);

MotorBehaviorMode getMotorBehavior();

// Advances the current behavior's state machine by however much time has
// passed. Must be called repeatedly from loop(); never blocks. No-op when
// OFF (or on an unimplemented placeholder mode).
void updateMotorBehavior();

// Immediate stop: halts the motor and forces mode back to OFF. Safe to
// call at any time, from any mode.
void stopMotorBehavior();

// Prints the current mode and, where applicable, the active behavior's
// internal phase/timing (e.g. which IDLE_SWAY segment is running and for
// how long). Used by the temporary serial test interface's '?' command.
void printMotorBehaviorDebugState();
