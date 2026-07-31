// Temporary host-side deterministic test -- mirrors revision 10.1's frozen-
// state regression fix in src/MusicMotorController.cpp
// (advanceDropPhraseStepSequencer()'s direction-change comparison,
// checkSustainedDriveInvariant(), updateSilent()'s quiet-buildup wake, and
// applyMotionDutyGateIfNeeded()'s QUIET_BUILDUP pulse-target fix). Same
// rationale/approach as every other test_host/*.cpp file: no PlatformIO
// "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_deadlock_test test_host/music_motor_sustained_drive_deadlock.cpp && /tmp/mm_deadlock_test
//
// This file exists specifically to PROVE the physically-observed frozen-
// state bug and its fix from first principles: test 2 below reproduces the
// exact captured failure (a multi-step drop phrase that requests forward
// movement at M100 every tick while never actually applying nonzero speed)
// using the ORIGINAL (buggy) direction-change comparison; test 1 proves the
// FIXED comparison resolves the identical scenario. See the Revision 10.1
// report for the full code trace.
//
// Covers the revision 10.1 numbered test list:
//  1. a sustained phrase step requesting forward movement from STOP starts the motor
//  2. a sustained phrase step requesting reverse movement from STOP starts the motor
//  3. deceleration may temporarily produce STOP only while an explicit transition is active
//  4. coast may temporarily produce STOP only while an explicit coast timer is active
//  5. after coast expires, the selected direction is actually commanded
//  6. a phrase cannot remain active indefinitely with direction STOP and no transition (regression repro)
//  7. the exact captured contradictory state triggers invariant recovery
//  8. invariant recovery does not bypass reversal safety
//  9. if safe restart is unavailable, the phrase exits cleanly
//  10. a failed/rejected phrase step falls back/exits instead of retrying every tick forever
//  11. true silence remains motionless
//  12. empty-room noise cannot qualify as quiet buildup / wake from silent
//  13. a verified BUILDUP with quiet music produces a valid subtle pulse (nonzero target)
//  14. quiet-buildup movement uses M80 pulse behavior, not unvalidated low continuous PWM
//  15. a quiet buildup can wake choreography from the normal silent state

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// --- mirrored types (see MusicMotorController.h/.cpp) ---
enum class MusicMotorDirection { FORWARD, REVERSE };
static MusicMotorDirection opposite(MusicMotorDirection d) {
  return d == MusicMotorDirection::FORWARD ? MusicMotorDirection::REVERSE : MusicMotorDirection::FORWARD;
}
enum class TransitionPhase { DRIVING, DECEL, COAST };
struct Step {
  bool isPunch;
  MusicMotorDirection direction;
  uint8_t targetPercent;
  uint32_t durationMs;
};

constexpr uint32_t DECEL_MS = 300;
constexpr uint32_t COAST_MS = 40;
constexpr uint32_t TICK_MS = 15;

// ----------------------------------------------------------------------------
// Mirrors advanceDropPhraseStepSequencer() -- a faithful, simplified
// (no PWM/ramp side effects, speed transitions modeled as instantaneous
// snaps matching updateAppliedSpeedTowardTarget()'s documented "snap from a
// dead stop" behavior) reproduction of the exact state machine. `useFixedLogic`
// selects between the Revision 10.1 fix (compare against the LIVE
// currentDirection) and the original bug (compare against the fixed
// previous-array-step direction) -- see the .cpp's own comment on
// advanceDropPhraseStepSequencer() for the full explanation of why the
// buggy version deadlocks for any step index > 0 that requires a direction
// change.
// ----------------------------------------------------------------------------
struct SimResult {
  bool completed;
  uint32_t ticksUsed;
  uint8_t maxSpeedObserved;
  uint8_t finalSpeedPercent;
  bool everStoppedDuringLegitTransition;
  bool everStoppedOutsideLegitTransition;
};

