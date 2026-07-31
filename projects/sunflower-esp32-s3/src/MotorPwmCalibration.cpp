#include "MotorPwmCalibration.h"

#include <Arduino.h>

#include "ExpressiveMotion.h"  // isAnyMotorDiagnosticActive() / isExpressiveMotionMoving() -- implemented in
                                // main.cpp, declared here per the existing project convention (see that
                                // header's own comment); used to refuse starting while another motor
                                // diagnostic or expressive motion owns the motor.
#include "MotorBehavior.h"     // setMotorBehavior(OFF) -- preempts IDLE_SWAY when this test takes ownership
#include "MotorDriver.h"       // motorPWM*()/initMotorPWM()/deinitMotorPWM() -- the only pin-touching layer

namespace {

// --- Tunables (spec ranges: freq 18-20kHz, 8-bit resolution 0-255) ---
constexpr uint32_t MOTOR_CAL_TICK_MS = 15;          // duty-update tick interval (spec: ~10-20ms)
constexpr uint32_t STARTUP_KICK_MS = 100;            // spec: 80-120ms
constexpr uint8_t STARTUP_KICK_BELOW_PERCENT = 70;   // kick applies only below this requested duty
constexpr uint32_t REVERSAL_COAST_MS = 70;           // spec: 60-80ms between-directions coast

uint8_t percentToDuty(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)((uint16_t)percent * 255 / 100);
}

const char *dirName(MotorCalDirection d) { return d == MotorCalDirection::FORWARD ? "FORWARD" : "REVERSE"; }

const char *routineName(MotorCalRoutine r) {
  switch (r) {
    case MotorCalRoutine::NONE: return "none";
    case MotorCalRoutine::MANUAL: return "manual";
    case MotorCalRoutine::RAMP: return "mramp";
    case MotorCalRoutine::CYCLE: return "mcycle";
  }
  return "unknown";
}

// ============================================================================
// Low-level drive engine: owns the actual commanded duty, the startup kick,
// linear ramps, and the safe-reversal coast sequence. Every higher-level
// caller (manual speed commands, mramp steps, mcycle steps) goes through
// engineDrive()/engineDriveRamped() -- neither of those ever calls
// motorPWMForward()/motorPWMReverse() directly without going through this
// state machine, so the "never switch directly forward<->reverse" rule and
// the startup-kick rule are enforced in exactly one place.
// ============================================================================

enum class EngineState { STOPPED, KICK, RAMP, HOLD, COAST_FOR_REVERSAL };

EngineState engineState = EngineState::STOPPED;
MotorCalDirection activeDirection = MotorCalDirection::FORWARD;
uint8_t currentDutyPercent = 0;  // live, always mirrors what's actually commanded to hardware

unsigned long rampStartMs = 0;
unsigned long rampDurationMs = 0;
uint8_t rampFromPercent = 0;
uint8_t rampToPercent = 0;

unsigned long kickEndMs = 0;
uint8_t kickSettlePercent = 0;

unsigned long coastEndMs = 0;
MotorCalDirection reversalTargetDirection = MotorCalDirection::FORWARD;
uint8_t reversalTargetPercent = 0;

bool kickEnabled = true;
bool lastKickApplied = false;  // set by beginDrive(), read by callers right after, for the transition print

bool pwmReady = false;

void ensurePWMReady() {
  if (pwmReady) return;
  initMotorPWM(MOTOR_CAL_PWM_FREQUENCY_HZ, MOTOR_CAL_PWM_RESOLUTION_BITS);
  pwmReady = true;
}

void applyDuty(MotorCalDirection dir, uint8_t percent) {
  uint8_t duty = percentToDuty(percent);
  if (dir == MotorCalDirection::FORWARD) {
    motorPWMForward(duty);
  } else {
    motorPWMReverse(duty);
  }
  activeDirection = dir;
  currentDutyPercent = percent;
}

void applyCoast() {
  motorPWMCoast();
  currentDutyPercent = 0;
}

void printReversalTransition();  // fwd decl -- defined after printTransition() below

