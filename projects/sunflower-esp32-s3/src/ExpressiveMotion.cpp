#include "ExpressiveMotion.h"

#include <Arduino.h>

#include "AudioAnalyzer.h"
#include "Config.h"
#include "MotorBehavior.h"
#include "MotorDriver.h"
#include "MotorPowerGuard.h"

namespace {

ExpressiveMotionMode mode = ExpressiveMotionMode::OFF;
ExpressiveMotionPhase phase = ExpressiveMotionPhase::IDLE;
unsigned long phaseStartMs = 0;

// --- IDLE_ALIVE timer ---
unsigned long idleDeadlineMs = 0;
bool idleDeadlineSet = false;
int8_t motionDirection = 0;  // +1 forward, -1 reverse, 0 = none chosen yet
uint8_t consecutiveSameDirCount = 0;

// --- Pattern-step engine, shared by ordinary movement and motion demo ---
// A pattern is a small, fixed sequence of steps; MOVE_RANDOM picks a fresh
// direction (respecting the consecutive-same-direction cap), MOVE_OPPOSITE
// always reverses relative to the last chosen direction (a deliberate,
// cap-respecting reversal -- it always resets the same-direction streak),
// STOP calls motorStop() and simply waits. Every step's duration is rolled
// once, from its [minMs,maxMs) tier, when the step begins.
enum class StepAction { MOVE_RANDOM, MOVE_OPPOSITE, STOP };
struct PatternStep {
  StepAction action;
  uint32_t minMs;
  uint32_t maxMs;
};

ExpressivePattern currentPattern = ExpressivePattern::NONE;
ExpressivePattern lastSelectedPattern = ExpressivePattern::NONE;
uint8_t patternStepIndex = 0;
uint32_t currentStepDurationMs = 0;

// Defensive backstop (see MOTION_MAX_ENERGIZED_MS in Config.h): sourced
// from millis() when the motor is actually energized, 0 otherwise --
// shared by ordinary movement and the demo, since only one of the two can
// ever be energizing the motor at a time (mutual exclusion between them
// is enforced elsewhere in this file).
unsigned long energizedSinceMs = 0;

// --- AUDIO_REACTIVE band tracking ---
AudioActivityBand audioBand = AudioActivityBand::QUIET;
unsigned long lastActiveTriggerMs = 0;
unsigned long lastStrongTriggerMs = 0;
unsigned long lastClapTriggerMs = 0;
unsigned long lastAnyAudioTriggerMs = 0;  // for SETTLE eligibility -- any of the three triggers above
bool audioCooldownInitialized = false;

// --- Speech dynamics: bounded window + counter, no queue/heap ---
unsigned long recentActiveWindowStartMs = 0;
uint8_t recentActiveTriggerCount = 0;

// --- motion demo ---
MotionDemoPhase demoPhase = MotionDemoPhase::IDLE;
unsigned long demoPhaseStartMs = 0;
uint8_t demoPatternIndex = 0;
uint8_t demoStepIndex = 0;
uint32_t demoStepDurationMs = 0;
ExpressiveMotionMode demoSavedMode = ExpressiveMotionMode::OFF;

constexpr ExpressivePattern DEMO_SEQUENCE[] = {
    ExpressivePattern::GENTLE_SWAY,      ExpressivePattern::MEDIUM_SWAY,     ExpressivePattern::DOUBLE_TWITCH,
    ExpressivePattern::FORWARD_REVERSE_NOD, ExpressivePattern::EXCITED_TRIPLE, ExpressivePattern::DRAMATIC_SWEEP,
    ExpressivePattern::AUDIO_CLAP_RECOIL,
};
constexpr uint8_t DEMO_SEQUENCE_COUNT = sizeof(DEMO_SEQUENCE) / sizeof(DEMO_SEQUENCE[0]);

// Set by cancelExpressiveMotion(), cleared by any explicit
// setExpressiveMotionMode() call -- lets '?' distinguish "disabled because
// of emergency stop" from "user deliberately selected OFF".
bool emergencyStoppedFlag = false;

const char *modeName(ExpressiveMotionMode m) {
  switch (m) {
    case ExpressiveMotionMode::OFF: return "DISABLED";
    case ExpressiveMotionMode::IDLE_ALIVE: return "IDLE_ALIVE";
    case ExpressiveMotionMode::AUDIO_REACTIVE: return "AUDIO_REACTIVE";
  }
  return "UNKNOWN";
}

const char *phaseName(ExpressiveMotionPhase p) {
  switch (p) {
    case ExpressiveMotionPhase::IDLE: return "IDLE";
    case ExpressiveMotionPhase::PREPARING: return "PREPARING";
    case ExpressiveMotionPhase::MOVING: return "MOVING";
    case ExpressiveMotionPhase::STOPPING: return "STOPPING";
    case ExpressiveMotionPhase::RELEASING: return "RELEASING";
  }
  return "UNKNOWN";
}

const char *bandName(AudioActivityBand b) {
  switch (b) {
    case AudioActivityBand::QUIET: return "QUIET";
    case AudioActivityBand::ACTIVE: return "ACTIVE";
    case AudioActivityBand::STRONG: return "STRONG";
  }
  return "UNKNOWN";
}

const char *demoPhaseName(MotionDemoPhase p) {
  switch (p) {
    case MotionDemoPhase::IDLE: return "IDLE";
    case MotionDemoPhase::PREPARING: return "PREPARING";
    case MotionDemoPhase::RUNNING_PATTERN: return "RUNNING_PATTERN";
    case MotionDemoPhase::INTER_PATTERN_PAUSE: return "INTER_PATTERN_PAUSE";
    case MotionDemoPhase::RELEASING: return "RELEASING";
  }
  return "UNKNOWN";
}

const char *patternName(ExpressivePattern p) {
  switch (p) {
    case ExpressivePattern::NONE: return "NONE";
    case ExpressivePattern::GENTLE_SWAY: return "GENTLE_SWAY";
    case ExpressivePattern::MEDIUM_SWAY: return "MEDIUM_SWAY";
    case ExpressivePattern::LONG_LEAN: return "LONG_LEAN";
    case ExpressivePattern::DOUBLE_TWITCH: return "DOUBLE_TWITCH";
    case ExpressivePattern::FORWARD_REVERSE_NOD: return "FORWARD_REVERSE_NOD";
    case ExpressivePattern::EXCITED_TRIPLE: return "EXCITED_TRIPLE";
    case ExpressivePattern::DRAMATIC_SWEEP: return "DRAMATIC_SWEEP";
    case ExpressivePattern::AUDIO_ACTIVE_PULSE: return "AUDIO_ACTIVE_PULSE";
    case ExpressivePattern::AUDIO_STRONG_BURST: return "AUDIO_STRONG_BURST";
    case ExpressivePattern::AUDIO_CLAP_RECOIL: return "AUDIO_CLAP_RECOIL";
    case ExpressivePattern::SETTLE: return "SETTLE";
  }
  return "UNKNOWN";
}

uint32_t randomRange(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

bool rollChance(float probability) { return ((float)random(0, 10000) / 10000.0f) < probability; }

// Hysteresis band transition -- see Config.h's MOTION_AUDIO_*_ENTER/EXIT
// comment. Never chatters right at a single threshold since entering and
// leaving a band use different levels.
AudioActivityBand computeBand(float envelope, AudioActivityBand prevBand) {
  switch (prevBand) {
    case AudioActivityBand::QUIET:
      if (envelope >= MOTION_AUDIO_STRONG_ENTER) return AudioActivityBand::STRONG;
      if (envelope >= MOTION_AUDIO_ACTIVE_ENTER) return AudioActivityBand::ACTIVE;
      return AudioActivityBand::QUIET;
    case AudioActivityBand::ACTIVE:
      if (envelope >= MOTION_AUDIO_STRONG_ENTER) return AudioActivityBand::STRONG;
      if (envelope < MOTION_AUDIO_ACTIVE_EXIT) return AudioActivityBand::QUIET;
      return AudioActivityBand::ACTIVE;
    case AudioActivityBand::STRONG:
      if (envelope < MOTION_AUDIO_STRONG_EXIT) {
        return (envelope >= MOTION_AUDIO_ACTIVE_ENTER) ? AudioActivityBand::ACTIVE : AudioActivityBand::QUIET;
      }
      return AudioActivityBand::STRONG;
  }
  return AudioActivityBand::QUIET;
}

// ----------------------------------------------------------------------------
// Pattern step tables -- see docs/EXPRESSIVE_MOTION_DEVELOPMENT.md for the
// rationale behind each shape. All durations reference the shared tiers in
// Config.h rather than hardcoding numbers here, so tuning one tier retunes
// every pattern that uses it.
// ----------------------------------------------------------------------------
constexpr PatternStep GENTLE_SWAY_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
};
constexpr PatternStep MEDIUM_SWAY_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
};
constexpr PatternStep LONG_LEAN_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_DRAMATIC_PULSE_MIN_MS, MOTION_DRAMATIC_PULSE_MAX_MS},
};
constexpr PatternStep DOUBLE_TWITCH_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_OPPOSITE, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
};
constexpr PatternStep FORWARD_REVERSE_NOD_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_OPPOSITE, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
};
// 1. short forward-or-reverse pulse (random initial direction), 2. stop,
// 3. short opposite pulse, 4. stop, 5. medium pulse in a freshly-random
// direction, 6. stop (implicit, via release).
constexpr PatternStep EXCITED_TRIPLE_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_OPPOSITE, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_RANDOM, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
};
// 1. medium/long pulse one direction, 2. complete stop + short pause,
// 3. shorter recoil pulse the opposite direction, 4. complete stop +
// extended rest before releasing -- the strongest idle pattern.
constexpr PatternStep DRAMATIC_SWEEP_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_DRAMATIC_PULSE_MIN_MS, MOTION_DRAMATIC_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_OPPOSITE, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_EXTENDED_PAUSE_MIN_MS, MOTION_EXTENDED_PAUSE_MAX_MS},
};
constexpr PatternStep AUDIO_ACTIVE_PULSE_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
};
// Two-pulse alternating burst -- noticeably stronger than DOUBLE_TWITCH
// (dramatic tier vs. gentle). STRONG events may also select EXCITED_TRIPLE
// instead, satisfying "two- or three-pulse" without a near-duplicate table.
constexpr PatternStep AUDIO_STRONG_BURST_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_DRAMATIC_PULSE_MIN_MS, MOTION_DRAMATIC_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_OPPOSITE, MOTION_MEDIUM_PULSE_MIN_MS, MOTION_MEDIUM_PULSE_MAX_MS},
};
// Sharp pulse, stop, short pause, shorter opposite-direction recoil, stop.
constexpr PatternStep AUDIO_CLAP_RECOIL_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_DRAMATIC_PULSE_MIN_MS, MOTION_DRAMATIC_PULSE_MAX_MS},
    {StepAction::STOP, MOTION_MICRO_PAUSE_MIN_MS, MOTION_MICRO_PAUSE_MAX_MS},
    {StepAction::MOVE_OPPOSITE, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
};
constexpr PatternStep SETTLE_STEPS[] = {
    {StepAction::MOVE_RANDOM, MOTION_GENTLE_PULSE_MIN_MS, MOTION_GENTLE_PULSE_MAX_MS},
};

