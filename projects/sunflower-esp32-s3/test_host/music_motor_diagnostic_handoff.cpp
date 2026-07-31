// Temporary host-side deterministic test -- mirrors the diagnostic
// investigation into a false "UNKNOWN_POSSIBLE_INVARIANT_VIOLATION"
// intentionalStopReason observed during MusicMotor physical validation, and
// the fix in src/MusicMotorController.cpp's maybePrintDiagnostic()
// (classifyIntentionalStopReason() below mirrors its exact fallback chain)
// plus exitSustainedDrive()'s new sustainedDriveLowEnergySinceMs reset. Same
// rationale/approach as every other test_host/*.cpp file: no PlatformIO
// "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_handoff_test test_host/music_motor_diagnostic_handoff.cpp && /tmp/mm_handoff_test
//
// Root cause (see the investigation report for the full trace): both the
// top-level coastingForReversal handler in updateMusicMotorController() and
// advanceDropPhraseStepSequencer()'s own COAST branch mutate
// currentDirection/directionStartMs and clear the "still coasting" flag
// (coastingForReversal / dropPhraseTransitionPhase) BEFORE
// maybePrintDiagnostic() runs on that SAME tick -- while the actual speed
// snap-to-target doesn't happen until the per-state dispatch resumes on the
// NEXT tick (this tick returns early). currentSpeedPercent is therefore
// legitimately 0 for exactly one tick after the handoff is already "done"
// by every other measure. The fix adds a precise, narrow fallback
// (directionStartMs==now) for that exact tick, plus a
// MusicMotorState::DECELERATING-specific label ("deceleration_handoff").
//
// This file reconstructs the full reported sequence tick-by-tick:
// SUSTAINED_DRIVE exit -> DECELERATING (ramping toward a falling target as
// the band drops) -> INTENSITY_SWAY (band recovers, a periodic reversal is
// requested) -> reversal coast -> direction handoff tick -> resumed
// movement in the new direction -- and asserts:
//   - no tick is ever classified UNKNOWN_POSSIBLE_INVARIANT_VIOLATION
//   - the (mirrored) invariant violation counter never increments
//   - currentSpeedPercent==0 only occurs during DECELERATING, the coast, or
//     the single handoff tick -- never as an unexplained stall
//   - movement resumes in the intended (reversed) direction at the correct
//     target speed
//   - the (mirrored) unexpected-stopped-time counter stays 0
//   - sustainedDriveLowEnergySinceMs is cleared by exitSustainedDrive(),
//     not left ACTIVE indefinitely

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

enum class Direction { FORWARD, REVERSE };
static Direction opposite(Direction d) { return d == Direction::FORWARD ? Direction::REVERSE : Direction::FORWARD; }
enum class SimState { DECELERATING, INTENSITY_SWAY };

// Mirrors maybePrintDiagnostic()'s exact fallback chain (see
// MusicMotorController.cpp for the authoritative original).
const char *classifyIntentionalStopReason(bool currentSpeedIsZero, bool lastStopWasEmergency, bool phraseDecelerating,
                                           bool phraseCoasting, bool stateIsDecelerating, bool dutyRest, bool coastingForReversal,
                                           bool directionJustChangedThisTick, bool desiredTargetIsZero) {
  if (!currentSpeedIsZero) return "none";
  if (lastStopWasEmergency) return "emergency_stop";
  if (phraseDecelerating) return "decelerating";
  if (phraseCoasting) return "coasting";
  if (stateIsDecelerating) return "deceleration_handoff";
  if (dutyRest) return "duty_rest";
  if (coastingForReversal) return "reversal_coast";
  if (directionJustChangedThisTick) return "direction_change_handoff";
  if (desiredTargetIsZero) return "target_zero";
  return "UNKNOWN_POSSIBLE_INVARIANT_VIOLATION";
}

struct SimCounters {
  uint32_t unexpectedStoppedMs = 0;
  uint32_t invariantViolations = 0;
};