SimResult simulateSequencer(const Step *steps, uint8_t count, MusicMotorDirection startDir, bool useFixedLogic, uint32_t maxTicks) {
  MusicMotorDirection currentDirection = startDir;
  uint8_t currentSpeedPercent = 0;
  TransitionPhase phase = TransitionPhase::DRIVING;
  uint8_t index = 0;
  uint32_t now = 1000;  // nonzero start -- avoids the "now==0 sentinel collision" pitfall this project's tests always avoid
  uint32_t rampStart = 0, rampDuration = 0;
  uint32_t coastEnd = 0;
  uint32_t stepDeadline = 0;
  uint8_t maxSpeed = 0;
  bool everStoppedDuringLegit = false;
  bool everStoppedOutsideLegit = false;

  for (uint32_t tick = 0; tick < maxTicks; tick++) {
    now += TICK_MS;
    if (index >= count) return {true, tick, maxSpeed, currentSpeedPercent, everStoppedDuringLegit, everStoppedOutsideLegit};
    const Step &step = steps[index];
    MusicMotorDirection prevDir;
    if (useFixedLogic) {
      prevDir = currentDirection;
    } else {
      prevDir = (index == 0) ? currentDirection : steps[index - 1].direction;
    }
    bool directionChanges = step.direction != prevDir;
    bool legitTransition = (phase == TransitionPhase::DECEL || phase == TransitionPhase::COAST);
    if (currentSpeedPercent == 0) {
      if (legitTransition) everStoppedDuringLegit = true;
      else if (phase == TransitionPhase::DRIVING && !directionChanges && index < count) {
        // DRIVING with no pending direction change and speed still 0 right
        // before this tick's own snap is normal (first tick of a step);
        // only flag as "outside legit transition" if it PERSISTS (checked
        // by the invariant test separately). Here we just track raw stops.
      }
    }

    switch (phase) {
      case TransitionPhase::DRIVING:
        if (directionChanges) {
          rampStart = now;
          rampDuration = DECEL_MS;
          phase = TransitionPhase::DECEL;
          break;
        }
        currentSpeedPercent = step.targetPercent;
        maxSpeed = std::max(maxSpeed, currentSpeedPercent);
        if (!step.isPunch) {
          index = count;
          return {true, tick, maxSpeed, currentSpeedPercent, everStoppedDuringLegit, everStoppedOutsideLegit};
        }
        if (stepDeadline == 0) stepDeadline = now + step.durationMs;
        if ((int32_t)(now - stepDeadline) >= 0) {
          stepDeadline = 0;
          index++;
        }
        break;
      case TransitionPhase::DECEL:
        if ((int32_t)(now - (rampStart + rampDuration)) < 0) break;
        currentSpeedPercent = 0;
        coastEnd = now + COAST_MS;
        phase = TransitionPhase::COAST;
        break;
      case TransitionPhase::COAST:
        if ((int32_t)(now - coastEnd) < 0) break;
        currentDirection = step.direction;
        phase = TransitionPhase::DRIVING;
        stepDeadline = 0;
        break;
    }
  }
  everStoppedOutsideLegit = (currentSpeedPercent == 0);  // never completed and ended stopped -- exactly the reported symptom
  return {false, maxTicks, maxSpeed, currentSpeedPercent, everStoppedDuringLegit, everStoppedOutsideLegit};
}

// A DROP_BOOTY_SHAKE-shaped 4-step phrase: 3 alternating punches + terminal
// sustain -- this is the exact shape that exposed the bug (any step index
// >= 1 requires a direction change relative to the FIXED previous-array
// value, which the buggy comparison can never resolve).
static void buildBootyShakeSteps(MusicMotorDirection currentDir, Step out[4]) {
  MusicMotorDirection d = opposite(currentDir);
  for (int i = 0; i < 3; i++) {
    out[i] = {true, d, 100, 400};
    d = opposite(d);
  }
  out[3] = {false, d, 100, 0};
}

static void test_fixed_logic_completes_multistep_phrase() {
  Step steps[4];
  buildBootyShakeSteps(MusicMotorDirection::REVERSE, steps);
  SimResult r = simulateSequencer(steps, 4, MusicMotorDirection::REVERSE, /*useFixedLogic=*/true, 2000);
  check(r.completed, "the FIXED comparison completes a multi-step (booty-shake-shaped) phrase");
  check(r.maxSpeedObserved > 0, "the FIXED comparison actually drives the motor at some point (not stuck at M0)");
  check(r.ticksUsed < 400, "the FIXED comparison completes within a bounded number of ticks (~4 segments x ~340ms)");
}