const PatternStep *stepsFor(ExpressivePattern pattern, uint8_t &count) {
  switch (pattern) {
    case ExpressivePattern::GENTLE_SWAY:
      count = sizeof(GENTLE_SWAY_STEPS) / sizeof(GENTLE_SWAY_STEPS[0]);
      return GENTLE_SWAY_STEPS;
    case ExpressivePattern::MEDIUM_SWAY:
      count = sizeof(MEDIUM_SWAY_STEPS) / sizeof(MEDIUM_SWAY_STEPS[0]);
      return MEDIUM_SWAY_STEPS;
    case ExpressivePattern::LONG_LEAN:
      count = sizeof(LONG_LEAN_STEPS) / sizeof(LONG_LEAN_STEPS[0]);
      return LONG_LEAN_STEPS;
    case ExpressivePattern::DOUBLE_TWITCH:
      count = sizeof(DOUBLE_TWITCH_STEPS) / sizeof(DOUBLE_TWITCH_STEPS[0]);
      return DOUBLE_TWITCH_STEPS;
    case ExpressivePattern::FORWARD_REVERSE_NOD:
      count = sizeof(FORWARD_REVERSE_NOD_STEPS) / sizeof(FORWARD_REVERSE_NOD_STEPS[0]);
      return FORWARD_REVERSE_NOD_STEPS;
    case ExpressivePattern::EXCITED_TRIPLE:
      count = sizeof(EXCITED_TRIPLE_STEPS) / sizeof(EXCITED_TRIPLE_STEPS[0]);
      return EXCITED_TRIPLE_STEPS;
    case ExpressivePattern::DRAMATIC_SWEEP:
      count = sizeof(DRAMATIC_SWEEP_STEPS) / sizeof(DRAMATIC_SWEEP_STEPS[0]);
      return DRAMATIC_SWEEP_STEPS;
    case ExpressivePattern::AUDIO_ACTIVE_PULSE:
      count = sizeof(AUDIO_ACTIVE_PULSE_STEPS) / sizeof(AUDIO_ACTIVE_PULSE_STEPS[0]);
      return AUDIO_ACTIVE_PULSE_STEPS;
    case ExpressivePattern::AUDIO_STRONG_BURST:
      count = sizeof(AUDIO_STRONG_BURST_STEPS) / sizeof(AUDIO_STRONG_BURST_STEPS[0]);
      return AUDIO_STRONG_BURST_STEPS;
    case ExpressivePattern::AUDIO_CLAP_RECOIL:
      count = sizeof(AUDIO_CLAP_RECOIL_STEPS) / sizeof(AUDIO_CLAP_RECOIL_STEPS[0]);
      return AUDIO_CLAP_RECOIL_STEPS;
    case ExpressivePattern::SETTLE:
      count = sizeof(SETTLE_STEPS) / sizeof(SETTLE_STEPS[0]);
      return SETTLE_STEPS;
    case ExpressivePattern::NONE:
      break;
  }
  count = 0;
  return nullptr;
}