// Mirrors checkSustainedDriveInvariant()'s scope: it ONLY ever evaluates
// while state==SUSTAINED_DRIVE. This sequence never re-enters
// SUSTAINED_DRIVE, so the mirrored counters must stay at 0 throughout --
// exactly matching the captured "Invariant violations: 0 / Recoveries: 0 /
// Aborts: 0 / Unexpected-stopped time: 0ms" summary, which was correct, not
// buggy.

static void test_full_exit_decel_handoff_resume_sequence() {
  constexpr uint32_t TICK_MS = 15;
  constexpr uint32_t DECEL_DURATION_MS = 400;
  constexpr uint32_t COAST_MS = 40;

  SimState state = SimState::DECELERATING;
  Direction currentDirection = Direction::FORWARD;
  Direction pendingDirection = Direction::FORWARD;
  bool coastingForReversal = false;
  uint32_t coastEndMs = 0;
  uint8_t currentSpeedPercent = 95;  // was actively driving in SUSTAINED_DRIVE just before exit
  uint8_t intensityTargetPercent = 0;  // band fell toward QUIET while decelerating
  uint32_t rampStartMs = 1000;
  uint8_t rampFromPercent = 95;
  SimCounters counters;

  // Band schedule, decoupled from the state machine (mirrors the shared
  // per-tick block computing intensityTargetPercent BEFORE the
  // coastingForReversal gate / per-state dispatch, every single tick,
  // regardless of state): QUIET (target 0) through the full decel
  // duration, recovering to LOW (target 80) exactly one tick later -- so
  // decel legitimately completes AT target 0 (a genuine, expected
  // zero-output tick), and the band recovery is a clean, separate event
  // the following tick, not conflated with ramp completion.
  uint32_t recoverAtMs = rampStartMs + DECEL_DURATION_MS + TICK_MS;
  bool periodicReversalRequested = false;

  uint32_t now = 1000;
  bool sawDecelZero = false;
  bool sawHandoffTick = false;
  bool everUnknown = false;
  bool everUnexplainedStopOutsideKnownWindows = false;
  bool sawReversalCoastActive = false;

  for (int tick = 0; tick < 200; tick++) {
    now += TICK_MS;

    // 1. Fresh target every tick, unconditionally (shared block).
    intensityTargetPercent = (now < recoverAtMs) ? 0 : 80;

    // 2. coastingForReversal gate -- resolved FIRST; per-state dispatch is
    // skipped this ENTIRE tick if we were still coasting at the top of it,
    // exactly matching updateMusicMotorController()'s own top-level
    // ordering (the actual root cause of the one-tick artifact).
    bool wasCoastingAtTickStart = coastingForReversal;
    bool directionJustChangedThisTick = false;
    if (wasCoastingAtTickStart) {
      if ((int32_t)(now - coastEndMs) >= 0) {
        currentDirection = pendingDirection;
        // (the real code also sets directionStartMs=now here -- the exact
        // signal maybePrintDiagnostic() uses; directionJustChangedThisTick
        // below is this test's equivalent boolean.)
        coastingForReversal = false;
        directionJustChangedThisTick = true;
      }
    } else {
      switch (state) {
        case SimState::DECELERATING: {
          // Re-target every tick, exactly like the real updateDecelerating().
          uint8_t rampToPercent = intensityTargetPercent;
          float progress = (float)(now - rampStartMs) / (float)DECEL_DURATION_MS;
          if (progress > 1.0f) progress = 1.0f;
          currentSpeedPercent = (uint8_t)(rampFromPercent + ((int)rampToPercent - (int)rampFromPercent) * progress);
          if (now - rampStartMs >= DECEL_DURATION_MS) {
            currentSpeedPercent = rampToPercent;
            // Captured HERE, before the state transition below, since by
            // the time this tick's diagnostic-equivalent classification
            // runs (just after this switch), `state` has already become
            // INTENSITY_SWAY -- exactly the same "read after mutation"
            // ordering as the real bug, just for this sanity flag rather
            // than the classification itself (which correctly reads
            // post-mutation state, matching the real diagnostic).
            if (currentSpeedPercent == 0) sawDecelZero = true;
            state = SimState::INTENSITY_SWAY;
          }
          break;
        }
        case SimState::INTENSITY_SWAY: {
          if (!periodicReversalRequested && intensityTargetPercent != 0) {
            // swayDeadlineMs had already elapsed during the quiet stretch
            // -- the first INTENSITY_SWAY tick with a real target requests
            // the periodic reversal, exactly matching "direction state
            // changes" in the reported sequence.
            periodicReversalRequested = true;
            pendingDirection = opposite(currentDirection);
            coastingForReversal = true;
            coastEndMs = now + COAST_MS;
            currentSpeedPercent = 0;  // tryRequestReversal()'s coast primitive zeroes speed immediately
          } else if (currentSpeedPercent == 0 && intensityTargetPercent != 0) {
            // updateAppliedSpeedTowardTarget()'s "snap from a dead stop"
            currentSpeedPercent = intensityTargetPercent;
          }
          break;
        }
      }
    }

    bool phraseDecelerating = false;  // drop-phrase sequencer not involved in this ordinary (non-drop) exit
    bool phraseCoasting = false;
    bool stateIsDecelerating = (state == SimState::DECELERATING);
    bool dutyRest = false;  // M80/GROOVE-adjacent target here, not a QUIET_BUILDUP/MELLOW duty tier
    bool lastStopWasEmergency = false;
    bool desiredTargetIsZero = intensityTargetPercent == 0;

    const char *reason = classifyIntentionalStopReason(currentSpeedPercent == 0, lastStopWasEmergency, phraseDecelerating,
                                                         phraseCoasting, stateIsDecelerating, dutyRest, coastingForReversal,
                                                         directionJustChangedThisTick, desiredTargetIsZero);

    if (currentSpeedPercent == 0) {
      if (strcmp(reason, "UNKNOWN_POSSIBLE_INVARIANT_VIOLATION") == 0) {
        everUnknown = true;
        everUnexplainedStopOutsideKnownWindows = true;
        counters.invariantViolations++;
        counters.unexpectedStoppedMs += TICK_MS;
      }
      if (state == SimState::DECELERATING) sawDecelZero = true;
    }
    if (directionJustChangedThisTick) sawHandoffTick = true;
    if (coastingForReversal) sawReversalCoastActive = true;

    if (state == SimState::INTENSITY_SWAY && !coastingForReversal && currentSpeedPercent == intensityTargetPercent &&
        intensityTargetPercent != 0 && currentDirection == pendingDirection && sawHandoffTick) {
      break;  // resumed normally -- sequence complete
    }
  }

  check(!everUnknown, "no tick in the full exit->decelerate->handoff->resume sequence is classified UNKNOWN_POSSIBLE_INVARIANT_VIOLATION");
  check(counters.invariantViolations == 0, "the mirrored invariant-violation counter stays at 0 throughout");
  check(counters.unexpectedStoppedMs == 0, "the mirrored unexpected-stopped-time counter stays at 0ms throughout");
  check(sawDecelZero, "sanity: currentSpeedPercent does legitimately reach 0 during DECELERATING (target fell to 0)");
  check(sawHandoffTick, "sanity: the direction-handoff tick (directionStartMs==now) does occur");
  check(sawReversalCoastActive, "sanity: the reversal coast (coastingForReversal) is exercised before the handoff tick");
  check(!everUnexplainedStopOutsideKnownWindows, "currentSpeedPercent==0 only ever occurs during DECELERATING, the coast, or the single handoff tick");
  check(currentDirection == pendingDirection, "movement resumes in the intended (reversed) direction");
  check(currentSpeedPercent == 80, "movement resumes at the correct (M80) target speed");
}

