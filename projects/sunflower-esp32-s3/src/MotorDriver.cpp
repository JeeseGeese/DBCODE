#include "MotorDriver.h"

#include <Arduino.h>

namespace {
constexpr uint8_t MOTOR_IN1_PIN = 8;
constexpr uint8_t MOTOR_IN2_PIN = 9;

// LEDC channels reserved for the motor PWM calibration test -- not used by
// any other module in this project (grep-verified before picking these).
constexpr uint8_t MOTOR_PWM_CHANNEL_IN1 = 0;
constexpr uint8_t MOTOR_PWM_CHANNEL_IN2 = 1;

bool motorPWMActive = false;
uint32_t motorPWMFrequency = 0;
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

uint32_t initMotorPWM(uint32_t freqHz, uint8_t resolutionBits) {
  if (motorPWMActive) return motorPWMFrequency;
  motorPWMFrequency = ledcSetup(MOTOR_PWM_CHANNEL_IN1, freqHz, resolutionBits);
  ledcSetup(MOTOR_PWM_CHANNEL_IN2, freqHz, resolutionBits);
  ledcAttachPin(MOTOR_IN1_PIN, MOTOR_PWM_CHANNEL_IN1);
  ledcAttachPin(MOTOR_IN2_PIN, MOTOR_PWM_CHANNEL_IN2);
  ledcWrite(MOTOR_PWM_CHANNEL_IN1, 0);
  ledcWrite(MOTOR_PWM_CHANNEL_IN2, 0);
  motorPWMActive = true;
  return motorPWMFrequency;
}

void deinitMotorPWM() {
  if (!motorPWMActive) return;
  ledcWrite(MOTOR_PWM_CHANNEL_IN1, 0);
  ledcWrite(MOTOR_PWM_CHANNEL_IN2, 0);
  ledcDetachPin(MOTOR_IN1_PIN);
  ledcDetachPin(MOTOR_IN2_PIN);
  motorPWMActive = false;
  // Restore plain digital-output/stopped state -- matches initMotor()/motorStop().
  pinMode(MOTOR_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IN2_PIN, OUTPUT);
  digitalWrite(MOTOR_IN1_PIN, LOW);
  digitalWrite(MOTOR_IN2_PIN, LOW);
}

bool isMotorPWMActive() { return motorPWMActive; }

void motorPWMForward(uint8_t duty) {
  ledcWrite(MOTOR_PWM_CHANNEL_IN2, 0);
  ledcWrite(MOTOR_PWM_CHANNEL_IN1, duty);
}

void motorPWMReverse(uint8_t duty) {
  ledcWrite(MOTOR_PWM_CHANNEL_IN1, 0);
  ledcWrite(MOTOR_PWM_CHANNEL_IN2, duty);
}

void motorPWMCoast() {
  ledcWrite(MOTOR_PWM_CHANNEL_IN1, 0);
  ledcWrite(MOTOR_PWM_CHANNEL_IN2, 0);
}

uint32_t getMotorPWMFrequency() { return motorPWMFrequency; }