// Weighted pick among the seven idle-eligible patterns -- see Config.h's
// MOTION_WEIGHT_* (sums to 1.0).
struct PatternWeight {
  ExpressivePattern pattern;
  float weight;
};
constexpr PatternWeight IDLE_PATTERN_WEIGHTS[] = {
    {ExpressivePattern::GENTLE_SWAY, MOTION_WEIGHT_GENTLE_SWAY},
    {ExpressivePattern::MEDIUM_SWAY, MOTION_WEIGHT_MEDIUM_SWAY},
    {ExpressivePattern::LONG_LEAN, MOTION_WEIGHT_LONG_LEAN},
    {ExpressivePattern::DOUBLE_TWITCH, MOTION_WEIGHT_DOUBLE_TWITCH},
    {ExpressivePattern::FORWARD_REVERSE_NOD, MOTION_WEIGHT_FORWARD_REVERSE_NOD},
    {ExpressivePattern::EXCITED_TRIPLE, MOTION_WEIGHT_EXCITED_TRIPLE},
    {ExpressivePattern::DRAMATIC_SWEEP, MOTION_WEIGHT_DRAMATIC_SWEEP},
};
constexpr uint8_t IDLE_PATTERN_WEIGHT_COUNT = sizeof(IDLE_PATTERN_WEIGHTS) / sizeof(IDLE_PATTERN_WEIGHTS[0]);