static void test_direction_handoff_tick_specifically_classified() {
  // Isolates the exact reported tick: state==INTENSITY_SWAY,
  // currentSpeedPercent==0, intensityTargetPercent==M80 (nonzero),
  // coastingForReversal just cleared THIS tick (directionJustChangedThisTick=true).
  const char *reason = classifyIntentionalStopReason(/*currentSpeedIsZero=*/true, /*lastStopWasEmergency=*/false,
                                                       /*phraseDecelerating=*/false, /*phraseCoasting=*/false,
                                                       /*stateIsDecelerating=*/false, /*dutyRest=*/false,
                                                       /*coastingForReversal=*/false, /*directionJustChangedThisTick=*/true,
                                                       /*desiredTargetIsZero=*/false);
  check(strcmp(reason, "direction_change_handoff") == 0, "the exact reported tick classifies as direction_change_handoff, not UNKNOWN");
}

static void test_ordinary_decelerating_state_gets_its_own_label() {
  const char *reason = classifyIntentionalStopReason(true, false, false, false, /*stateIsDecelerating=*/true, false, false, false, false);
  check(strcmp(reason, "deceleration_handoff") == 0, "MusicMotorState::DECELERATING gets its own precise label, not UNKNOWN");
}

static void test_still_coasting_is_not_the_handoff_tick() {
  // While the coast is STILL in progress (not yet resolved), the correct
  // label remains reversal_coast, never direction_change_handoff or UNKNOWN.
  const char *reason = classifyIntentionalStopReason(true, false, false, false, false, false, /*coastingForReversal=*/true,
                                                       /*directionJustChangedThisTick=*/false, false);
  check(strcmp(reason, "reversal_coast") == 0, "a still-in-progress coast is labeled reversal_coast");
}