// Instant entry: applies the startup kick (if eligible) then settles to
// `percent`, or goes straight to `percent` if no kick applies. Assumes the
// caller has already resolved any direction reversal (i.e. the motor is
// either already stopped, or already running in `direction`).
void beginDrive(MotorCalDirection direction, uint8_t percent) {
  bool startingFromStopped = (currentDutyPercent == 0);
  activeDirection = direction;
  if (startingFromStopped && percent > 0 && kickEnabled && percent < STARTUP_KICK_BELOW_PERCENT) {
    lastKickApplied = true;
    kickSettlePercent = percent;
    kickEndMs = millis() + STARTUP_KICK_MS;
    applyDuty(direction, 100);
    engineState = EngineState::KICK;
    Serial.printf("[MOTOR TEST] Startup kick applied: 100%% for %ums, then settling to %u%%\n",
                  (unsigned)STARTUP_KICK_MS, percent);
  } else {
    lastKickApplied = false;
    applyDuty(direction, percent);
    engineState = (percent == 0) ? EngineState::STOPPED : EngineState::HOLD;
  }
}

// Ramped entry: linearly ramps currentDutyPercent -> percent over rampMs.
// Same reversal precondition as beginDrive() -- never called with a pending
// direction change (mcycle's table never asks for one; see its steps).
void beginRampedDrive(MotorCalDirection direction, uint8_t percent, uint32_t rampMs) {
  lastKickApplied = false;  // ramped entries only ever occur while already running -- never a fresh start
  activeDirection = direction;
  rampFromPercent = currentDutyPercent;
  rampToPercent = percent;
  rampStartMs = millis();
  rampDurationMs = (rampMs == 0) ? 1 : rampMs;
  engineState = EngineState::RAMP;
}

void beginReversalCoast(MotorCalDirection newDirection, uint8_t newPercent) {
  applyCoast();
  engineState = EngineState::COAST_FOR_REVERSAL;
  coastEndMs = millis() + REVERSAL_COAST_MS;
  reversalTargetDirection = newDirection;
  reversalTargetPercent = newPercent;
  Serial.println(F("[MOTOR TEST] Direction change requested -- coasting before reversal"));
}

// Public engine entry point (instant/hold-style). Handles the safe-reversal
// sequence automatically: if the motor is currently running the opposite
// direction, ramps/coasts through zero and waits REVERSAL_COAST_MS before
// actually driving the new direction.
void engineDrive(MotorCalDirection direction, uint8_t percent) {
  if (percent > 100) percent = 100;
  bool isRunning = currentDutyPercent > 0 && engineState != EngineState::COAST_FOR_REVERSAL;
  if (isRunning && direction != activeDirection) {
    beginReversalCoast(direction, percent);
    return;
  }
  if (engineState == EngineState::COAST_FOR_REVERSAL) {
    reversalTargetDirection = direction;
    reversalTargetPercent = percent;
    return;
  }
  beginDrive(direction, percent);
}

// Ramped variant, used by mcycle's explicit ramp steps. Falls back to the
// same reversal-coast handling as engineDrive() if ever called across a
// direction change (not exercised by the current mcycle table, but kept
// safe rather than assumed unreachable).
void engineDriveRamped(MotorCalDirection direction, uint8_t percent, uint32_t rampMs) {
  if (percent > 100) percent = 100;
  bool isRunning = currentDutyPercent > 0 && engineState != EngineState::COAST_FOR_REVERSAL;
  if (isRunning && direction != activeDirection) {
    beginReversalCoast(direction, percent);
    return;
  }
  if (engineState == EngineState::COAST_FOR_REVERSAL) {
    reversalTargetDirection = direction;
    reversalTargetPercent = percent;
    return;
  }
  beginRampedDrive(direction, percent, rampMs);
}

// Forces an immediate coast, bypassing ramp/kick/reversal bookkeeping --
// used by mcycle's explicit "neutral" step and by mstop/emergency-cancel.
void engineForceCoast() {
  applyCoast();
  engineState = EngineState::STOPPED;
}

unsigned long lastEngineTickMs = 0;

void engineUpdate() {
  unsigned long now = millis();
  if (now - lastEngineTickMs < MOTOR_CAL_TICK_MS) return;
  lastEngineTickMs = now;

  switch (engineState) {
    case EngineState::STOPPED:
    case EngineState::HOLD:
      return;
    case EngineState::KICK:
      if ((long)(now - kickEndMs) >= 0) {
        applyDuty(activeDirection, kickSettlePercent);
        engineState = (kickSettlePercent == 0) ? EngineState::STOPPED : EngineState::HOLD;
      }
      return;
    case EngineState::RAMP: {
      float t = (float)(now - rampStartMs) / (float)rampDurationMs;
      if (t >= 1.0f) t = 1.0f;
      int16_t span = (int16_t)rampToPercent - (int16_t)rampFromPercent;
      uint8_t p = (uint8_t)((int16_t)rampFromPercent + (int16_t)(span * t));
      applyDuty(activeDirection, p);
      if (t >= 1.0f) engineState = (p == 0) ? EngineState::STOPPED : EngineState::HOLD;
      return;
    }
    case EngineState::COAST_FOR_REVERSAL:
      if ((long)(now - coastEndMs) >= 0) {
        beginDrive(reversalTargetDirection, reversalTargetPercent);
        printReversalTransition();
      }
      return;
  }
}

