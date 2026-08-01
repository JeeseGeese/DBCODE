#include "DanceEngine.h"

// See include/DanceEngine.h's ENABLE_LEGACY_DANCE_ENGINE comment. When 0
// (the default), this entire legacy implementation is compiled out and
// replaced by the no-op/false stubs in the #else branch at the bottom of
// this file -- DanceEngine adds no code size, no init work, and no per-loop
// work to a normal production build.
#if ENABLE_LEGACY_DANCE_ENGINE

#include <Arduino.h>
#include <math.h>

#include "AudioAnalyzer.h"
#include "Config.h"
#include "ExpressiveMotion.h"  // isAnyMotorDiagnosticActive()/isExpressiveMotionMoving() -- implemented in
                                // main.cpp, declared here per the existing project convention (see
                                // MotorPwmCalibration.cpp for the same pattern); also
                                // getExpressiveMotionMode()/setExpressiveMotionMode() so enabling DanceEngine
                                // can turn off the old AUDIO_REACTIVE motor pulses instead of running two
                                // audio-to-motor implementations at once.
#include "MotorBehavior.h"     // setMotorBehavior(OFF) -- preempts IDLE_SWAY, matching every other owner
#include "MotorDriver.h"       // motorPWM*()/initMotorPWM()/deinitMotorPWM() -- the only pin-touching layer
#include "MusicMotorController.h"  // isMusicMotorControllerActive()/cancelMusicMotorController() -- a
                                     // choreographed dance and music-reactive movement are never allowed to
                                     // drive the motor at once (see danceEngineEnable())