static void test_genuine_unexplained_stall_still_reports_unknown() {
  // "Do not suppress the message blindly" -- a genuinely inexplicable
  // stall (none of the legitimate transitional conditions true, target
  // nonzero, speed zero) must still surface as UNKNOWN so a REAL future
  // regression is not silently hidden by this fix.
  const char *reason = classifyIntentionalStopReason(true, false, false, false, false, false, false, false,
                                                       /*desiredTargetIsZero=*/false);
  check(strcmp(reason, "UNKNOWN_POSSIBLE_INVARIANT_VIOLATION") == 0,
        "a genuinely unexplained stall (no legitimate transition, nonzero target) still reports UNKNOWN -- not suppressed");
}

// ----------------------------------------------------------------------------
// Mirrors exitSustainedDrive()'s new sustainedDriveLowEnergySinceMs reset.
// ----------------------------------------------------------------------------
struct ExitSustainedDriveState {
  unsigned long lowEnergySinceMs;
};
void mirrorExitSustainedDrive(ExitSustainedDriveState &s) {
  s.lowEnergySinceMs = 0;  // the fix
}

static void test_low_energy_grace_cleared_on_exit() {
  ExitSustainedDriveState s;
  s.lowEnergySinceMs = 12345;  // was ACTIVE (nonzero) at the moment of exit -- e.g. exited BECAUSE of low_energy_grace_expired
  mirrorExitSustainedDrive(s);
  check(s.lowEnergySinceMs == 0, "exitSustainedDrive() clears sustainedDriveLowEnergySinceMs -- 'Low-energy grace' no longer shows stale ACTIVE after exit");
}

int main() {
  test_full_exit_decel_handoff_resume_sequence();
  test_direction_handoff_tick_specifically_classified();
  test_ordinary_decelerating_state_gets_its_own_label();
  test_still_coasting_is_not_the_handoff_tick();
  test_genuine_unexplained_stall_still_reports_unknown();
  test_low_energy_grace_cleared_on_exit();

  if (g_failures == 0) {
    printf("All music_motor_diagnostic_handoff tests passed.\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
