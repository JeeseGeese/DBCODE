#pragma once
// BEGIN DRV8833 MOTOR TEST
// Temporary, isolated DRV8833 motor-driver characterization test -- see
// src/MotorTest.cpp for what it does and why. Runs once at the end of
// setup(), after the existing HardwareTest/MicRetest sequences, before the
// normal loop() begins. Drives GPIO8 (DRV8833 IN1) and GPIO9 (DRV8833 IN2)
// directly via LEDC PWM; does not touch LED, microphone, or button code or
// pins.
//
// Confirmed wiring for this test (Board B): DRV8833 VCC->ESP32 3.3V,
// DRV8833 GND->common GND, IN1->GPIO8, IN2->GPIO9, motor->OUT1/OUT2. No
// ULT/SLEEP pin connection is confirmed or assumed -- see the top of
// src/MotorTest.cpp for why, and ask for a board photo before wiring
// ULT/SLEEP on any board if that ever becomes necessary.
//
// To disable without deleting anything: flip this to 0 and rebuild --
// motor pins are still initialized LOW (for safety) but runMotorTest()
// becomes a no-op.
#define ENABLE_MOTOR_TEST 1

// To remove entirely: delete this file, src/MotorTest.cpp, and the
// init/include/call sites in src/main.cpp (all marked
// BEGIN/END DRV8833 MOTOR TEST).

// Configures GPIO8/GPIO9 as plain digital outputs, drives them LOW, and
// holds LOW for 2s. Called at the very start of setup(), before Serial/LED/
// mic init, so the motor pins never float. This delays the rest of setup()
// (and therefore the existing HardwareTest/MicRetest sequences) by ~2s;
// none of their own logic changes. Safe to call regardless of
// ENABLE_MOTOR_TEST.
void initMotorTestPins();

// Runs the one-shot motor characterization sequence: coast-stop test,
// forward/reverse start-threshold sweeps, forward/reverse ramps, a safe
// direction-change sequence, and a repeatability test. Never exceeds 70%
// duty, never runs both channels' PWM simultaneously, never holds a stall
// condition, and never reverses without an intervening stop. No-op if
// ENABLE_MOTOR_TEST is 0. Aborts immediately (outputs -> LOW, remaining
// stages skipped) if any byte arrives on Serial during the run.
void runMotorTest();
// END DRV8833 MOTOR TEST