// ============================================================================
// Shared transition printing (spec's exact block format)
// ============================================================================

void printTransition(MotorCalDirection dir, uint8_t percent, bool kickApplied, const char *stepLabel) {
  Serial.printf("[MOTOR TEST] Direction: %s\n", dirName(dir));
  Serial.printf("[MOTOR TEST] Requested: %u%%\n", percent);
  Serial.printf("[MOTOR TEST] Duty: %u/255\n", percentToDuty(percent));
  Serial.printf("[MOTOR TEST] Startup kick: %s\n", kickApplied ? "YES" : "NO");
  if (stepLabel && stepLabel[0]) Serial.printf("[MOTOR TEST] %s\n", stepLabel);
}

void printReversalTransition() {
  printTransition(activeDirection, reversalTargetPercent, lastKickApplied, "(direction changed)");
}

// ============================================================================
// Top-level test/routine state
// ============================================================================

bool testActive = false;
MotorCalRoutine currentRoutine = MotorCalRoutine::NONE;
MotorCalDirection selectedDirection = MotorCalDirection::FORWARD;  // 'mf'/'mr' selection for manual mode
uint8_t manualRequestedPercent = 0;                                 // 0 = no manual hold running
bool lastStopWasEmergency = false;

void finishTest(const char *message) {
  applyCoast();
  deinitMotorPWM();
  pwmReady = false;
  setMotorBehavior(MotorBehaviorMode::OFF);  // return ownership cleanly -- never auto-resumes IDLE_SWAY
  testActive = false;
  currentRoutine = MotorCalRoutine::NONE;
  manualRequestedPercent = 0;
  engineState = EngineState::STOPPED;
  if (message) Serial.println(message);
}

// Returns true if the motor is (or already was) exclusively owned by this
// module -- refuses and prints a message if another motor diagnostic or
// expressive motion currently owns it. Only actually checks external state
// on the FIRST entry into an active test; once testActive is already true,
// internal transitions (mf/mr/m##/mramp/mcycle while already running) are
// always allowed without re-checking (isAnyMotorDiagnosticActive() would
// otherwise see this module's own active flag and refuse itself).
bool ensureExclusiveOwnership() {
  if (testActive) return true;
  if (isAnyMotorDiagnosticActive() || isExpressiveMotionMoving()) {
    Serial.println(F("[MOTOR TEST] Refused -- another motor diagnostic or expressive motion currently owns the motor"));
    return false;
  }
  setMotorBehavior(MotorBehaviorMode::OFF);  // preempt IDLE_SWAY
  testActive = true;
  lastStopWasEmergency = false;
  return true;
}

// --- mramp state ---
enum class RampPhase { FORWARD, COAST, REVERSE, DONE };
constexpr uint8_t RAMP_STEP_COUNT = 9;
constexpr uint32_t RAMP_STEP_DURATION_MS = 2000;
constexpr uint32_t RAMP_COAST_DURATION_MS = 500;

RampPhase rampPhase = RampPhase::FORWARD;
uint8_t rampStepIdx = 0;  // 0..8 -> 20%..100%
unsigned long rampStepStartMs = 0;

uint8_t rampStepPercent(uint8_t idx) { return (uint8_t)(20 + idx * 10); }

void enterRampStep(RampPhase phase, uint8_t idx) {
  MotorCalDirection dir = (phase == RampPhase::FORWARD) ? MotorCalDirection::FORWARD : MotorCalDirection::REVERSE;
  uint8_t percent = rampStepPercent(idx);
  engineDrive(dir, percent);
  char label[16];
  snprintf(label, sizeof(label), "Step %u/%u", (unsigned)(idx + 1), (unsigned)RAMP_STEP_COUNT);
  printTransition(dir, percent, lastKickApplied, label);
  rampStepStartMs = millis();
}