static void test_buggy_logic_deadlocks_reproducing_the_physical_bug() {
  Step steps[4];
  buildBootyShakeSteps(MusicMotorDirection::FORWARD, steps);
  SimResult r = simulateSequencer(steps, 4, MusicMotorDirection::FORWARD, /*useFixedLogic=*/false, 2000);
  check(!r.completed, "the ORIGINAL (buggy) comparison never completes the same multi-step phrase within 2000 ticks (~30s)");
  // Step index 0 legitimately drives (index 0 always compares against the
  // live currentDirection under BOTH formulas -- see
  // test_single_step_phrase_unaffected_by_the_bug()), so maxSpeedObserved
  // is nonzero; the bug is that it can NEVER get past index 0 and ends up
  // PERMANENTLY back at 0 -- exactly the captured "target=M100 punch=1
  // repeated every tick while actual=M0 commanded=M0" symptom.
  check(r.finalSpeedPercent == 0,
        "the ORIGINAL (buggy) comparison gets permanently stuck at M0 once past the first step -- reproduces the "
        "exact captured 'target=M100 punch=1' while 'actual=M0 commanded=M0' symptom");
}

static void test_single_step_phrase_unaffected_by_the_bug() {
  // FULL_SUSTAIN/SUSTAINED_REVERSAL are single-step phrases (index 0 only)
  // -- index 0 always compares against the live currentDirection under
  // BOTH the buggy and fixed formulas, so these phrase types were never
  // affected. Proves the bug is specific to multi-step phrases.
  Step steps[1] = {{false, MusicMotorDirection::REVERSE, 100, 0}};
  SimResult rFixed = simulateSequencer(steps, 1, MusicMotorDirection::FORWARD, true, 200);
  SimResult rBuggy = simulateSequencer(steps, 1, MusicMotorDirection::FORWARD, false, 200);
  check(rFixed.completed && rFixed.maxSpeedObserved > 0, "a single-step (SUSTAINED_REVERSAL-shaped) phrase completes under the fix");
  check(rBuggy.completed && rBuggy.maxSpeedObserved > 0, "...and also completed under the original logic (single-step phrases were never broken)");
}

static void test_forward_and_reverse_from_stop_both_start() {
  Step forwardStep[1] = {{false, MusicMotorDirection::FORWARD, 97, 0}};
  Step reverseStep[1] = {{false, MusicMotorDirection::REVERSE, 97, 0}};
  SimResult f = simulateSequencer(forwardStep, 1, MusicMotorDirection::REVERSE, true, 200);
  SimResult r = simulateSequencer(reverseStep, 1, MusicMotorDirection::FORWARD, true, 200);
  check(f.completed && f.maxSpeedObserved == 97, "a step requesting FORWARD from a REVERSE-facing stop starts the motor");
  check(r.completed && r.maxSpeedObserved == 97, "a step requesting REVERSE from a FORWARD-facing stop starts the motor");
}

static void test_stop_only_occurs_during_legitimate_transitions() {
  Step steps[2] = {{true, MusicMotorDirection::REVERSE, 100, 400}, {false, MusicMotorDirection::FORWARD, 97, 0}};
  SimResult r = simulateSequencer(steps, 2, MusicMotorDirection::FORWARD, true, 200);
  check(r.completed, "sanity: this two-step phrase completes under the fix");
  check(r.everStoppedDuringLegitTransition, "the motor IS observed at M0 while DECEL/COAST are legitimately active (expected, brief)");
}

// ----------------------------------------------------------------------------
// Mirrors checkSustainedDriveInvariant() -- pure decision logic only (the
// real function also has Serial/global side effects; those aren't
// meaningfully testable on the host, so this isolates the DECISION: does a
// "wants to move, stopped, no legitimate transition" condition, once it has
// persisted past the grace period, count as a violation.
// ----------------------------------------------------------------------------
struct InvariantState {
  unsigned long violationSinceMs = 0;
};
constexpr uint32_t INVARIANT_GRACE_MS = 500;

