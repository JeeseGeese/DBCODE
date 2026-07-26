// drv8833-motor-test
// Minimal bring-up test for a single DRV8833 H-bridge channel driving one
// brushed DC motor from an ESP32-S3 N16R8 board.
//
// Wiring (verified):
//   ESP32-S3 GPIO8 -> DRV8833 IN1
//   ESP32-S3 GPIO9 -> DRV8833 IN2
//   ESP32-S3 GND   -> DRV8833 GND
//   DRV8833 VCC    -> external test supply (NOT the ESP32-S3)
//   Motor wire 1   -> DRV8833 OUT1
//   Motor wire 2   -> DRV8833 OUT2
//
// Unused on the DRV8833 for this test: IN3, IN4, OUT3, OUT4, EEP, and ULT
// (leave ULT alone unless testing later proves the board is asleep).

#include <Arduino.h>

static const char *PROJECT_NAME = "drv8833-motor-test";

static const uint8_t MOTOR_IN1 = 8;
static const uint8_t MOTOR_IN2 = 9;

static const unsigned long SAFETY_TIMEOUT_MS = 10000;

enum MotorState {
  MOTOR_STOP,
  MOTOR_FORWARD,
  MOTOR_REVERSE,
};

enum ControlMode {
  MODE_AUTO,
  MODE_MANUAL,
};

struct SequenceStep {
  MotorState state;
  unsigned long durationMs;
};

// STOP 3s -> FORWARD 2s -> STOP 3s -> REVERSE 2s -> STOP 3s -> repeat
static const SequenceStep AUTO_SEQUENCE[] = {
    {MOTOR_STOP, 3000},
    {MOTOR_FORWARD, 2000},
    {MOTOR_STOP, 3000},
    {MOTOR_REVERSE, 2000},
    {MOTOR_STOP, 3000},
};
static const size_t AUTO_SEQUENCE_LEN = sizeof(AUTO_SEQUENCE) / sizeof(AUTO_SEQUENCE[0]);

static ControlMode mode = MODE_AUTO;

static MotorState currentState;
static bool haveCurrentState = false;

static size_t autoIndex = 0;
static unsigned long autoStepStartMs = 0;

static MotorState manualState = MOTOR_STOP;
static unsigned long manualDriveStartMs = 0;
static bool manualTimeoutArmed = false;

static const char *stateName(MotorState s) {
  switch (s) {
    case MOTOR_STOP:
      return "STOP";
    case MOTOR_FORWARD:
      return "FORWARD";
    case MOTOR_REVERSE:
      return "REVERSE";
  }
  return "UNKNOWN";
}

// Drives the pins for `state` and, if it differs from the currently applied
// state, prints exactly one transition line combining the state name and
// the commanded pin levels.
static void applyMotorState(MotorState state) {
  if (haveCurrentState && state == currentState) {
    return;
  }

  int in1;
  int in2;
  switch (state) {
    case MOTOR_FORWARD:
      in1 = HIGH;
      in2 = LOW;
      break;
    case MOTOR_REVERSE:
      in1 = LOW;
      in2 = HIGH;
      break;
    case MOTOR_STOP:
    default:
      in1 = LOW;
      in2 = LOW;
      break;
  }

  digitalWrite(MOTOR_IN1, in1);
  digitalWrite(MOTOR_IN2, in2);

  Serial.print("[MOTOR] ");
  Serial.print(stateName(state));
  Serial.print(" | IN1=");
  Serial.print(in1 == HIGH ? 1 : 0);
  Serial.print(" IN2=");
  Serial.println(in2 == HIGH ? 1 : 0);

  currentState = state;
  haveCurrentState = true;
}

static void printHelp() {
  Serial.println();
  Serial.println("[HELP] Serial commands:");
  Serial.println("  f = forward");
  Serial.println("  r = reverse");
  Serial.println("  s = stop");
  Serial.println("  a = resume automatic sequence");
  Serial.println("  h = print this help");
  Serial.println();
}