ExpressivePattern pickWeightedIdlePattern() {
  float r = (float)random(0, 10000) / 10000.0f;
  float cumulative = 0.0f;
  for (uint8_t i = 0; i < IDLE_PATTERN_WEIGHT_COUNT; i++) {
    cumulative += IDLE_PATTERN_WEIGHTS[i].weight;
    if (r < cumulative) return IDLE_PATTERN_WEIGHTS[i].pattern;
  }
  return IDLE_PATTERN_WEIGHTS[0].pattern;  // float-rounding fallback
}

void armIdleDeadline(unsigned long now) {
  uint32_t restMs = rollChance(MOTION_LONG_REST_CHANCE) ? randomRange(MOTION_LONG_REST_MIN_MS, MOTION_LONG_REST_MAX_MS)
                                                         : randomRange(MOTION_REST_MIN_MS, MOTION_REST_MAX_MS);
  idleDeadlineMs = now + restMs;
  idleDeadlineSet = true;
}

// "no more than MOTION_MAX_CONSECUTIVE_SAME_DIR pulses in the same
// direction" -- forces a switch once the cap is hit, otherwise picks
// randomly. This is the ONLY place a streak can grow; MOVE_OPPOSITE always
// resets it (see chooseOppositeDirection() below), so the cap is enforced
// globally across pattern and mode boundaries, not just within one pattern.
void chooseDirection() {
  int8_t dir;
  if (consecutiveSameDirCount >= MOTION_MAX_CONSECUTIVE_SAME_DIR && motionDirection != 0) {
    dir = (int8_t)-motionDirection;
  } else {
    dir = (random(0, 2) == 0) ? (int8_t)1 : (int8_t)-1;
  }
  consecutiveSameDirCount = (dir == motionDirection) ? (uint8_t)(consecutiveSameDirCount + 1) : (uint8_t)1;
  motionDirection = dir;
}

// A deliberate, cap-respecting reversal -- always breaks any same-direction
// streak (the new direction is by definition never the same as before).
void chooseOppositeDirection() {
  motionDirection = (motionDirection > 0) ? (int8_t)-1 : (int8_t)1;  // 0 (unset) treated as "was forward"
  consecutiveSameDirCount = 1;
}

void driveChosenDirection(unsigned long now) {
  if (motionDirection >= 0) motorForward();
  else motorReverse();
  energizedSinceMs = now;
}

void stopMotor() {
  motorStop();
  energizedSinceMs = 0;
}

// Begins one pattern: requests DIM_DURING_MOTION motor power at the
// already-selected/validated test brightness (see MotorPowerGuard.h) and
// transitions to PREPARING. Caller must already have verified
// !isAnyMotorDiagnosticActive() and getMotorBehavior()==OFF.
void beginPattern(ExpressivePattern pattern, unsigned long now) {
  currentPattern = pattern;
  lastSelectedPattern = pattern;
  patternStepIndex = 0;
  setMotorLedPowerMode(MotorLedPowerMode::DIM_DURING_MOTION);
  requestMotorPower();
  phase = ExpressiveMotionPhase::PREPARING;
  phaseStartMs = now;
}