namespace {

float sanitizeFloat(float v, float fallback, float lo, float hi) {
  if (isnan(v) || isinf(v)) return fallback;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

uint8_t percentToDuty(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)((uint16_t)percent * 255 / 100);
}

const char *dirName(DanceDirection d) { return d == DanceDirection::FORWARD ? "FORWARD" : "REVERSE"; }

// Matches the diagnostic-line example's "state=FORWARD"/"state=REVERSE" for
// the two moving states; the raw enum name otherwise.
const char *stateName(DanceState s) {
  switch (s) {
    case DanceState::OFF: return "DISABLED";
    case DanceState::RESTING: return "RESTING";
    case DanceState::STARTING: return "STARTING";
    case DanceState::MOVING_FORWARD: return "FORWARD";
    case DanceState::MOVING_REVERSE: return "REVERSE";
    case DanceState::RAMPING_DOWN: return "RAMPING_DOWN";
    case DanceState::COASTING_FOR_REVERSE: return "COASTING_FOR_REVERSE";
  }
  return "UNKNOWN";
}

// ============================================================================
// Deterministic simulated-energy test sequence ('dancetest') -- see
// DanceEngine.h. Values feed into the exact same rawEnergy/smoothedEnergy/
// transientStrength path live AudioFeatures do (see updateDanceEngine()).
// ============================================================================
struct DanceTestStep {
  float energy;
  bool injectStrongTransient;  // one-shot spike on this step's first tick, matching a real transient's edge
  uint32_t durationMs;
  const char *label;
};
constexpr DanceTestStep DANCE_TEST_STEPS[] = {
    {0.00f, false, 1000, "Silence"},
    {0.25f, false, 1800, "Low energy"},
    {0.50f, false, 2200, "Medium energy"},
    {0.75f, false, 2200, "High energy"},
    {0.97f, true, 1600, "Strong transient / peak"},
    {0.50f, false, 1600, "Medium energy (settling)"},
    {0.00f, false, 1200, "Silence"},
};
constexpr uint8_t DANCE_TEST_STEP_COUNT = sizeof(DANCE_TEST_STEPS) / sizeof(DANCE_TEST_STEPS[0]);

// ============================================================================
// State
// ============================================================================
DanceState state = DanceState::OFF;
bool pwmReady = false;

DanceDirection currentDirection = DanceDirection::FORWARD;
DanceDirection pendingDirection = DanceDirection::FORWARD;  // meaningful only while reversalPending
bool reversalPending = false;                                // RAMPING_DOWN/COASTING_FOR_REVERSE heading to a reversal (false = heading to rest)

uint8_t currentDutyPercent = 0;
uint8_t targetDutyPercent = 0;

bool kickActive = false;
unsigned long kickEndMs = 0;

unsigned long directionStartMs = 0;
unsigned long lastReversalMs = 0;
unsigned long belowStopThresholdSinceMs = 0;  // 0 = not currently below the stop threshold
unsigned long coastEndMs = 0;

unsigned long lastTickMs = 0;
unsigned long lastDiagPrintMs = 0;

float rawEnergy = 0.0f;
float smoothedEnergy = 0.0f;
float transientStrength = 0.0f;

bool lastStopWasEmergency = false;

// --- simulated input ---
bool simulatedActive = false;
float simEnergy = 0.0f;
float simTransientOneShot = 0.0f;  // consumed (reset to 0) after the tick that reads it

bool testRunning = false;
uint8_t testStepIdx = 0;
unsigned long testStepStartMs = 0;

void ensurePWMReady() {
  if (pwmReady) return;
  initMotorPWM(DANCE_PWM_FREQUENCY_HZ, DANCE_PWM_RESOLUTION_BITS);
  pwmReady = true;
}

void applyDuty(DanceDirection dir, uint8_t percent) {
  uint8_t duty = percentToDuty(percent);
  if (dir == DanceDirection::FORWARD) motorPWMForward(duty);
  else motorPWMReverse(duty);
}

// Energy -> target speed-percent mapping (see Config.h / the DanceEngine V1
// spec's band table). Below 0.15 this returns 0 -- callers that are
// actively MOVING clamp the result up to DANCE_MIN_SPEED_PERCENT themselves
// (see updateMoving()), since sub-80% duty isn't useful movement on the
// physically-validated motor; a genuine sustained drop is handled by the
// separate silence-hysteresis timer, not by this mapping.
uint8_t computeTargetPercent(float energy) {
  energy = constrain(energy, 0.0f, 1.0f);
  float pct;
  if (energy < 0.15f) {
    pct = 0.0f;
  } else if (energy < 0.35f) {
    pct = 80.0f + (85.0f - 80.0f) * ((energy - 0.15f) / (0.35f - 0.15f));
  } else if (energy < 0.60f) {
    pct = 85.0f + (92.0f - 85.0f) * ((energy - 0.35f) / (0.60f - 0.35f));
  } else if (energy < 0.82f) {
    pct = 92.0f + (97.0f - 92.0f) * ((energy - 0.60f) / (0.82f - 0.60f));
  } else {
    float t = (1.00f - 0.82f) > 0.0f ? (energy - 0.82f) / (1.00f - 0.82f) : 1.0f;
    pct = 97.0f + (100.0f - 97.0f) * constrain(t, 0.0f, 1.0f);
  }
  return (uint8_t)(pct + 0.5f);
}

// Slew-rate limiter -- only ever called from the already DANCE_PWM_UPDATE_MS
// rate-limited tick (see updateDanceEngine()), so that fixed interval is
// used as dt rather than tracking a separate timestamp.
void rampDutyTowardRate(uint8_t target, float ratePercentPerSec) {
  float maxStep = ratePercentPerSec * (DANCE_PWM_UPDATE_MS / 1000.0f);
  int16_t diff = (int16_t)target - (int16_t)currentDutyPercent;
  if (diff > 0) {
    int16_t step = (int16_t)ceilf(maxStep);
    currentDutyPercent = (uint8_t)min((int16_t)target, (int16_t)(currentDutyPercent + step));
  } else if (diff < 0) {
    int16_t step = (int16_t)ceilf(maxStep);
    currentDutyPercent = (uint8_t)max((int16_t)target, (int16_t)(currentDutyPercent - step));
  }
}

// Shared by STARTING's completion and COASTING_FOR_REVERSE's completion --
// begins actually driving `dir`, applying the startup kick when the current
// energy-derived target is below the kick threshold. Only ever called when
// currentDutyPercent is already 0 (freshly stopped/coasted), matching the
// "only apply when starting actual movement from stopped" rule. Does not
// touch LED state in any way -- DanceEngine never reads or writes LED mute
// (see the header comment); LEDs remain exactly as the user left them
// through startup, ramping, sustained movement, and reversal alike.
void beginMovement(DanceDirection dir, unsigned long now) {
  currentDirection = dir;
  directionStartMs = now;
  uint8_t target = computeTargetPercent(smoothedEnergy);
  target = (uint8_t)constrain((int)target, (int)DANCE_MIN_SPEED_PERCENT, (int)DANCE_MAX_SPEED_PERCENT);
  targetDutyPercent = target;

  if (target < DANCE_STARTUP_KICK_THRESHOLD_PERCENT) {
    kickActive = true;
    kickEndMs = now + DANCE_STARTUP_KICK_MS;
    currentDutyPercent = DANCE_STARTUP_KICK_PERCENT;
    Serial.printf("[DANCE] Startup kick applied: %u%% for %ums\n", (unsigned)DANCE_STARTUP_KICK_PERCENT,
                  (unsigned)DANCE_STARTUP_KICK_MS);
  } else {
    kickActive = false;
    currentDutyPercent = 0;  // ramps up toward target over the next ticks (moderate ramp-up)
  }
  applyDuty(dir, currentDutyPercent);
  state = (dir == DanceDirection::FORWARD) ? DanceState::MOVING_FORWARD : DanceState::MOVING_REVERSE;
  Serial.printf("[DANCE] Movement started: %s at %u%%\n", dirName(dir), (unsigned)target);
}

void beginRampDown(unsigned long now, bool goingToRest) {
  kickActive = false;
  targetDutyPercent = 0;
  if (goingToRest) {
    reversalPending = false;
    Serial.println(F("[DANCE] Ramping down"));
  } else {
    pendingDirection = (currentDirection == DanceDirection::FORWARD) ? DanceDirection::REVERSE : DanceDirection::FORWARD;
    reversalPending = true;
    Serial.println(F("[DANCE] Ramping down for reversal"));
  }
  state = DanceState::RAMPING_DOWN;
}

void updateResting(unsigned long now) {
  if (smoothedEnergy >= DANCE_START_ENERGY_THRESHOLD) {
    state = DanceState::STARTING;  // one tick, then updateStarting() begins driving -- no MotorPowerGuard wait
  }
}

void updateStarting(unsigned long now) {
  beginMovement(currentDirection, now);
}

void updateMoving(unsigned long now, bool strongTrigger, bool mediumTrigger) {
  // Target speed tracks live smoothed energy continuously, always clamped
  // to the physically-validated active range while actually moving (see
  // SAFETY: "Clamp target motor speed to 80-100% while moving").
  uint8_t rawTarget = computeTargetPercent(smoothedEnergy);
  targetDutyPercent = (uint8_t)constrain((int)rawTarget, (int)DANCE_MIN_SPEED_PERCENT, (int)DANCE_MAX_SPEED_PERCENT);

  if (kickActive && (long)(now - kickEndMs) >= 0) kickActive = false;
  if (!kickActive) {
    float rate = (targetDutyPercent >= currentDutyPercent) ? DANCE_RAMP_UP_PERCENT_PER_SEC : DANCE_RAMP_DOWN_PERCENT_PER_SEC;
    rampDutyTowardRate(targetDutyPercent, rate);
    applyDuty(currentDirection, currentDutyPercent);
  }

  // Defensive backstop -- mirrors MOTION_MAX_ENERGIZED_MS/BREAKAWAY_MAX_ENERGIZED_MS
  // elsewhere in this codebase (see Config.h's DANCE_MAX_SEGMENT_MS comment).
  if ((now - directionStartMs) >= DANCE_MAX_SEGMENT_MS) {
    Serial.println(F("[DANCE] Safety: max segment runtime exceeded -- forcing ramp-down"));
    beginRampDown(now, /*goingToRest=*/true);
    return;
  }

  // Silence hysteresis: only ramp down to rest once the energy has stayed
  // below the (lower) stop threshold for DANCE_SILENCE_HOLD_MS -- a single
  // quiet instant never cuts a movement short.
  if (smoothedEnergy < DANCE_STOP_ENERGY_THRESHOLD) {
    if (belowStopThresholdSinceMs == 0) belowStopThresholdSinceMs = now;
    if (now - belowStopThresholdSinceMs >= DANCE_SILENCE_HOLD_MS) {
      Serial.println(F("[DANCE] Silence detected -- returning to rest"));
      beginRampDown(now, /*goingToRest=*/true);
      return;
    }
  } else {
    belowStopThresholdSinceMs = 0;
  }

  // Deterministic reversal decision -- see Config.h's DANCE_MIN_DIRECTION_HOLD_MS/
  // DANCE_MEDIUM_DIRECTION_HOLD_MS/DANCE_REVERSAL_COOLDOWN_MS. A small
  // transient (neither strong nor medium) intentionally does nothing extra
  // here -- it already nudges the target speed up via the attack-smoothed
  // energy above, satisfying "small transient: keep direction, optionally
  // increase speed briefly" without a separate special case.
  unsigned long heldMs = now - directionStartMs;
  bool cooldownElapsed = (now - lastReversalMs) >= DANCE_REVERSAL_COOLDOWN_MS;
  if (strongTrigger && heldMs >= DANCE_MIN_DIRECTION_HOLD_MS && cooldownElapsed) {
    Serial.println(F("[DANCE] Strong transient -- reversal requested"));
    beginRampDown(now, /*goingToRest=*/false);
  } else if (mediumTrigger && heldMs >= DANCE_MEDIUM_DIRECTION_HOLD_MS && cooldownElapsed) {
    Serial.println(F("[DANCE] Medium transient -- reversal requested"));
    beginRampDown(now, /*goingToRest=*/false);
  }
  // The simulated one-shot pulse is consumed here -- the first (and only)
  // tick where a MOVING_* state actually evaluates it -- regardless of
  // whether it triggered a reversal (hold-time/cooldown may have refused
  // it; that's still "evaluated once", matching a real transient's edge).
  if (simulatedActive) simTransientOneShot = 0.0f;
}

void updateRampingDown(unsigned long now) {
  float rate = reversalPending ? DANCE_RAMP_DOWN_FAST_PERCENT_PER_SEC : DANCE_RAMP_DOWN_PERCENT_PER_SEC;
  rampDutyTowardRate(0, rate);
  applyDuty(currentDirection, currentDutyPercent);
  if (currentDutyPercent != 0) return;

  motorPWMCoast();  // exact both-LOW, regardless of any rounding in the ramp above
  Serial.println(F("[DANCE] Coast"));
  if (reversalPending) {
    coastEndMs = now + DANCE_REVERSE_COAST_MS;
    state = DanceState::COASTING_FOR_REVERSE;
  } else {
    // No MotorPowerGuard release here -- DanceEngine never requests it.
    belowStopThresholdSinceMs = 0;
    state = DanceState::RESTING;
  }
}

void updateCoastingForReverse(unsigned long now) {
  if ((long)(now - coastEndMs) < 0) return;
  Serial.printf("[DANCE] Direction changed: %s\n", dirName(pendingDirection));
  reversalPending = false;
  lastReversalMs = now;
  beginMovement(pendingDirection, now);
}

void maybePrintDiagnostic(unsigned long now) {
  if (state == DanceState::OFF || state == DanceState::RESTING) return;
  if (now - lastDiagPrintMs < DANCE_DIAG_PRINT_INTERVAL_MS) return;
  lastDiagPrintMs = now;
  long heldMs = (long)(now - directionStartMs);
  Serial.printf("[DANCE] state=%s energy=%.2f smooth=%.2f transient=%.2f speed=%u%% hold=%ldms\n", stateName(state),
                rawEnergy, smoothedEnergy, transientStrength, (unsigned)currentDutyPercent, heldMs);
}

void enterTestStep(uint8_t idx, unsigned long now) {
  const DanceTestStep &s = DANCE_TEST_STEPS[idx];
  simEnergy = s.energy;
  simTransientOneShot = s.injectStrongTransient ? (DANCE_TRANSIENT_STRONG_THRESHOLD + 1.0f) : 0.0f;
  testStepStartMs = now;
  Serial.printf("[DANCE TEST] Step %u/%u: %s (energy=%.2f, %lums)\n", (unsigned)(idx + 1),
                (unsigned)DANCE_TEST_STEP_COUNT, s.label, (double)s.energy, (unsigned long)s.durationMs);
}

// Advances the dancetest step timer -- called every updateDanceEngine()
// call (not rate-limited), so step durations stay accurate regardless of
// the motor-output tick rate.
void advanceTestIfNeeded(unsigned long now) {
  if (!testRunning) return;
  if (now - testStepStartMs < DANCE_TEST_STEPS[testStepIdx].durationMs) return;
  testStepIdx++;
  if (testStepIdx >= DANCE_TEST_STEP_COUNT) {
    testRunning = false;
    simulatedActive = false;
    Serial.println(F("[DANCE] dancetest complete -- returning to live microphone input"));
    return;
  }
  enterTestStep(testStepIdx, now);
}

// Full reset shared by initDanceEngine() and the enable/hard-stop paths.
void resetRuntimeState() {
  currentDirection = DanceDirection::FORWARD;
  pendingDirection = DanceDirection::FORWARD;
  reversalPending = false;
  currentDutyPercent = 0;
  targetDutyPercent = 0;
  kickActive = false;
  kickEndMs = 0;
  directionStartMs = 0;
  // Seeded in the past (relative to "now") so the very first reversal after
  // enabling is never blocked by a stale cooldown from a previous session.
  lastReversalMs = millis() - DANCE_REVERSAL_COOLDOWN_MS;
  belowStopThresholdSinceMs = 0;
  coastEndMs = 0;
  lastTickMs = 0;
  lastDiagPrintMs = 0;
  rawEnergy = 0.0f;
  smoothedEnergy = 0.0f;
  transientStrength = 0.0f;
  simulatedActive = false;
  simEnergy = 0.0f;
  simTransientOneShot = 0.0f;
  testRunning = false;
  testStepIdx = 0;
  testStepStartMs = 0;
}

// Coasts, detaches PWM, and forces OFF -- shared by danceoff and the
// emergency/mstop cancel path. Does not touch LED state -- DanceEngine
// never reads or writes it, so there is nothing to restore here.
void hardStop() {
  motorPWMCoast();
  deinitMotorPWM();
  pwmReady = false;
  state = DanceState::OFF;
  resetRuntimeState();
}

bool ensureEnabledForTest();  // fwd decl, defined after danceEngineEnable() below

}  // namespace

