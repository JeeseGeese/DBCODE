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

// ----------------------------------------------------------------------------
// PWM support -- temporary, for the manual PWM calibration test (see
// include/MotorPwmCalibration.h). Uses the installed ESP32 Arduino core's
// LEDC API (channel-based: ledcSetup/ledcAttachPin/ledcWrite(channel, ...) --
// verified against the actual installed framework-arduinoespressif32
// package, not assumed from a newer or older core version). GPIO8/GPIO9
// remain the only pins touched; motorPWM*() below simply attaches them to
// LEDC channels instead of driving them with digitalWrite.
//
// While PWM is active (between initMotorPWM() and deinitMotorPWM()), the
// plain digitalWrite-based functions above (motorForward/motorReverse/
// motorStop/motorBrake) must not be relied on -- the pins are routed through
// the LEDC peripheral, not GPIO output registers. deinitMotorPWM() detaches
// LEDC and restores GPIO8/GPIO9 to plain digital outputs driven LOW (the
// same state motorStop()/initMotor() leave them in), so ordinary MotorDriver
// control resumes working normally once it returns.
// ----------------------------------------------------------------------------

// Configures LEDC channels for GPIO8 (IN1) and GPIO9 (IN2) at `freqHz`/
// `resolutionBits` and attaches both pins to them, duty initially 0 (LOW).
// Safe to call repeatedly -- a no-op while already active. Returns the
// actual LEDC frequency the hardware was able to configure (may differ
// slightly from `freqHz` depending on resolution/clock divisors).
uint32_t initMotorPWM(uint32_t freqHz, uint8_t resolutionBits);

// Detaches GPIO8/GPIO9 from LEDC and returns them to plain digital outputs,
// driven LOW -- equivalent to motorStop(), and required before any
// digitalWrite-based MotorDriver function is used again. Safe to call
// repeatedly -- a no-op while already inactive.
void deinitMotorPWM();

// True between initMotorPWM() and deinitMotorPWM().
bool isMotorPWMActive();

// Forward at PWM `duty` (0-2^resolutionBits-1, e.g. 0-255 at 8-bit): IN1=PWM,
// IN2=0 (LOW). Requires initMotorPWM() to have been called first.
void motorPWMForward(uint8_t duty);

// Reverse at PWM `duty`: IN1=0 (LOW), IN2=PWM. Requires initMotorPWM().
void motorPWMReverse(uint8_t duty);

// Coasts: both LEDC channels driven to 0 duty (electrically equivalent to
// motorStop()). Requires initMotorPWM().
void motorPWMCoast();

// The actual LEDC frequency configured by the most recent initMotorPWM()
// call (0 if PWM has never been initialized).
uint32_t getMotorPWMFrequency();