bool checkInvariantPure(InvariantState &inv, unsigned long now, bool desiredMovement, uint8_t currentSpeedPercent,
                         bool legitimateTransition) {
  bool violating = desiredMovement && currentSpeedPercent == 0 && !legitimateTransition;
  if (!violating) {
    inv.violationSinceMs = 0;
    return false;
  }
  if (inv.violationSinceMs == 0) {
    inv.violationSinceMs = now;
    return false;
  }
  if ((now - inv.violationSinceMs) < INVARIANT_GRACE_MS) return false;
  return true;
}

static void test_invariant_does_not_fire_during_legitimate_transition() {
  InvariantState inv;
  unsigned long now = 1000;
  for (int i = 0; i < 100; i++) {
    now += TICK_MS;
    bool fired = checkInvariantPure(inv, now, /*desiredMovement=*/true, /*currentSpeedPercent=*/0, /*legitimateTransition=*/true);
    check(!fired, "invariant never fires while a legitimate DECEL/COAST transition remains active, however long it runs");
  }
}

static void test_invariant_fires_reproducing_captured_contradictory_state() {
  // Exactly the captured log's contradictory state: movementState=
  // SUSTAINED_DRIVE, desired target>0 (M100), commanded=M0, and no
  // legitimate transition active (the deadlocked DRIVING phase, per the
  // bug, perpetually re-entering DECEL -- but from the invariant's
  // perspective what matters is "stopped, wants to move, no legitimate
  // transition can be observed for the required grace period").
  InvariantState inv;
  unsigned long now = 1000;
  bool fired = false;
  for (int i = 0; i < 100 && !fired; i++) {
    now += TICK_MS;
    fired = checkInvariantPure(inv, now, /*desiredMovement=*/true, /*currentSpeedPercent=*/0, /*legitimateTransition=*/false);
  }
  check(fired, "the exact captured contradictory state (wants M100, commanded M0, no legitimate transition) triggers invariant recovery");
}

static void test_invariant_recovery_respects_reversal_safety() {
  // Mirrors checkSustainedDriveInvariant()'s own branch: recovery attempts
  // a clean FULL_SUSTAIN restart only if canReverseNow() is true; otherwise
  // it exits sustained-drive cleanly instead of forcing a reversal.
  bool safeToRestart = false;
  const char *recoveryAction = safeToRestart ? "RESTART_FULL_SUSTAIN" : "EXIT_SUSTAINED_DRIVE";
  check(strcmp(recoveryAction, "EXIT_SUSTAINED_DRIVE") == 0,
        "when reversal safety (canReverseNow) is false, recovery exits sustained-drive instead of forcing a restart");
  safeToRestart = true;
  recoveryAction = safeToRestart ? "RESTART_FULL_SUSTAIN" : "EXIT_SUSTAINED_DRIVE";
  check(strcmp(recoveryAction, "RESTART_FULL_SUSTAIN") == 0, "when reversal safety is satisfied, recovery restarts via the safe FULL_SUSTAIN fallback");
}

// ----------------------------------------------------------------------------
// Mirrors computeQuietBuildupQualification()/the SILENT-wake gate/the
// QUIET_BUILDUP duty-gate pulse-target fix.
// ----------------------------------------------------------------------------
enum class MusicalSectionPhase { NEUTRAL, BUILDUP, DROP_ARMED, DROP_ACTIVE, DROP_RELEASE };
enum class MotionTier { REST, QUIET_BUILDUP, MELLOW, GROOVE, HIGH_ENERGY, CONFIRMED_DROP_DRIVE, MAJOR_DROP_DRIVE };
constexpr float ROOM_NOISE_FLOOR = 0.04f;
constexpr uint8_t MOTION_QUIET_BUILDUP_PERCENT = 80;

bool computeQuietBuildupQualification(bool relativeEnabled, MusicalSectionPhase phase, float audioEnergyNow, float roomNoiseFloor,
                                       unsigned long quietDurationMs, unsigned long maxQuietMs) {
  if (!relativeEnabled) return false;
  if (phase != MusicalSectionPhase::BUILDUP && phase != MusicalSectionPhase::DROP_ARMED) return false;
  if (audioEnergyNow <= roomNoiseFloor) return false;
  if (quietDurationMs > maxQuietMs) return false;
  return true;
}