void initDanceEngine() {
  state = DanceState::OFF;
  pwmReady = false;
  lastStopWasEmergency = false;
  resetRuntimeState();
}

void updateDanceEngine(unsigned long now) {
  if (state == DanceState::OFF) return;

  advanceTestIfNeeded(now);

  // Energy smoothing runs on every call (cheap; must not miss a live
  // AudioFeatures refresh just because the decision tick below is
  // rate-limited).
  float liveNorm = simulatedActive ? simEnergy : getAudioFeatures().normalized;
  liveNorm = sanitizeFloat(liveNorm, 0.0f, 0.0f, 1.0f);
  rawEnergy = liveNorm;
  float attackOrRelease = (rawEnergy > smoothedEnergy) ? DANCE_ENERGY_ATTACK : DANCE_ENERGY_RELEASE;
  smoothedEnergy = constrain(smoothedEnergy + (rawEnergy - smoothedEnergy) * attackOrRelease, 0.0f, 1.0f);

  if (now - lastTickMs < DANCE_PWM_UPDATE_MS) return;
  lastTickMs = now;

  // Transient reading happens only on an actual decision tick. The
  // simulated one-shot pulse (dancetest's injected "strong transient" step)
  // is deliberately NOT cleared here -- only updateMoving() clears it, and
  // only once it has actually evaluated it for a reversal decision. If the
  // pulse arrives while the engine is mid RAMPING_DOWN/RESTING/STARTING
  // (e.g. re-settling after a prior transition), it must survive until the
  // engine actually reaches a MOVING_* state -- otherwise a tick in one of
  // those other states would silently read-and-discard it first and the
  // scripted/live reversal would never fire (found during dancetest
  // validation: the peak step's transient landed mid-transition and was
  // lost before updateMoving() ever saw it).
  float liveTransient;
  bool liveClap;
  if (simulatedActive) {
    liveTransient = simTransientOneShot;
    liveClap = false;
  } else {
    const AudioFeatures &f = getAudioFeatures();
    liveTransient = f.transientStrength;
    liveClap = f.clap;
  }
  transientStrength = sanitizeFloat(liveTransient, 0.0f, 0.0f, 100.0f);  // generous clamp, just guards NaN/inf/runaway

  bool strongTrigger = liveClap || (transientStrength >= DANCE_TRANSIENT_STRONG_THRESHOLD);
  bool mediumTrigger = !strongTrigger && (transientStrength >= DANCE_TRANSIENT_MEDIUM_THRESHOLD);

  switch (state) {
    case DanceState::OFF: break;  // unreachable (handled above)
    case DanceState::RESTING: updateResting(now); break;
    case DanceState::STARTING: updateStarting(now); break;
    case DanceState::MOVING_FORWARD:
    case DanceState::MOVING_REVERSE: updateMoving(now, strongTrigger, mediumTrigger); break;
    case DanceState::RAMPING_DOWN: updateRampingDown(now); break;
    case DanceState::COASTING_FOR_REVERSE: updateCoastingForReverse(now); break;
  }

  maybePrintDiagnostic(now);
}