static void printStartupReport() {
  Serial.println();
  Serial.println("=================================================");
  Serial.print("  Project: ");
  Serial.println(PROJECT_NAME);
  Serial.println("=================================================");
  Serial.println("[GPIO] MOTOR_IN1 -> GPIO8  (DRV8833 IN1)");
  Serial.println("[GPIO] MOTOR_IN2 -> GPIO9  (DRV8833 IN2)");
  Serial.println("[GPIO] GND       -> DRV8833 GND");
  Serial.println();
  Serial.println("[TEST SEQUENCE]");
  Serial.println("  1. Boot GPIO self-test (IN1/IN2 toggle, 500 ms stages)");
  Serial.println("  2. Automatic loop: STOP 3s -> FORWARD 2s -> STOP 3s ->");
  Serial.println("     REVERSE 2s -> STOP 3s -> repeat");
  Serial.println("  3. Manual override available at any time via serial:");
  Serial.println("     f / r / s / a / h  (10s safety timeout on f/r)");
  Serial.println();
  Serial.println("[WARNING] The motor must ALWAYS be connected through the");
  Serial.println("DRV8833 driver (OUT1/OUT2). NEVER connect a motor directly");
  Serial.println("to an ESP32-S3 GPIO pin.");
  Serial.println("=================================================");
  Serial.println();
}

// One-time boot self-test, run before the automatic sequence begins.
static void runBootSelfTest() {
  Serial.println("[SELFTEST] Starting boot GPIO self-test...");

  Serial.println("[SELFTEST] Stage 1: IN1=HIGH IN2=LOW (500 ms)");
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
  delay(500);

  Serial.println("[SELFTEST] Stage 2: STOP (500 ms)");
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  delay(500);

  Serial.println("[SELFTEST] Stage 3: IN1=LOW IN2=HIGH (500 ms)");
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
  delay(500);

  Serial.println("[SELFTEST] Stage 4: STOP");
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);

  Serial.println("[SELFTEST] Complete.");
  Serial.println();
}

static void enterAutoMode() {
  mode = MODE_AUTO;
  autoIndex = 0;
  autoStepStartMs = millis();
  Serial.println("[MODE] AUTO sequence resumed");
  // Force the transition print even if the pins already happen to be in
  // this state (e.g. right after boot init or self-test).
  haveCurrentState = false;
  applyMotorState(AUTO_SEQUENCE[autoIndex].state);
}

static void enterManualState(MotorState state) {
  mode = MODE_MANUAL;
  manualState = state;
  applyMotorState(state);

  if (state == MOTOR_FORWARD || state == MOTOR_REVERSE) {
    manualDriveStartMs = millis();
    manualTimeoutArmed = true;
  } else {
    manualTimeoutArmed = false;
  }
}

static void handleSerialCommands() {
  while (Serial.available() > 0) {
    int c = Serial.read();

    switch (c) {
      case 'f':
      case 'F':
        Serial.println("[CMD] Manual FORWARD");
        enterManualState(MOTOR_FORWARD);
        break;

      case 'r':
      case 'R':
        Serial.println("[CMD] Manual REVERSE");
        enterManualState(MOTOR_REVERSE);
        break;

      case 's':
      case 'S':
        Serial.println("[CMD] Manual STOP");
        enterManualState(MOTOR_STOP);
        break;

      case 'a':
      case 'A':
        enterAutoMode();
        break;

      case 'h':
      case 'H':
        printHelp();
        break;

      case '\r':
      case '\n':
        break;

      default:
        Serial.print("[CMD] Unknown command: '");
        Serial.print((char)c);
        Serial.println("' — press 'h' for help");
        break;
    }
  }
}

static void serviceSafetyTimeout() {
  if (mode != MODE_MANUAL || !manualTimeoutArmed) {
    return;
  }

  if (millis() - manualDriveStartMs >= SAFETY_TIMEOUT_MS) {
    Serial.println("[SAFETY] Manual motor timeout \xE2\x80\x94 STOP");
    manualState = MOTOR_STOP;
    manualTimeoutArmed = false;
    applyMotorState(MOTOR_STOP);
  }
}

static void serviceAutoSequence() {
  if (mode != MODE_AUTO) {
    return;
  }

  if (millis() - autoStepStartMs >= AUTO_SEQUENCE[autoIndex].durationMs) {
    autoIndex = (autoIndex + 1) % AUTO_SEQUENCE_LEN;
    autoStepStartMs = millis();
    applyMotorState(AUTO_SEQUENCE[autoIndex].state);
  }
}

void setup() {
  // Keep the motor stopped through upload/init: configure and drive both
  // pins LOW before anything else happens.
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  currentState = MOTOR_STOP;
  haveCurrentState = true;

  Serial.begin(115200);
  unsigned long serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) {
    delay(10);
  }

  delay(2000); // settle time before the test begins, as required

  printStartupReport();
  runBootSelfTest();
  printHelp();

  enterAutoMode();
}

void loop() {
  handleSerialCommands();
  serviceSafetyTimeout();
  serviceAutoSequence();
}