// Executes patternStepIndex of currentPattern, or releases if the pattern
// is complete. Shared by ordinary movement only (the demo has its own
// near-identical enterDemoStep(), since it tracks a separate phase enum
// and must additionally know when to move on to the *next* pattern).
void enterOrdinaryPatternStep(unsigned long now) {
  uint8_t count;
  const PatternStep *steps = stepsFor(currentPattern, count);
  if (!steps || patternStepIndex >= count) {
    releaseMotorPower();
    phase = ExpressiveMotionPhase::RELEASING;
    phaseStartMs = now;
    return;
  }
  const PatternStep &step = steps[patternStepIndex];
  currentStepDurationMs = randomRange(step.minMs, step.maxMs);
  if (step.action == StepAction::STOP) {
    stopMotor();
    phase = ExpressiveMotionPhase::STOPPING;
  } else {
    if (step.action == StepAction::MOVE_RANDOM) chooseDirection();
    else chooseOppositeDirection();
    driveChosenDirection(now);
    phase = ExpressiveMotionPhase::MOVING;
  }
  phaseStartMs = now;
}

// Shared by pauseExpressiveMotion()/cancelExpressiveMotion()/
// setExpressiveMotionMode(): stops the motor and releases MotorPowerGuard
// immediately if a pattern is in flight, returning to a safe idle state.
// Idempotent. Also restores MotorLedPowerMode to FULL_MUTE -- '2'/'3' rely
// on that being the standing default and never set it themselves (see
// MotorPowerGuard.h); leaving DIM_DURING_MOTION selected after an
// interrupted pattern would silently change their LED behavior too.
void forceReleaseOrdinaryMovement() {
  if (phase != ExpressiveMotionPhase::IDLE) {
    stopMotor();
    releaseMotorPowerImmediately();
    phase = ExpressiveMotionPhase::IDLE;
    currentPattern = ExpressivePattern::NONE;
  }
  setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);
  idleDeadlineSet = false;
}

void forceReleaseDemo() {
  if (demoPhase != MotionDemoPhase::IDLE) {
    stopMotor();
    releaseMotorPowerImmediately();
    demoPhase = MotionDemoPhase::IDLE;
  }
  setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);  // see forceReleaseOrdinaryMovement()'s comment
}

// Executes demoStepIndex of DEMO_SEQUENCE[demoPatternIndex]; on that
// pattern's completion, either pauses before the next pattern or (if this
// was the last one) releases. Mirrors enterOrdinaryPatternStep() but
// additionally advances demoPatternIndex/prints per-pattern announcements.
void enterDemoStep(unsigned long now) {
  uint8_t count;
  const PatternStep *steps = stepsFor(DEMO_SEQUENCE[demoPatternIndex], count);
  if (!steps || demoStepIndex >= count) {
    stopMotor();
    demoPatternIndex++;
    if (demoPatternIndex >= DEMO_SEQUENCE_COUNT) {
      Serial.println(F("[MOTION DEMO] Stop"));
      releaseMotorPower();
      demoPhase = MotionDemoPhase::RELEASING;
      demoPhaseStartMs = now;
    } else {
      demoStepDurationMs = randomRange(MOTION_DEMO_INTER_PATTERN_PAUSE_MIN_MS, MOTION_DEMO_INTER_PATTERN_PAUSE_MAX_MS);
      demoPhase = MotionDemoPhase::INTER_PATTERN_PAUSE;
      demoPhaseStartMs = now;
    }
    return;
  }
  const PatternStep &step = steps[demoStepIndex];
  demoStepDurationMs = randomRange(step.minMs, step.maxMs);
  if (step.action == StepAction::STOP) {
    stopMotor();
  } else {
    if (step.action == StepAction::MOVE_RANDOM) chooseDirection();
    else chooseOppositeDirection();
    driveChosenDirection(now);
  }
  demoPhase = MotionDemoPhase::RUNNING_PATTERN;
  demoPhaseStartMs = now;
}

void updateMotionDemo(unsigned long now) {
  if (demoPhase == MotionDemoPhase::IDLE) return;
  unsigned long elapsed = now - demoPhaseStartMs;

  switch (demoPhase) {
    case MotionDemoPhase::IDLE:
      break;
    case MotionDemoPhase::PREPARING:
      if (isMotorPowerReady()) {
        demoPatternIndex = 0;
        demoStepIndex = 0;
        Serial.printf("[MOTION DEMO] Pattern: %s\n", patternName(DEMO_SEQUENCE[0]));
        enterDemoStep(now);
      }
      break;
    case MotionDemoPhase::RUNNING_PATTERN:
      if (elapsed >= demoStepDurationMs) {
        demoStepIndex++;
        enterDemoStep(now);
      }
      break;
    case MotionDemoPhase::INTER_PATTERN_PAUSE:
      if (elapsed >= demoStepDurationMs) {
        demoStepIndex = 0;
        Serial.printf("[MOTION DEMO] Pattern: %s\n", patternName(DEMO_SEQUENCE[demoPatternIndex]));
        enterDemoStep(now);
      }
      break;
    case MotionDemoPhase::RELEASING:
      if (getMotorPowerGuardState() == MotorPowerGuardState::IDLE) {
        setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);  // restore the safe default, matches the '5' test
        mode = demoSavedMode;                                // restore previous expressive mode
        if (mode != ExpressiveMotionMode::OFF) idleDeadlineSet = false;  // fresh timing on resume
        Serial.println(F("[MOTION DEMO] Complete"));
        demoPhase = MotionDemoPhase::IDLE;
      }
      break;
  }
}

}  // namespace