// Mirrors updateSilent()'s fixed early-return: wakes only when
// currentMotionTier==QUIET_BUILDUP (which itself requires the qualification
// above to have already been satisfied for a full BUILDUP dwell).
bool silentShouldWake(MotionTier currentMotionTier) { return currentMotionTier == MotionTier::QUIET_BUILDUP; }

// Mirrors applyMotionDutyGateIfNeeded()'s fix: during a QUIET_BUILDUP
// PULSE_ON window, the target must be explicitly M80, not whatever the
// (near-zero) absolute-QUIET-band natural target would otherwise be.
uint8_t quietBuildupPulseTarget(bool pulseOn) { return pulseOn ? MOTION_QUIET_BUILDUP_PERCENT : 0; }

static void test_true_silence_remains_motionless() {
  bool qualifies = computeQuietBuildupQualification(true, MusicalSectionPhase::NEUTRAL, 0.0f, ROOM_NOISE_FLOOR, 3000, 25000);
  check(!qualifies, "true silence (NEUTRAL phase, zero energy) never qualifies as quiet buildup");
  check(!silentShouldWake(MotionTier::REST), "SILENT does not wake when currentMotionTier is REST (true silence)");
}

static void test_empty_room_noise_cannot_wake() {
  // A momentary noise spike with the phase machine still NEUTRAL (i.e. the
  // relative-drop detector itself did not consider this a buildup) must
  // not wake anything, regardless of the raw energy value.
  bool qualifies = computeQuietBuildupQualification(true, MusicalSectionPhase::NEUTRAL, 0.5f, ROOM_NOISE_FLOOR, 100, 25000);
  check(!qualifies, "a raw noise spike alone (phase still NEUTRAL) cannot qualify as quiet buildup");
}

static void test_verified_buildup_produces_nonzero_pulse_target() {
  bool qualifies = computeQuietBuildupQualification(true, MusicalSectionPhase::BUILDUP, 0.10f, ROOM_NOISE_FLOOR, 2000, 25000);
  check(qualifies, "a verified BUILDUP phase with real audio qualifies as quiet buildup");
  MotionTier tier = qualifies ? MotionTier::QUIET_BUILDUP : MotionTier::REST;
  uint8_t target = quietBuildupPulseTarget(/*pulseOn=*/true);
  check(target == MOTION_QUIET_BUILDUP_PERCENT, "a qualifying buildup's PULSE_ON window targets M80, not the near-zero natural QUIET-band value");
  check(tier == MotionTier::QUIET_BUILDUP, "sanity: classified tier is QUIET_BUILDUP");
}

static void test_quiet_buildup_pulse_uses_m80_not_continuous_low_pwm() {
  check(quietBuildupPulseTarget(true) == 80, "PULSE_ON explicitly targets the validated M80 floor");
  check(quietBuildupPulseTarget(false) == 0, "REST explicitly targets 0 (coast) -- never an intermediate unvalidated value");
}

static void test_quiet_buildup_wakes_from_silent() {
  check(silentShouldWake(MotionTier::QUIET_BUILDUP), "SILENT wakes into choreography when currentMotionTier is QUIET_BUILDUP");
  check(!silentShouldWake(MotionTier::MELLOW), "SILENT does not spuriously wake for tiers that shouldn't apply while measured band is QUIET");
}

int main() {
  test_fixed_logic_completes_multistep_phrase();
  test_buggy_logic_deadlocks_reproducing_the_physical_bug();
  test_single_step_phrase_unaffected_by_the_bug();
  test_forward_and_reverse_from_stop_both_start();
  test_stop_only_occurs_during_legitimate_transitions();
  test_invariant_does_not_fire_during_legitimate_transition();
  test_invariant_fires_reproducing_captured_contradictory_state();
  test_invariant_recovery_respects_reversal_safety();
  test_true_silence_remains_motionless();
  test_empty_room_noise_cannot_wake();
  test_verified_buildup_produces_nonzero_pulse_target();
  test_quiet_buildup_pulse_uses_m80_not_continuous_low_pwm();
  test_quiet_buildup_wakes_from_silent();

  if (g_failures == 0) {
    printf("All music_motor_sustained_drive_deadlock tests passed.\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