void startRampRoutine() {
  if (!ensureExclusiveOwnership()) return;
  currentRoutine = MotorCalRoutine::RAMP;
  ensurePWMReady();
  rampPhase = RampPhase::FORWARD;
  rampStepIdx = 0;
  Serial.println(F("[MOTOR TEST] mramp: starting forward ramp"));
  enterRampStep(RampPhase::FORWARD, 0);
}

void updateRampRoutine() {
  unsigned long now = millis();
  switch (rampPhase) {
    case RampPhase::FORWARD:
      if (now - rampStepStartMs >= RAMP_STEP_DURATION_MS) {
        if (rampStepIdx + 1 < RAMP_STEP_COUNT) {
          rampStepIdx++;
          enterRampStep(RampPhase::FORWARD, rampStepIdx);
        } else {
          engineForceCoast();
          Serial.println(F("[MOTOR TEST] Coast (500ms)"));
          rampPhase = RampPhase::COAST;
          rampStepStartMs = now;
        }
      }
      return;
    case RampPhase::COAST:
      if (now - rampStepStartMs >= RAMP_COAST_DURATION_MS) {
        rampPhase = RampPhase::REVERSE;
        rampStepIdx = 0;
        Serial.println(F("[MOTOR TEST] mramp: starting reverse ramp"));
        enterRampStep(RampPhase::REVERSE, 0);
      }
      return;
    case RampPhase::REVERSE:
      if (now - rampStepStartMs >= RAMP_STEP_DURATION_MS) {
        if (rampStepIdx + 1 < RAMP_STEP_COUNT) {
          rampStepIdx++;
          enterRampStep(RampPhase::REVERSE, rampStepIdx);
        } else {
          rampPhase = RampPhase::DONE;
          finishTest("[MOTOR TEST] mramp complete");
        }
      }
      return;
    case RampPhase::DONE:
      return;
  }
}

// --- mcycle state ---
enum class CycleMode { HOLD, RAMP_TO, INSTANT_TO, COAST };
struct CycleStep {
  MotorCalDirection direction;
  CycleMode mode;
  uint8_t percent;
  uint32_t rampMs;
  uint32_t stepMs;
  const char *label;
};

// clang-format off
constexpr CycleStep CYCLE_STEPS[] = {
  {MotorCalDirection::FORWARD, CycleMode::INSTANT_TO,  35,   0, 1000, "Forward 35% (slow dancing)"},
  {MotorCalDirection::FORWARD, CycleMode::RAMP_TO,      60, 500,  500, "Ramp forward -> 60%"},
  {MotorCalDirection::FORWARD, CycleMode::HOLD,         60,   0, 1000, "Hold forward 60% (medium dancing)"},
  {MotorCalDirection::FORWARD, CycleMode::RAMP_TO,     100, 300,  300, "Ramp forward -> 100%"},
  {MotorCalDirection::FORWARD, CycleMode::HOLD,        100,   0, 1400, "Hold forward 100% (full-energy dancing)"},
  {MotorCalDirection::FORWARD, CycleMode::RAMP_TO,       0, 150,  150, "Ramp forward -> 0%"},
  {MotorCalDirection::FORWARD, CycleMode::COAST,         0,   0,   75, "Neutral coast"},
  {MotorCalDirection::REVERSE, CycleMode::INSTANT_TO,  100,   0, 1200, "Reverse 100% (quick safe reversal)"},
  {MotorCalDirection::REVERSE, CycleMode::RAMP_TO,      55, 500,  500, "Ramp reverse -> 55%"},
  {MotorCalDirection::REVERSE, CycleMode::HOLD,         55,   0, 1000, "Hold reverse 55%"},
  {MotorCalDirection::REVERSE, CycleMode::RAMP_TO,      85, 300,  300, "Ramp reverse -> 85%"},
  {MotorCalDirection::REVERSE, CycleMode::HOLD,         85,   0, 1200, "Hold reverse 85%"},
  {MotorCalDirection::REVERSE, CycleMode::RAMP_TO,       0, 200,  200, "Ramp -> 0% and stop"},
};
// clang-format on
constexpr uint8_t CYCLE_STEP_COUNT = sizeof(CYCLE_STEPS) / sizeof(CYCLE_STEPS[0]);

uint8_t cycleStepIdx = 0;
unsigned long cycleStepStartMs = 0;
bool cycleRunning = false;