void initExpressiveMotion() {
  mode = ExpressiveMotionMode::OFF;
  phase = ExpressiveMotionPhase::IDLE;
  idleDeadlineSet = false;
  motionDirection = 0;
  consecutiveSameDirCount = 0;
  currentPattern = ExpressivePattern::NONE;
  lastSelectedPattern = ExpressivePattern::NONE;
  patternStepIndex = 0;
  currentStepDurationMs = 0;
  energizedSinceMs = 0;
  audioBand = AudioActivityBand::QUIET;
  lastActiveTriggerMs = 0;
  lastStrongTriggerMs = 0;
  lastClapTriggerMs = 0;
  lastAnyAudioTriggerMs = 0;
  audioCooldownInitialized = false;
  recentActiveWindowStartMs = 0;
  recentActiveTriggerCount = 0;
  demoPhase = MotionDemoPhase::IDLE;
  demoPatternIndex = 0;
  demoStepIndex = 0;
  demoStepDurationMs = 0;
  emergencyStoppedFlag = false;
}

void updateExpressiveMotion(unsigned long now) {
  // Defensive backstop: no single continuous energized segment may exceed
  // MOTION_MAX_ENERGIZED_MS, regardless of what a pattern's rolled step
  // duration works out to (should never trigger given the tiers in
  // Config.h -- see their comments -- but guards against a future
  // misconfiguration, mirroring main.cpp's MOTOR BREAKAWAY TEST backstop).
  if (energizedSinceMs != 0 && (now - energizedSinceMs) >= MOTION_MAX_ENERGIZED_MS) {
    Serial.println(F("[MOTION] Safety: max energized runtime exceeded -- stopping"));
    forceReleaseDemo();
    forceReleaseOrdinaryMovement();
    return;
  }

  // Motion demo takes exclusive priority -- ordinary movement is on hold
  // for its duration (startMotionDemo() already paused it).
  updateMotionDemo(now);
  if (demoPhase != MotionDemoPhase::IDLE) return;

  if (mode == ExpressiveMotionMode::OFF) return;

  bool audioTriggerClap = false;
  bool audioTriggerStrong = false;
  bool audioTriggerActive = false;
  if (mode == ExpressiveMotionMode::AUDIO_REACTIVE) {
    const AudioFeatures &f = getAudioFeatures();
    AudioActivityBand prevBand = audioBand;
    audioBand = computeBand(f.envelope, prevBand);

    bool risingIntoActive = (prevBand == AudioActivityBand::QUIET && audioBand != AudioActivityBand::QUIET);
    bool risingIntoStrong = (prevBand != AudioActivityBand::STRONG && audioBand == AudioActivityBand::STRONG);

    if (!audioCooldownInitialized) {
      lastActiveTriggerMs = now - MOTION_AUDIO_ACTIVE_COOLDOWN_MS;
      lastStrongTriggerMs = now - MOTION_AUDIO_STRONG_COOLDOWN_MS;
      lastClapTriggerMs = now - MOTION_AUDIO_CLAP_COOLDOWN_MS;
      audioCooldownInitialized = true;
    }

    // Priority clap > STRONG > ACTIVE if more than one condition is true
    // in the same window. Each has its own independent cooldown -- a clap
    // reaction doesn't consume or reset the STRONG cooldown and vice
    // versa. All three only ever fire on a rising EDGE (or the clap
    // edge itself), never a held level, so a sustained loud passage
    // produces intermittent movement, never continuous energizing.
    if (f.clap && phase == ExpressiveMotionPhase::IDLE && (now - lastClapTriggerMs) >= MOTION_AUDIO_CLAP_COOLDOWN_MS) {
      audioTriggerClap = true;
      lastClapTriggerMs = now;
    } else if (risingIntoStrong && phase == ExpressiveMotionPhase::IDLE &&
               (now - lastStrongTriggerMs) >= MOTION_AUDIO_STRONG_COOLDOWN_MS) {
      audioTriggerStrong = true;
      lastStrongTriggerMs = now;
    } else if (risingIntoActive && phase == ExpressiveMotionPhase::IDLE &&
               (now - lastActiveTriggerMs) >= MOTION_AUDIO_ACTIVE_COOLDOWN_MS) {
      audioTriggerActive = true;
      lastActiveTriggerMs = now;
    }
    if (audioTriggerClap || audioTriggerStrong || audioTriggerActive) lastAnyAudioTriggerMs = now;
    // QUIET (no trigger this frame): falls through to the same idle-timer
    // logic IDLE_ALIVE uses below -- deliberately no separate code path.
  }

  switch (phase) {
    case ExpressiveMotionPhase::IDLE: {
      if (!idleDeadlineSet) armIdleDeadline(now);

      bool timerElapsed = (long)(now - idleDeadlineMs) >= 0;
      if (!(audioTriggerClap || audioTriggerStrong || audioTriggerActive || timerElapsed)) return;

      // Never fight IDLE_SWAY or an active diagnostic for the motor --
      // defense in depth on top of main.cpp's pauseExpressiveMotion() gate.
      if (isAnyMotorDiagnosticActive() || getMotorBehavior() != MotorBehaviorMode::OFF) return;

      ExpressivePattern selected;
      if (audioTriggerClap) {
        selected = ExpressivePattern::AUDIO_CLAP_RECOIL;
      } else if (audioTriggerStrong) {
        // "two- or three-pulse alternating burst"
        selected = rollChance(0.5f) ? ExpressivePattern::AUDIO_STRONG_BURST : ExpressivePattern::EXCITED_TRIPLE;
      } else if (audioTriggerActive) {
        // Speech dynamics: a burst of ACTIVE events within a bounded
        // window occasionally gets a livelier grouped pattern instead of
        // identical single pulses -- fixed-size counter, resets on window
        // expiry or once it actually groups, so it can never accumulate
        // an unbounded backlog.
        if (now - recentActiveWindowStartMs > MOTION_SPEECH_WINDOW_MS) {
          recentActiveWindowStartMs = now;
          recentActiveTriggerCount = 0;
        }
        recentActiveTriggerCount++;
        if (recentActiveTriggerCount >= MOTION_SPEECH_GROUP_THRESHOLD && rollChance(MOTION_SPEECH_GROUP_CHANCE)) {
          recentActiveTriggerCount = 0;
          selected = rollChance(0.5f) ? ExpressivePattern::EXCITED_TRIPLE : ExpressivePattern::DOUBLE_TWITCH;
        } else {
          selected = rollChance(MOTION_ACTIVE_NOD_CHANCE) ? ExpressivePattern::FORWARD_REVERSE_NOD
                                                           : ExpressivePattern::AUDIO_ACTIVE_PULSE;
        }
      } else {
        // Ordinary idle tick -- also what AUDIO_REACTIVE uses while QUIET.
        bool recentActivity =
            (lastAnyAudioTriggerMs != 0) && (now - lastAnyAudioTriggerMs) < MOTION_SETTLE_RECENT_ACTIVITY_MS;
        selected = (recentActivity && rollChance(MOTION_SETTLE_CHANCE)) ? ExpressivePattern::SETTLE
                                                                         : pickWeightedIdlePattern();
      }

      beginPattern(selected, now);
      break;
    }
    case ExpressiveMotionPhase::PREPARING:
      if (isMotorPowerReady()) enterOrdinaryPatternStep(now);
      break;
    case ExpressiveMotionPhase::MOVING:
    case ExpressiveMotionPhase::STOPPING:
      if (now - phaseStartMs >= currentStepDurationMs) {
        patternStepIndex++;
        enterOrdinaryPatternStep(now);
      }
      break;
    case ExpressiveMotionPhase::RELEASING:
      if (getMotorPowerGuardState() == MotorPowerGuardState::IDLE) {
        // Restore the safe FULL_MUTE default between patterns -- see
        // forceReleaseOrdinaryMovement()'s comment on why '2'/'3' need
        // this. beginPattern() re-selects DIM_DURING_MOTION before the
        // next pattern, so this only costs a redundant mode set, never a
        // behavior change to ordinary expressive movement itself.
        setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);
        phase = ExpressiveMotionPhase::IDLE;
        currentPattern = ExpressivePattern::NONE;
        idleDeadlineSet = false;  // re-rolled on the next IDLE tick
      }
      break;
  }
}