bool isDanceEngineActive() { return state != DanceState::OFF; }

void danceEngineEnable() {
  if (state != DanceState::OFF) {
    Serial.println(F("[DANCE] Already enabled"));
    return;
  }
  // MusicMotorController is stopped PROACTIVELY, before the refusal check
  // below -- isAnyMotorDiagnosticActive() already reports true while it is
  // active, so checking it first would always refuse this enable instead
  // of cleanly taking over. Starting a choreographed dance must stop
  // incompatible music-reactive movement, not be blocked by it.
  if (isMusicMotorControllerActive()) {
    Serial.println(F("[DANCE] Stopping MusicMotorController -- DanceEngine is taking motor ownership"));
    cancelMusicMotorController();
  }
  if (isAnyMotorDiagnosticActive() || isExpressiveMotionMoving()) {
    Serial.println(F("[DANCE] Refused -- another motor diagnostic or expressive motion currently owns the motor"));
    return;
  }
  setMotorBehavior(MotorBehaviorMode::OFF);  // preempt IDLE_SWAY, matching every other motor-owning module
  if (getExpressiveMotionMode() == ExpressiveMotionMode::AUDIO_REACTIVE) {
    // DanceEngine is the single audio-to-motor path -- avoid two
    // audio-reactive motor implementations both being "selected" at once,
    // even though mutual exclusion already prevents them both driving.
    Serial.println(F("[DANCE] Disabling AUDIO_REACTIVE expressive-motion mode -- DanceEngine is now the audio-to-motor path"));
    setExpressiveMotionMode(ExpressiveMotionMode::OFF);
  }
  ensurePWMReady();
  resetRuntimeState();
  lastStopWasEmergency = false;
  state = DanceState::RESTING;
  Serial.println(F("[DANCE] Enabled"));
}