void enterCycleStep(uint8_t idx) {
  const CycleStep &s = CYCLE_STEPS[idx];
  char label[40];
  snprintf(label, sizeof(label), "Step %u/%u: %s", (unsigned)(idx + 1), (unsigned)CYCLE_STEP_COUNT, s.label);

  switch (s.mode) {
    case CycleMode::INSTANT_TO:
      engineDrive(s.direction, s.percent);
      printTransition(s.direction, s.percent, lastKickApplied, label);
      break;
    case CycleMode::RAMP_TO:
      engineDriveRamped(s.direction, s.percent, s.rampMs);
      printTransition(s.direction, s.percent, false, label);
      break;
    case CycleMode::HOLD:
      Serial.printf("[MOTOR TEST] %s\n", label);
      break;
    case CycleMode::COAST:
      engineForceCoast();
      Serial.printf("[MOTOR TEST] %s\n", label);
      break;
  }
  cycleStepStartMs = millis();
}

void startCycleRoutine() {
  if (!ensureExclusiveOwnership()) return;
  currentRoutine = MotorCalRoutine::CYCLE;
  ensurePWMReady();
  cycleRunning = true;
  cycleStepIdx = 0;
  Serial.println(F("[MOTOR TEST] mcycle: starting dance sequence"));
  enterCycleStep(0);
}

