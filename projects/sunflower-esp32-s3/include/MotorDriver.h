#pragma once
// DRV8833 motor driver control -- digitalWrite only, no PWM yet.
//
// Verified wiring (J2-bridged DRV8833 board -- see project history/commit
// "Preserve final DRV8833 motor electrical diagnostic before removal" for
// the validation work behind this configuration):
//   ESP32 GPIO8 -> DRV8833 IN1
//   ESP32 GPIO9 -> DRV8833 IN2
//   ESP32 3.3V  -> DRV8833 VCC
//   ESP32 GND   -> DRV8833 GND
//   Motor       -> DRV8833 OUT1/OUT2
//
// Note: the ESP32's 3.3V rail is a logic supply, not a motor supply -- it
// has limited current headroom and motor inrush can sag it toward the
// DRV8833's undervoltage lockout threshold. This wiring is validated
// working for bring-up, but an adequate external motor supply with common
// ground is recommended before sustained/loaded use.
#include <stdint.h>

// Configures GPIO8/GPIO9 as outputs and leaves the motor stopped
// (IN1=LOW, IN2=LOW). Call once during setup(), before any other motor
// function.
void initMotor();

// Drives the motor forward (IN1=HIGH, IN2=LOW) until told otherwise.
void motorForward();

// Drives the motor in reverse (IN1=LOW, IN2=HIGH) until told otherwise.
void motorReverse();

// Coasts the motor to a stop (IN1=LOW, IN2=LOW).
void motorStop();

// Brakes the motor (IN1=HIGH, IN2=HIGH) -- the DRV8833's documented brake
// mode, both outputs driven to the same rail. Electrically validated
// during bring-up; not the same as a mechanical stall.
void motorBrake();

// Blocking helper: drives forward for `ms` milliseconds, then stops.
void motorForwardMs(uint32_t ms);

// Blocking helper: drives reverse for `ms` milliseconds, then stops.
void motorReverseMs(uint32_t ms);
