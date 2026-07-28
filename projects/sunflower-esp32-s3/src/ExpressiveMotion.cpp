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

// --- IDLE_ALIVE timer + curious-double-movement bookkeeping ---
unsigned long idleDeadlineMs = 0;
bool idleDeadlineSet = false;
bool curiousPending = false;
bool curiousSecondPulseDone = false;
int8_t motionDirection = 0;  // +1 forward, -1 reverse, 0 = none chosen yet
uint8_t consecutiveSameDirCount = 0;
uint32_t currentPulseMs = 0;

// --- AUDIO_REACTIVE band tracking ---
AudioActivityBand audioBand = AudioActivityBand::QUIET;
unsigned long lastActiveTriggerMs = 0;
unsigned long lastStrongTriggerMs = 0;
bool audioCooldownInitialized = false;

// --- motion demo ---
MotionDemoPhase demoPhase = MotionDemoPhase::IDLE;
unsigned long demoPhaseStartMs = 0;
uint8_t demoCuriousStep = 0;
ExpressiveMotionMode demoSavedMode = ExpressiveMotionMode::OFF;
constexpr uint32_t DEMO_PULSE_MS = 200;  // within MOTION_PULSE_MIN_MS..MOTION_PULSE_MAX_MS
constexpr uint32_t DEMO_STOP_MS = 300;

// Set by cancelExpressiveMotion(), cleared by any explicit
// setExpressiveMotionMode() call -- lets '?' distinguish "disabled because
// of emergency stop" from "user deliberately selected DISABLED/off".
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
    case MotionDemoPhase::FORWARD: return "FORWARD";
    case MotionDemoPhase::STOP1: return "STOP1";
    case MotionDemoPhase::REVERSE: return "REVERSE";
    case MotionDemoPhase::STOP2: return "STOP2";
    case MotionDemoPhase::CURIOUS_PULSE: return "CURIOUS_PULSE";
    case MotionDemoPhase::CURIOUS_STOP: return "CURIOUS_STOP";
    case MotionDemoPhase::RELEASING: return "RELEASING";
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

void armIdleDeadline(unsigned long now) {
  uint32_t restMs = rollChance(MOTION_LONG_REST_CHANCE) ? randomRange(MOTION_LONG_REST_MIN_MS, MOTION_LONG_REST_MAX_MS)
                                                         : randomRange(MOTION_REST_MIN_MS, MOTION_REST_MAX_MS);
  idleDeadlineMs = now + restMs;
  idleDeadlineSet = true;
  curiousSecondPulseDone = false;
}

// "no more than MOTION_MAX_CONSECUTIVE_SAME_DIR pulses in the same
// direction" -- forces a switch once the cap is hit, otherwise picks
// randomly. Used for both the first pulse of an idle cycle and a curious
// double-movement's second pulse.
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

void driveChosenDirection() {
  if (motionDirection >= 0) motorForward();
  else motorReverse();
}

// Begins one movement pulse: requests DIM_DURING_MOTION motor power at the
// already-selected/validated test brightness (see MotorPowerGuard.h) and
// transitions to PREPARING. Caller must already have verified
// !isAnyMotorDiagnosticActive() and getMotorBehavior()==OFF.
void beginPulse(unsigned long now, uint32_t pulseMs) {
  currentPulseMs = pulseMs;
  setMotorLedPowerMode(MotorLedPowerMode::DIM_DURING_MOTION);
  requestMotorPower();
  phase = ExpressiveMotionPhase::PREPARING;
  phaseStartMs = now;
}

// Shared by pauseExpressiveMotion()/cancelExpressiveMotion(): stops the
// motor and releases MotorPowerGuard immediately if a pulse is in flight,
// returning to a safe idle state. Idempotent. Also restores
// MotorLedPowerMode to FULL_MUTE -- '2'/'3' rely on that being the
// standing default and never set it themselves (see MotorPowerGuard.h);
// leaving DIM_DURING_MOTION selected after an interrupted pulse would
// silently change their LED behavior too.
void forceReleaseOrdinaryMovement() {
  if (phase != ExpressiveMotionPhase::IDLE) {
    motorStop();
    releaseMotorPowerImmediately();
    phase = ExpressiveMotionPhase::IDLE;
  }
  setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);
  idleDeadlineSet = false;
}

void forceReleaseDemo() {
  if (demoPhase != MotionDemoPhase::IDLE) {
    motorStop();
    releaseMotorPowerImmediately();
    demoPhase = MotionDemoPhase::IDLE;
  }
  setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);  // see forceReleaseOrdinaryMovement()'s comment
}