void updateCycleRoutine() {
  if (!cycleRunning) return;
  unsigned long now = millis();
  const CycleStep &s = CYCLE_STEPS[cycleStepIdx];
  if (now - cycleStepStartMs < s.stepMs) return;
  if (cycleStepIdx + 1 < CYCLE_STEP_COUNT) {
    cycleStepIdx++;
    enterCycleStep(cycleStepIdx);
  } else {
    cycleRunning = false;
    finishTest("[MOTOR TEST] mcycle complete");
  }
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

void initMotorPwmCalibration() {
  engineState = EngineState::STOPPED;
  activeDirection = MotorCalDirection::FORWARD;
  currentDutyPercent = 0;
  kickEnabled = true;
  lastKickApplied = false;
  pwmReady = false;
  testActive = false;
  currentRoutine = MotorCalRoutine::NONE;
  selectedDirection = MotorCalDirection::FORWARD;
  manualRequestedPercent = 0;
  lastStopWasEmergency = false;
  rampPhase = RampPhase::FORWARD;
  rampStepIdx = 0;
  cycleRunning = false;
  cycleStepIdx = 0;
  lastEngineTickMs = 0;
}

void updateMotorPwmCalibration() {
  if (!testActive) return;
  engineUpdate();
  if (currentRoutine == MotorCalRoutine::RAMP) updateRampRoutine();
  else if (currentRoutine == MotorCalRoutine::CYCLE) updateCycleRoutine();
}

bool isMotorPwmCalibrationActive() { return testActive; }

void motorCalSelectDirection(MotorCalDirection direction) {
  if (!ensureExclusiveOwnership()) return;
  currentRoutine = MotorCalRoutine::MANUAL;
  ensurePWMReady();
  bool directionActuallyChanged = (direction != selectedDirection) || (direction != activeDirection);
  selectedDirection = direction;
  Serial.printf("[MOTOR TEST] Direction selected: %s\n", dirName(direction));
  if (manualRequestedPercent > 0 && directionActuallyChanged) {
    engineDrive(direction, manualRequestedPercent);
    if (engineState != EngineState::COAST_FOR_REVERSAL) {
      printTransition(direction, manualRequestedPercent, lastKickApplied, "(direction changed)");
    }
    // else: the transition prints itself once the reversal coast completes (printReversalTransition()).
  }
}

void motorCalManualSpeed(uint8_t percent) {
  if (percent == 0 || percent > 100) {
    Serial.println(F("[MOTOR TEST] Manual speed must be 1-100"));
    return;
  }
  if (!ensureExclusiveOwnership()) return;
  currentRoutine = MotorCalRoutine::MANUAL;
  ensurePWMReady();
  manualRequestedPercent = percent;
  engineDrive(selectedDirection, percent);
  if (engineState != EngineState::COAST_FOR_REVERSAL) {
    printTransition(selectedDirection, percent, lastKickApplied, "Manual");
  }
}

void motorCalStop() {
  if (!testActive) {
    Serial.println(F("[MOTOR TEST] Already stopped"));
    return;
  }
  finishTest("[MOTOR TEST] Stopped (mstop)");
}

void motorCalEmergencyCancel() {
  if (!testActive) return;
  applyCoast();
  deinitMotorPWM();
  pwmReady = false;
  testActive = false;
  currentRoutine = MotorCalRoutine::NONE;
  manualRequestedPercent = 0;
  engineState = EngineState::STOPPED;
  cycleRunning = false;
  rampPhase = RampPhase::DONE;
  lastStopWasEmergency = true;
}

void motorCalStartRamp() { startRampRoutine(); }
void motorCalStartCycle() { startCycleRoutine(); }

void motorCalToggleKick() {
  kickEnabled = !kickEnabled;
  Serial.printf("[MOTOR TEST] Startup kick %s\n", kickEnabled ? "ENABLED" : "DISABLED");
}

void motorCalPrintStatus() {
  unsigned long now = millis();
  Serial.println(F("[MOTOR TEST STATUS]"));
  Serial.printf("  Test active: %s\n", testActive ? "yes" : "no");
  Serial.printf("  Routine: %s\n", routineName(currentRoutine));
  Serial.printf("  Direction: %s (selected: %s)\n", dirName(activeDirection), dirName(selectedDirection));
  Serial.printf("  Current PWM duty: %u/255 (%u%%)\n", percentToDuty(currentDutyPercent), currentDutyPercent);

  uint8_t targetPercent = currentDutyPercent;
  switch (engineState) {
    case EngineState::KICK: targetPercent = kickSettlePercent; break;
    case EngineState::RAMP: targetPercent = rampToPercent; break;
    case EngineState::COAST_FOR_REVERSAL: targetPercent = reversalTargetPercent; break;
    default: break;
  }
  Serial.printf("  Target PWM duty: %u/255 (%u%%)\n", percentToDuty(targetPercent), targetPercent);
  Serial.printf("  Startup kick enabled: %s\n", kickEnabled ? "yes" : "no");
  Serial.printf("  Startup kick active: %s\n", engineState == EngineState::KICK ? "yes" : "no");

  if (currentRoutine == MotorCalRoutine::RAMP) {
    unsigned long stepMs = (rampPhase == RampPhase::COAST) ? RAMP_COAST_DURATION_MS : RAMP_STEP_DURATION_MS;
    unsigned long elapsed = now - rampStepStartMs;
    long remaining = (long)stepMs - (long)elapsed;
    if (remaining < 0) remaining = 0;
    const char *phaseName = rampPhase == RampPhase::FORWARD ? "FORWARD" : rampPhase == RampPhase::COAST ? "COAST" : "REVERSE";
    Serial.printf("  Routine step: %s %u/%u  Remaining: %ldms\n", phaseName, (unsigned)(rampStepIdx + 1),
                  (unsigned)RAMP_STEP_COUNT, remaining);
  } else if (currentRoutine == MotorCalRoutine::CYCLE) {
    const CycleStep &s = CYCLE_STEPS[cycleStepIdx];
    unsigned long elapsed = now - cycleStepStartMs;
    long remaining = (long)s.stepMs - (long)elapsed;
    if (remaining < 0) remaining = 0;
    Serial.printf("  Routine step: %u/%u (%s)  Remaining: %ldms\n", (unsigned)(cycleStepIdx + 1),
                  (unsigned)CYCLE_STEP_COUNT, s.label, remaining);
  } else {
    Serial.println(F("  Routine step: -  Remaining: -"));
  }

  Serial.printf("  Pending reversal: %s\n", engineState == EngineState::COAST_FOR_REVERSAL ? "yes" : "no");
  Serial.printf("  Emergency-stop state: %s\n", lastStopWasEmergency ? "LATCHED (last stop was emergency)" : "clear");

  if (!testActive || currentDutyPercent == 0) {
    Serial.println(F("  GPIO8 (IN1): LOW   GPIO9 (IN2): LOW"));
  } else if (activeDirection == MotorCalDirection::FORWARD) {
    Serial.printf("  GPIO8 (IN1): PWM duty=%u/255   GPIO9 (IN2): LOW\n", percentToDuty(currentDutyPercent));
  } else {
    Serial.printf("  GPIO8 (IN1): LOW   GPIO9 (IN2): PWM duty=%u/255\n", percentToDuty(currentDutyPercent));
  }
  Serial.printf("  PWM config: %luHz, %u-bit (0-255)\n", (unsigned long)MOTOR_CAL_PWM_FREQUENCY_HZ,
                (unsigned)MOTOR_CAL_PWM_RESOLUTION_BITS);
}