void cycleExpressiveMotionMode() {
  ExpressiveMotionMode next = ExpressiveMotionMode::OFF;
  switch (mode) {
    case ExpressiveMotionMode::OFF: next = ExpressiveMotionMode::IDLE_ALIVE; break;
    case ExpressiveMotionMode::IDLE_ALIVE: next = ExpressiveMotionMode::AUDIO_REACTIVE; break;
    case ExpressiveMotionMode::AUDIO_REACTIVE: next = ExpressiveMotionMode::OFF; break;
  }
  setExpressiveMotionMode(next);
}

void setExpressiveMotionMode(ExpressiveMotionMode newMode) {
  emergencyStoppedFlag = false;  // any explicit call is a deliberate action -- clears the emergency-inhibit flag
  if (newMode == mode) {
    Serial.printf("[MOTION] %s (unchanged)\n", modeName(newMode));
    return;
  }
  ExpressiveMotionMode oldMode = mode;
  mode = newMode;
  // Cancel any in-flight pattern safely on every transition (including
  // directly between IDLE_ALIVE and AUDIO_REACTIVE) rather than letting it
  // finish under the old mode, and reset idle timing fresh for the new one.
  forceReleaseOrdinaryMovement();
  if (oldMode == ExpressiveMotionMode::OFF && newMode != ExpressiveMotionMode::OFF) {
    // Freshly enabling -- ensure IDLE_SWAY isn't concurrently driving the
    // motor, matching every existing motor diagnostic's own guard.
    setMotorBehavior(MotorBehaviorMode::OFF);
  }
  Serial.printf("[MOTION] %s\n", modeName(newMode));
}