void danceEngineDisable() {
  if (state == DanceState::OFF) {
    Serial.println(F("[DANCE] Already disabled"));
    return;
  }
  hardStop();
  Serial.println(F("[DANCE] Disabled"));
}

void cancelDanceEngine() {
  if (state == DanceState::OFF) return;  // idempotent no-op -- caller (k/mstop) prints its own message
  hardStop();
  lastStopWasEmergency = true;
}

namespace {
bool ensureEnabledForTest() {
  if (state != DanceState::OFF) return true;
  danceEngineEnable();
  return state != DanceState::OFF;
}
}  // namespace

void danceEngineStartTest() {
  if (!ensureEnabledForTest()) return;
  testRunning = true;
  simulatedActive = true;
  testStepIdx = 0;
  Serial.println(F("[DANCE] dancetest started"));
  enterTestStep(0, millis());
}

void danceEngineStopTest() {
  if (!testRunning && !simulatedActive) {
    Serial.println(F("[DANCE] No test/simulated input active"));
    return;
  }
  testRunning = false;
  simulatedActive = false;
  Serial.println(state != DanceState::OFF ? F("[DANCE] Test mode off -- live microphone input")
                                                : F("[DANCE] Test mode off"));
}

void danceEngineSimQuiet() {
  if (!ensureEnabledForTest()) return;
  testRunning = false;
  simulatedActive = true;
  simEnergy = 0.25f;
  simTransientOneShot = 0.0f;
  Serial.println(F("[DANCE] Simulated input: QUIET (energy=0.25)"));
}

