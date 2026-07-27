#include "MotorDriver.h"

#include <Arduino.h>

namespace {
constexpr uint8_t MOTOR_IN1_PIN = 8;
constexpr uint8_t MOTOR_IN2_PIN = 9;
}  // namespace

void initMotor() {
  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
}

void motorForward() {
  digitalWrite(MOTOR_IN1_PIN, HIGH);
  digitalWrite(MOTOR_IN2_PIN, LOW);
}

void motorReverse() {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, HIGH);
}

void motorStop() {
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
}

void motorBrake() {
  digitalWrite(MOTOR_IN1_PIN, HIGH);
  digitalWrite(MOTOR_IN2_PIN, HIGH);
}

void motorForwardMs(uint32_t ms) {
  motorForward();
  delay(ms);
  motorStop();
}

void motorReverseMs(uint32_t ms) {
  motorReverse();
  delay(ms);
  motorStop();
}
