// BEGIN DRV8833 MOTOR TEST
// See include/MotorTest.h for scope/removal instructions.
//
// Drives a DRV8833 H-bridge via GPIO8 (IN1) and GPIO9 (IN2). GPIO8/GPIO9
// are not used anywhere else in this project (see include/Config.h:
// LED_PIN=4, buttons on 5/10/11/17, I2S on 6/7/15; Adafruit_NeoPixel uses
// the RMT peripheral, not LEDC, so there's no channel overlap either).
//
// ELECTRICAL DIAGNOSTIC (this revision): the prior 7-stage LEDC/PWM
// characterization test produced no visible/audible motor response, so
// this revision replaces it with the simplest possible test: four fixed
// digitalWrite() states (LOW/LOW, HIGH/LOW, HIGH/HIGH, LOW/HIGH), each held
// for 10 seconds with a live per-second countdown, purely so a multimeter
// can be walked across GPIO8, GPIO9, OUT1, OUT2, and GND at a comfortable
// pace. No PWM, no LEDC, no analogWrite -- see the
// MOTOR ELECTRICAL DIAGNOSTIC block below.
//
// ULT/SLEEP: no project notes (README, prior test source, docs/) document
// this breakout board's exact ULT/SLEEP circuit, and the user has
// explicitly said not to assume a connection exists. Only VCC, GND, IN1,
// IN2, OUT1, OUT2 are confirmed wired. This file does not reference,
// drive, or assume anything about ULT/SLEEP.
#include "MotorTest.h"

#include <Arduino.h>

namespace {
constexpr uint8_t MOTOR_IN1_PIN = 8;
constexpr uint8_t MOTOR_IN2_PIN = 9;
}  // namespace

void initMotorTestPins() {
  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  delay(2000);
}

#if ENABLE_MOTOR_TEST

// ===== BEGIN MOTOR ELECTRICAL DIAGNOSTIC =====
// Not a production motor test. A slow, five-state electrical walk (STATE
// 0-4) intended purely to make multimeter measurement easy: each state is
// held 10s with a live countdown, using only digitalWrite() on GPIO8/GPIO9
// -- no PWM, no LEDC, no analogWrite. STATE 2 (both inputs HIGH) is the
// DRV8833's documented "brake" mode (both outputs driven to the same
// rail), not a mechanical stall -- included to check whether the driver's
// outputs actually change state electrically, independent of whether the
// motor spins.
namespace {

bool g_aborted = false;

void setStop() {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
}

// Sleeps in short chunks, checking for an abort request (any incoming
// serial byte) between chunks. On abort: drains the input, forces outputs
// LOW, sets g_aborted, and returns true immediately.
bool delayWithAbort(uint32_t ms) {
  constexpr uint32_t CHUNK_MS = 20;
  uint32_t waited = 0;
  while (waited < ms) {
    if (Serial.available() > 0) {
      while (Serial.available() > 0) Serial.read();
      g_aborted = true;
      setStop();
      return true;
    }
    uint32_t thisChunk = min(CHUNK_MS, ms - waited);
    delay(thisChunk);
    waited += thisChunk;
  }
  return false;
}

// One countdown line per second for `seconds` seconds -- this IS the
// "countdown before each state change" the next state transition happens
// immediately after it reaches 1. Returns true if aborted mid-countdown.
bool holdWithCountdown(uint32_t seconds) {
  for (uint32_t s = seconds; s >= 1; s--) {
    Serial.printf("[MOTOR DIAG] Next state in %lus...\n", (unsigned long)s);
    if (delayWithAbort(1000)) return true;
  }
  return false;
}

void printStateHeader(const char *label) {
  Serial.println(F("----------------------------------------"));
  Serial.println(label);
  Serial.println(F("Measure:"));
  Serial.println(F("  GPIO8 -> GND"));
  Serial.println(F("  GPIO9 -> GND"));
  Serial.println(F("  OUT1 -> GND"));
  Serial.println(F("  OUT2 -> GND"));
  Serial.println(F("  Voltage across OUT1 and OUT2"));
  Serial.println(F("----------------------------------------"));
}

void finishAborted() {
  setStop();
  Serial.println(F("[MOTOR DIAG] ABORTED by user (serial input received) -- outputs LOW"));
  Serial.println(F("[MOTOR DIAG] ==================================="));
}

}  // namespace

void runMotorTest() {
  g_aborted = false;

  Serial.println(F("[MOTOR DIAG] ==================================="));
  Serial.println(F("[MOTOR DIAG] DRV8833 electrical diagnostic -- digitalWrite only, no PWM/LEDC"));
  Serial.printf("[MOTOR DIAG] GPIO assignments: IN1=GPIO%u, IN2=GPIO%u\n", MOTOR_IN1_PIN, MOTOR_IN2_PIN);
  Serial.println(F("[MOTOR DIAG] Each state holds 10s. Send any byte on Serial at any time to ABORT (outputs -> LOW)"));

  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);

  // STATE 0 - BOTH LOW
  printStateHeader("STATE 0 - BOTH LOW");
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  if (holdWithCountdown(10)) { finishAborted(); return; }

  // STATE 1 - FORWARD
  printStateHeader("STATE 1 - FORWARD");
  digitalWrite(MOTOR_IN1_PIN, HIGH);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  if (holdWithCountdown(10)) { finishAborted(); return; }

  // STATE 2 - BOTH HIGH
  printStateHeader("STATE 2 - BOTH HIGH");
  digitalWrite(MOTOR_IN1_PIN, HIGH);
  digitalWrite(MOTOR_IN2_PIN, HIGH);
  if (holdWithCountdown(10)) { finishAborted(); return; }

  // STATE 3 - REVERSE
  printStateHeader("STATE 3 - REVERSE");
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, HIGH);
  if (holdWithCountdown(10)) { finishAborted(); return; }

  // STATE 4 - BOTH LOW (END) -- stays here forever, per spec. This upload
  // intentionally does not return to normal Sunflower LED/mic/button
  // operation; setup() never completes and loop() never starts this run.
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
  Serial.println(F("----------------------------------------"));
  Serial.println(F("STATE 4 - BOTH LOW (END)"));
  Serial.println(F("----------------------------------------"));
  Serial.println(F("[MOTOR DIAG] Diagnostic complete -- outputs LOW permanently, staying here"));
  Serial.println(F("[MOTOR DIAG] ==================================="));
  for (;;) {
    if (Serial.available() > 0) { while (Serial.available() > 0) Serial.read(); }
    delay(200);
  }
}
// ===== END MOTOR ELECTRICAL DIAGNOSTIC =====

#else

void runMotorTest() {}

#endif
// END DRV8833 MOTOR TEST