void danceEngineSimMid() {
  if (!ensureEnabledForTest()) return;
  testRunning = false;
  simulatedActive = true;
  simEnergy = 0.50f;
  simTransientOneShot = 0.0f;
  Serial.println(F("[DANCE] Simulated input: MID (energy=0.50)"));
}

void danceEngineSimHigh() {
  if (!ensureEnabledForTest()) return;
  testRunning = false;
  simulatedActive = true;
  simEnergy = 0.75f;
  simTransientOneShot = 0.0f;
  Serial.println(F("[DANCE] Simulated input: HIGH (energy=0.75)"));
}

void danceEngineSimPeak() {
  if (!ensureEnabledForTest()) return;
  testRunning = false;
  simulatedActive = true;
  simEnergy = 0.95f;
  simTransientOneShot = DANCE_TRANSIENT_STRONG_THRESHOLD + 1.0f;
  Serial.println(F("[DANCE] Simulated input: PEAK (energy=0.95, strong transient injected)"));
}

void danceEnginePrintStatus() {
  unsigned long now = millis();
  Serial.println(F("[DANCE STATUS]"));
  Serial.printf("  Enabled: %s\n", state != DanceState::OFF ? "yes" : "no");
  Serial.printf("  Input mode: %s\n", simulatedActive ? "simulated" : "live");
  Serial.printf("  State: %s\n", stateName(state));
  Serial.printf("  Direction: %s\n", dirName(currentDirection));
  Serial.printf("  Pending direction: %s\n", reversalPending ? dirName(pendingDirection) : "-");
  Serial.printf("  Raw energy: %.3f\n", (double)rawEnergy);
  Serial.printf("  Smoothed energy: %.3f\n", (double)smoothedEnergy);
  Serial.printf("  Transient strength: %.3f\n", (double)transientStrength);
  Serial.printf("  Current duty: %u/255 (%u%%)\n", percentToDuty(currentDutyPercent), (unsigned)currentDutyPercent);
  Serial.printf("  Target duty: %u/255 (%u%%)\n", percentToDuty(targetDutyPercent), (unsigned)targetDutyPercent);

  bool moving = (state == DanceState::MOVING_FORWARD || state == DanceState::MOVING_REVERSE);
  long heldMs = moving ? (long)(now - directionStartMs) : 0;
  Serial.printf("  Time in current direction: %ldms\n", heldMs);
  long remainingHold = (long)DANCE_MIN_DIRECTION_HOLD_MS - heldMs;
  if (remainingHold < 0) remainingHold = 0;
  Serial.printf("  Remaining min-hold (strong-transient-eligible): %ldms\n", remainingHold);
  long cooldownRemaining = (long)DANCE_REVERSAL_COOLDOWN_MS - (long)(now - lastReversalMs);
  if (cooldownRemaining < 0) cooldownRemaining = 0;
  Serial.printf("  Reversal cooldown remaining: %ldms\n", cooldownRemaining);
  long silenceMs = (belowStopThresholdSinceMs != 0) ? (long)(now - belowStopThresholdSinceMs) : 0;
  Serial.printf("  Silence timer: %ldms (stops at %lums)\n", silenceMs, (unsigned long)DANCE_SILENCE_HOLD_MS);
  Serial.printf("  Startup kick active: %s\n", kickActive ? "yes" : "no");
  Serial.printf("  Coast/reversal state: %s\n",
                state == DanceState::COASTING_FOR_REVERSE ? "coasting"
                : state == DanceState::RAMPING_DOWN        ? (reversalPending ? "ramping down (reversal)" : "ramping down (rest)")
                                                             : "-");
  Serial.printf("  Motor ownership: %s\n", state != DanceState::OFF ? "DanceEngine" : "none (available)");
  Serial.printf("  Emergency-stop state: %s\n", lastStopWasEmergency ? "LATCHED (last stop was emergency/mstop)" : "clear");
  Serial.printf("  Test running: %s", testRunning ? "yes" : "no");
  if (testRunning) Serial.printf(" (step %u/%u: %s)", (unsigned)(testStepIdx + 1), (unsigned)DANCE_TEST_STEP_COUNT,
                                  DANCE_TEST_STEPS[testStepIdx].label);
  Serial.println();

  if (state == DanceState::OFF || state == DanceState::RESTING || currentDutyPercent == 0) {
    Serial.println(F("  GPIO8 (IN1): LOW   GPIO9 (IN2): LOW"));
  } else if (currentDirection == DanceDirection::FORWARD) {
    Serial.printf("  GPIO8 (IN1): PWM duty=%u/255   GPIO9 (IN2): LOW\n", percentToDuty(currentDutyPercent));
  } else {
    Serial.printf("  GPIO8 (IN1): LOW   GPIO9 (IN2): PWM duty=%u/255\n", percentToDuty(currentDutyPercent));
  }
}