ExpressiveMotionMode getExpressiveMotionMode() { return mode; }

bool isExpressiveMotionMoving() { return phase != ExpressiveMotionPhase::IDLE || demoPhase != MotionDemoPhase::IDLE; }

void pauseExpressiveMotion() {
  forceReleaseDemo();
  forceReleaseOrdinaryMovement();
}

void cancelExpressiveMotion() {
  bool wasActive = isExpressiveMotionMoving() || mode != ExpressiveMotionMode::OFF;
  forceReleaseDemo();
  forceReleaseOrdinaryMovement();
  mode = ExpressiveMotionMode::OFF;  // must stay inhibited until explicitly re-enabled
  emergencyStoppedFlag = true;
  if (wasActive) Serial.println(F("[MOTION] Emergency stop -- DISABLED"));
}

void startMotionDemo() {
  if (isMotionDemoActive()) return;
  if (isAnyMotorDiagnosticActive()) return;
  forceReleaseOrdinaryMovement();  // pause ordinary movement first, if any was mid-pattern
  demoSavedMode = mode;
  Serial.println(F("[MOTION DEMO] Preparing"));
  setMotorLedPowerMode(MotorLedPowerMode::DIM_DURING_MOTION);
  requestMotorPower();
  demoPhase = MotionDemoPhase::PREPARING;
  demoPhaseStartMs = millis();
}

bool isMotionDemoActive() { return demoPhase != MotionDemoPhase::IDLE; }

void printExpressiveMotionDebugState() {
  unsigned long now = millis();
  long msUntilNext = idleDeadlineSet ? (long)(idleDeadlineMs - now) : 0;
  if (msUntilNext < 0) msUntilNext = 0;

  long activeCooldownLeft = (long)MOTION_AUDIO_ACTIVE_COOLDOWN_MS - (long)(now - lastActiveTriggerMs);
  long strongCooldownLeft = (long)MOTION_AUDIO_STRONG_COOLDOWN_MS - (long)(now - lastStrongTriggerMs);
  long clapCooldownLeft = (long)MOTION_AUDIO_CLAP_COOLDOWN_MS - (long)(now - lastClapTriggerMs);
  if (activeCooldownLeft < 0) activeCooldownLeft = 0;
  if (strongCooldownLeft < 0) strongCooldownLeft = 0;
  if (clapCooldownLeft < 0) clapCooldownLeft = 0;

  uint8_t stepCount = 0;
  stepsFor(currentPattern, stepCount);
  long stepRemainingMs = 0;
  if (phase == ExpressiveMotionPhase::MOVING || phase == ExpressiveMotionPhase::STOPPING) {
    stepRemainingMs = (long)currentStepDurationMs - (long)(now - phaseStartMs);
    if (stepRemainingMs < 0) stepRemainingMs = 0;
  }

  Serial.printf(
      "[MOTION] mode=%s phase=%s pattern=%s step=%u/%u stepRemainingMs=%ld direction=%s "
      "msUntilNextMovement=%ld audioBand=%s activeCooldownRemainingMs=%ld strongCooldownRemainingMs=%ld "
      "clapCooldownRemainingMs=%ld lastPattern=%s demoActive=%d demoPhase=%s powerGuardState=%d "
      "dimBrightnessRaw=%u emergencyStopped=%d\n",
      modeName(mode), phaseName(phase), patternName(currentPattern), (unsigned)(patternStepIndex + 1),
      (unsigned)stepCount, stepRemainingMs, motionDirection > 0 ? "FORWARD" : (motionDirection < 0 ? "REVERSE" : "NONE"),
      msUntilNext, bandName(audioBand), activeCooldownLeft, strongCooldownLeft, clapCooldownLeft,
      patternName(lastSelectedPattern), isMotionDemoActive() ? 1 : 0, demoPhaseName(demoPhase),
      (int)getMotorPowerGuardState(), getMotorDimBrightnessRaw(), emergencyStoppedFlag ? 1 : 0);
}