void updateMotionDemo(unsigned long now) {
  if (demoPhase == MotionDemoPhase::IDLE) return;
  unsigned long elapsed = now - demoPhaseStartMs;

  switch (demoPhase) {
    case MotionDemoPhase::IDLE:
      break;
    case MotionDemoPhase::PREPARING:
      if (isMotorPowerReady()) {
        Serial.println(F("[MOTION DEMO] Gentle forward"));
        motorForward();
        demoPhase = MotionDemoPhase::FORWARD;
        demoPhaseStartMs = now;
      }
      break;
    case MotionDemoPhase::FORWARD:
      if (elapsed >= DEMO_PULSE_MS) {
        motorStop();
        Serial.println(F("[MOTION DEMO] Stop"));
        demoPhase = MotionDemoPhase::STOP1;
        demoPhaseStartMs = now;
      }
      break;
    case MotionDemoPhase::STOP1:
      if (elapsed >= DEMO_STOP_MS) {
        Serial.println(F("[MOTION DEMO] Gentle reverse"));
        motorReverse();
        demoPhase = MotionDemoPhase::REVERSE;
        demoPhaseStartMs = now;
      }
      break;
    case MotionDemoPhase::REVERSE:
      if (elapsed >= DEMO_PULSE_MS) {
        motorStop();
        Serial.println(F("[MOTION DEMO] Stop"));
        demoPhase = MotionDemoPhase::STOP2;
        demoPhaseStartMs = now;
      }
      break;
    case MotionDemoPhase::STOP2:
      if (elapsed >= DEMO_STOP_MS) {
        Serial.println(F("[MOTION DEMO] Curious movement"));
        motorForward();
        demoCuriousStep = 0;
        demoPhase = MotionDemoPhase::CURIOUS_PULSE;
        demoPhaseStartMs = now;
      }
      break;
    case MotionDemoPhase::CURIOUS_PULSE:
      if (elapsed >= DEMO_PULSE_MS) {
        motorStop();
        demoPhase = MotionDemoPhase::CURIOUS_STOP;
        demoPhaseStartMs = now;
      }
      break;
    case MotionDemoPhase::CURIOUS_STOP:
      if (elapsed >= MOTION_INTRA_PULSE_STOP_MS) {
        if (demoCuriousStep == 0) {
          demoCuriousStep = 1;
          motorReverse();  // second curious pulse, opposite direction -- still a full stop between (above)
          demoPhase = MotionDemoPhase::CURIOUS_PULSE;
          demoPhaseStartMs = now;
        } else {
          Serial.println(F("[MOTION DEMO] Restoring"));
          releaseMotorPower();
          demoPhase = MotionDemoPhase::RELEASING;
          demoPhaseStartMs = now;
        }
      }
      break;
    case MotionDemoPhase::RELEASING:
      if (getMotorPowerGuardState() == MotorPowerGuardState::IDLE) {
        setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);  // restore the safe default, matches the '5' test
        mode = demoSavedMode;                                // restore previous expressive mode (step 6)
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
  curiousPending = false;
  curiousSecondPulseDone = false;
  motionDirection = 0;
  consecutiveSameDirCount = 0;
  audioBand = AudioActivityBand::QUIET;
  lastActiveTriggerMs = 0;
  lastStrongTriggerMs = 0;
  audioCooldownInitialized = false;
  demoPhase = MotionDemoPhase::IDLE;
  demoCuriousStep = 0;
  emergencyStoppedFlag = false;
}

void updateExpressiveMotion(unsigned long now) {
  // Motion demo takes exclusive priority -- ordinary movement is on hold
  // for its short duration (startMotionDemo() already paused it).
  updateMotionDemo(now);
  if (demoPhase != MotionDemoPhase::IDLE) return;

  if (mode == ExpressiveMotionMode::OFF) return;

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
      audioCooldownInitialized = true;
    }

    // STRONG (or a clap, regardless of band) takes priority over a mere
    // ACTIVE crossing; each tier has its own cooldown so a sustained loud
    // passage produces intermittent movement, not continuous energizing --
    // this only ever fires on a rising EDGE, never a held level.
    if ((risingIntoStrong || f.clap) && phase == ExpressiveMotionPhase::IDLE &&
        (now - lastStrongTriggerMs) >= MOTION_AUDIO_STRONG_COOLDOWN_MS) {
      audioTriggerStrong = true;
      lastStrongTriggerMs = now;
    } else if (risingIntoActive && phase == ExpressiveMotionPhase::IDLE &&
               (now - lastActiveTriggerMs) >= MOTION_AUDIO_ACTIVE_COOLDOWN_MS) {
      audioTriggerActive = true;
      lastActiveTriggerMs = now;
    }
    // QUIET (no trigger this frame): falls through to the same idle-timer
    // logic IDLE_ALIVE uses below -- deliberately no separate code path.
  }

  switch (phase) {
    case ExpressiveMotionPhase::IDLE: {
      if (!idleDeadlineSet) armIdleDeadline(now);

      bool timerElapsed = (long)(now - idleDeadlineMs) >= 0;
      if (!(audioTriggerStrong || audioTriggerActive || timerElapsed)) return;

      // Never fight IDLE_SWAY or an active diagnostic for the motor --
      // defense in depth on top of main.cpp's pauseExpressiveMotion() gate.
      if (isAnyMotorDiagnosticActive() || getMotorBehavior() != MotorBehaviorMode::OFF) return;

      curiousPending = audioTriggerStrong ? true : rollChance(MOTION_CURIOUS_CHANCE);
      curiousSecondPulseDone = false;

      chooseDirection();
      uint32_t pulseMs = randomRange(MOTION_PULSE_MIN_MS, MOTION_PULSE_MAX_MS);
      beginPulse(now, pulseMs);
      break;
    }
    case ExpressiveMotionPhase::PREPARING:
      if (isMotorPowerReady()) {
        driveChosenDirection();
        phase = ExpressiveMotionPhase::MOVING;
        phaseStartMs = now;
      }
      break;
    case ExpressiveMotionPhase::MOVING:
      if (now - phaseStartMs >= currentPulseMs) {
        motorStop();
        phase = ExpressiveMotionPhase::STOPPING;
        phaseStartMs = now;
      }
      break;
    case ExpressiveMotionPhase::STOPPING:
      if (now - phaseStartMs >= MOTION_INTRA_PULSE_STOP_MS) {
        if (curiousPending && !curiousSecondPulseDone) {
          curiousSecondPulseDone = true;
          chooseDirection();  // "occasionally move in the opposite direction" -- cap still applies
          currentPulseMs = randomRange(MOTION_PULSE_MIN_MS, MOTION_PULSE_MAX_MS);
          driveChosenDirection();
          phase = ExpressiveMotionPhase::MOVING;
          phaseStartMs = now;
        } else {
          releaseMotorPower();
          phase = ExpressiveMotionPhase::RELEASING;
          phaseStartMs = now;
        }
      }
      break;
    case ExpressiveMotionPhase::RELEASING:
      if (getMotorPowerGuardState() == MotorPowerGuardState::IDLE) {
        // Restore the safe FULL_MUTE default between pulses -- see
        // forceReleaseOrdinaryMovement()'s comment on why '2'/'3' need
        // this. beginPulse() re-selects DIM_DURING_MOTION before the next
        // pulse, so this only costs a redundant mode set, never a
        // behavior change to ordinary expressive movement itself.
        setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);
        phase = ExpressiveMotionPhase::IDLE;
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
  if (newMode == ExpressiveMotionMode::OFF) {
    forceReleaseOrdinaryMovement();
  } else if (oldMode == ExpressiveMotionMode::OFF) {
    // Freshly enabling -- ensure IDLE_SWAY isn't concurrently driving the
    // motor, matching every existing motor diagnostic's own guard.
    setMotorBehavior(MotorBehaviorMode::OFF);
    idleDeadlineSet = false;
  } else {
    idleDeadlineSet = false;  // switching IDLE_ALIVE <-> AUDIO_REACTIVE -- re-roll fresh timing
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
  forceReleaseOrdinaryMovement();  // pause ordinary movement first, if any was mid-pulse
  demoSavedMode = mode;
  demoCuriousStep = 0;
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
  if (activeCooldownLeft < 0) activeCooldownLeft = 0;
  if (strongCooldownLeft < 0) strongCooldownLeft = 0;

  Serial.printf(
      "[MOTION] mode=%s phase=%s direction=%s msUntilNextMovement=%ld audioBand=%s "
      "activeCooldownRemainingMs=%ld strongCooldownRemainingMs=%ld demoActive=%d demoPhase=%s "
      "powerGuardState=%d dimBrightnessRaw=%u emergencyStopped=%d\n",
      modeName(mode), phaseName(phase),
      motionDirection > 0 ? "FORWARD" : (motionDirection < 0 ? "REVERSE" : "NONE"), msUntilNext, bandName(audioBand),
      activeCooldownLeft, strongCooldownLeft, isMotionDemoActive() ? 1 : 0, demoPhaseName(demoPhase),
      (int)getMotorPowerGuardState(), getMotorDimBrightnessRaw(), emergencyStoppedFlag ? 1 : 0);
}