#else  // !ENABLE_LEGACY_DANCE_ENGINE

// Legacy engine compiled out (see include/DanceEngine.h). Every function
// below is a cheap no-op/false stub -- no state, no motor ownership, no
// Serial output -- so every existing call site (main.cpp, Controls.cpp,
// isAnyMotorDiagnosticActive()) still links and behaves as "DanceEngine is
// not running" without needing to be individually gated.
#include <Arduino.h>

void initDanceEngine() {}
void updateDanceEngine(unsigned long) {}
bool isDanceEngineActive() { return false; }
void danceEngineEnable() { Serial.println(F("[DANCE] Legacy DanceEngine is disabled in this build (superseded by MusicMotorController)")); }
void danceEngineDisable() {}
void danceEnginePrintStatus() { Serial.println(F("[DANCE] Legacy DanceEngine is disabled in this build (superseded by MusicMotorController)")); }
void danceEngineStartTest() { danceEngineEnable(); }
void danceEngineStopTest() {}
void danceEngineSimQuiet() { danceEngineEnable(); }
void danceEngineSimMid() { danceEngineEnable(); }
void danceEngineSimHigh() { danceEngineEnable(); }
void danceEngineSimPeak() { danceEngineEnable(); }
void cancelDanceEngine() {}

#endif  // ENABLE_LEGACY_DANCE_ENGINE
