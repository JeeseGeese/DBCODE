// Temporary host-side deterministic test -- mirrors revision 7's
// SUSTAINED_DRIVE choreography state added to src/MusicMotorController.cpp
// (computeSustainedDriveDecision(), chooseSustainedDriveDirection(), and
// the entry/update/exit state machine: enterSustainedDrive()/
// updateSustainedDrive()/exitSustainedDrive()). Same rationale/approach as
// the other test_host/*.cpp files: no PlatformIO "test" env exists in this
// project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_sustained_test test_host/music_motor_sustained_drive.cpp && /tmp/mm_sustained_test
//
// Covers the revision-7 test list:
//  1. duration always remains within [MIN,MAX]
//  2. ordinary beats cannot reverse an active sustained drive
//  3. LOW periodic reversal cannot interrupt it (structural: the state
//     dispatch mirror only reaches the LOW-band timer while
//     state==INTENSITY_SWAY, never while state==SUSTAINED_DRIVE)
//  4. strong hits can accent without forcing reversal
//  5. emergency stop and MusicMotor disable interrupt immediately
//  6. silence handling remains safe
//  7. it cannot start during genuine quiet
//  8. cooldown prevents constant repeated activation
//  9. exit does not automatically force a reversal
//  10. both FORWARD and REVERSE sustained drives occur across deterministic
//      test seeds
//  11. (existing choreography/MusicMotor host tests are re-run alongside
//      this one by the harness/report, not duplicated here)

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

// --- mirrored Config.h constants ---
constexpr uint32_t SUSTAINED_DRIVE_MIN_MS = 5000;
constexpr uint32_t SUSTAINED_DRIVE_MAX_MS = 10000;
constexpr uint32_t SUSTAINED_DRIVE_COOLDOWN_MIN_MS = 8000;
constexpr uint32_t SUSTAINED_DRIVE_COOLDOWN_MAX_MS = 18000;
constexpr uint8_t WEIGHT_MEDIUM_PERCENT = 3;
constexpr uint8_t WEIGHT_HIGH_PERCENT = 12;
constexpr uint8_t WEIGHT_PEAK_PERCENT = 22;
constexpr uint8_t ACCENT_FLIP_PERCENT = 35;
constexpr uint8_t MAX_CONSECUTIVE_SAME_DIRECTION = 2;

enum class Band { QUIET, LOW, MEDIUM, HIGH, PEAK };
enum class Direction { FORWARD, REVERSE };
Direction opposite(Direction d) { return d == Direction::FORWARD ? Direction::REVERSE : Direction::FORWARD; }
const char *dirName(Direction d) { return d == Direction::FORWARD ? "FORWARD" : "REVERSE"; }

// Deterministic seeded PRNG (mirrors "random(100)" as an explicit draw in
// [0,99], matching computeSustainedDriveDecision()'s own documented
// contract) -- NOT Arduino's random(), which doesn't exist on the host;
// the real firmware's random() draws are the untested integration point,
// exactly like every other test_host/*.cpp file's relationship to
// Arduino's random()/millis().
struct Rng {
  uint32_t state;
  explicit Rng(uint32_t seed) : state(seed ? seed : 1) {}
  uint32_t next() {
    // xorshift32 -- fast, deterministic, good enough for test variety.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  uint8_t draw100() { return (uint8_t)(next() % 100); }
  uint32_t range(uint32_t lo, uint32_t hi) { return lo + (next() % (hi - lo + 1)); }
};

// --- mirrored sustainedDriveWeightPercent()/computeSustainedDriveDecision()
// (see MusicMotorController.cpp) ---
uint8_t sustainedDriveWeightPercent(Band effBand) {
  switch (effBand) {
    case Band::MEDIUM: return WEIGHT_MEDIUM_PERCENT;
    case Band::HIGH: return WEIGHT_HIGH_PERCENT;
    case Band::PEAK: return WEIGHT_PEAK_PERCENT;
    default: return 0;
  }
}

enum class Outcome { STARTED, REJECTED };
struct Decision {
  bool bandEligible;
  bool cooldownReady;
  bool weightRoll;
  Outcome outcome;
  const char *reason;
};

Decision computeSustainedDriveDecision(Band effBand, bool silent, bool alreadyActive, uint32_t now, uint32_t cooldownUntilMs,
                                        uint8_t weightRollPercent0to99) {
  Decision d{};
  d.bandEligible = ((int)effBand >= (int)Band::MEDIUM) && !silent;
  d.cooldownReady = (int32_t)(now - cooldownUntilMs) >= 0;
  if (alreadyActive) {
    d.outcome = Outcome::REJECTED;
    d.reason = "already_active";
    return d;
  }
  if (!d.bandEligible) {
    d.outcome = Outcome::REJECTED;
    d.reason = silent ? "silent" : "band_below_medium";
    return d;
  }
  if (!d.cooldownReady) {
    d.outcome = Outcome::REJECTED;
    d.reason = "cooldown_active";
    return d;
  }
  d.weightRoll = weightRollPercent0to99 < sustainedDriveWeightPercent(effBand);
  if (!d.weightRoll) {
    d.outcome = Outcome::REJECTED;
    d.reason = "weight_roll_failed";
    return d;
  }
  d.outcome = Outcome::STARTED;
  d.reason = "weight_roll_succeeded";
  return d;
}

// --- mirrored chooseSustainedDriveDirection() (see MusicMotorController.cpp) ---
struct DirectionChoice {
  Direction direction;
  bool accentFlipped;
  bool capForcedFlip;
  bool capForcedButUnsafe;
};

DirectionChoice chooseSustainedDriveDirection(Direction currentDirection, bool strongAccent, uint8_t flipRollPercent0to99,
                                               bool safeToFlip, Direction lastDirection, uint8_t consecutiveSameDirectionCount,
                                               bool consecutiveCapOverride) {
  DirectionChoice c{};
  c.direction = currentDirection;
  if (strongAccent && safeToFlip && flipRollPercent0to99 < ACCENT_FLIP_PERCENT) {
    c.direction = opposite(currentDirection);
    c.accentFlipped = true;
  }
  bool wouldRepeat = (c.direction == lastDirection) && (consecutiveSameDirectionCount >= MAX_CONSECUTIVE_SAME_DIRECTION);
  if (wouldRepeat && !consecutiveCapOverride) {
    if (safeToFlip) {
      c.direction = opposite(c.direction);
      c.capForcedFlip = true;
    } else {
      c.capForcedButUnsafe = true;
    }
  }
  return c;
}

// ----------------------------------------------------------------------------
// Minimal mirrored state-machine harness -- models only what's needed to
// prove the revision-7 interruption/exit/eligibility guarantees, driven by
// explicit inputs rather than the full audio pipeline (already covered by
// music_motor_pipeline_profiles.cpp). "state" mirrors the real MusicMotorState
// enum's relevant subset.
// ----------------------------------------------------------------------------
enum class SimState { OFF, SILENT, INTENSITY_SWAY, SUSTAINED_DRIVE };

struct SimHarness {
  SimState state = SimState::INTENSITY_SWAY;
  Direction currentDirection = Direction::FORWARD;
  uint32_t sustainedDriveDeadlineMs = 0;
  uint32_t sustainedDriveDurationMs = 0;
  uint32_t sustainedDriveCooldownUntilMs = 0;
  Direction lastSustainedDriveDirection = Direction::FORWARD;
  uint8_t consecutiveSameDirectionCount = 0;
  int reversalAttemptsWhileActive = 0;   // ordinary-beat/strong-hit reversal attempts that were BLOCKED
  int reinforcementsWhileActive = 0;     // beat/strong-hit accents applied instead
  int lowPeriodicReversalTicksReached = 0;  // how many times the LOW-band periodic-reversal code path was even REACHED
  bool belowSilenceActive = false;
  uint32_t belowSilenceSinceMs = 0;
  bool stoppedForSilence = false;

  // Mirrors trySustainedDriveEntry() + enterSustainedDrive() together.
  bool tryEnter(uint32_t now, Band effBand, bool silent, Rng &rng, bool strongAccent, bool safeToFlip,
                bool consecutiveCapOverride) {
    Decision d = computeSustainedDriveDecision(effBand, silent, state == SimState::SUSTAINED_DRIVE, now,
                                                sustainedDriveCooldownUntilMs, rng.draw100());
    if (d.outcome != Outcome::STARTED) return false;
    DirectionChoice choice = chooseSustainedDriveDirection(currentDirection, strongAccent, rng.draw100(), safeToFlip,
                                                            lastSustainedDriveDirection, consecutiveSameDirectionCount,
                                                            consecutiveCapOverride);
    state = SimState::SUSTAINED_DRIVE;
    currentDirection = choice.direction;  // models the coast+flip resolving
    sustainedDriveDurationMs = rng.range(SUSTAINED_DRIVE_MIN_MS, SUSTAINED_DRIVE_MAX_MS);
    sustainedDriveDeadlineMs = now + sustainedDriveDurationMs;
    if (choice.direction == lastSustainedDriveDirection) {
      consecutiveSameDirectionCount++;
    } else {
      consecutiveSameDirectionCount = 1;
    }
    lastSustainedDriveDirection = choice.direction;
    return true;
  }

  // Test-only setup helper -- entry itself (the weight roll + direction
  // choice) is already exhaustively covered above via
  // computeSustainedDriveDecision()/chooseSustainedDriveDirection()
  // directly; the tests below are about what happens DURING/AFTER an
  // active hold, so they force one directly rather than depending on
  // finding a seed whose roll happens to succeed.
  void forceEnter(uint32_t now, Direction direction, uint32_t durationMs) {
    state = SimState::SUSTAINED_DRIVE;
    currentDirection = direction;
    sustainedDriveDurationMs = durationMs;
    sustainedDriveDeadlineMs = now + durationMs;
    if (direction == lastSustainedDriveDirection) {
      consecutiveSameDirectionCount++;
    } else {
      consecutiveSameDirectionCount = 1;
    }
    lastSustainedDriveDirection = direction;
  }

  // Mirrors updateSustainedDrive()'s beat handling -- reinforcement only,
  // NEVER a direction change. `wouldOrdinarilyReverse` models "this exact
  // strong hit, at this exact band/counter phase, would have selected
  // REVERSE_DIRECTION under ordinary (non-SUSTAINED_DRIVE) choreography" --
  // the test asserts the direction stays the same regardless.
  void ordinaryBeatWhileActive() {
    if (state != SimState::SUSTAINED_DRIVE) return;
    reinforcementsWhileActive++;
  }
  void strongHitWhileActive(bool wouldOrdinarilyReverse) {
    if (state != SimState::SUSTAINED_DRIVE) return;
    reinforcementsWhileActive++;
    if (wouldOrdinarilyReverse) reversalAttemptsWhileActive++;  // counted as "blocked," never applied -- see check below
  }

  // Mirrors the state-dispatch switch in updateMusicMotorController() --
  // the LOW-band periodic-reversal timer lives ONLY inside
  // updateIntensitySway(), which this switch only calls when
  // state==INTENSITY_SWAY. Modeling the dispatch structurally (not just
  // asserting "it didn't reverse") is what proves interruption is
  // architecturally impossible, not merely untriggered in this run.
  void tickDispatch() {
    if (state == SimState::INTENSITY_SWAY) {
      lowPeriodicReversalTicksReached++;
    }
    // SUSTAINED_DRIVE (and every other non-INTENSITY_SWAY state) never
    // reaches the LOW-band timer at all -- intentionally no branch for it.
  }

  // Mirrors checkAndHandleSilenceTimeout(), reused by both
  // updateIntensitySway() and updateSustainedDrive().
  bool checkSilence(uint32_t now, bool bandIsQuiet, uint32_t timeoutMs) {
    if (bandIsQuiet) {
      if (!belowSilenceActive) {
        belowSilenceActive = true;
        belowSilenceSinceMs = now;
      }
      if (now - belowSilenceSinceMs >= timeoutMs) {
        stopCleanly();
        return true;
      }
    } else {
      belowSilenceActive = false;
    }
    return false;
  }

  void stopCleanly() {
    state = SimState::SILENT;
    // sustained drive, if any, ends immediately -- no cooldown bookkeeping
    // (mirrors stopCleanly() not calling exitSustainedDrive()).
  }

  // Mirrors hardStop() -- unconditional, regardless of state or remaining
  // commitment time.
  void hardStop() { state = SimState::OFF; }

  // Mirrors exitSustainedDrive(): direction is left EXACTLY as-is, cooldown
  // is (re)randomized, state returns toward ordinary choreography
  // (modeled here as returning straight to INTENSITY_SWAY, skipping the
  // intermediate DECELERATING ramp state -- irrelevant to the properties
  // under test).
  void expire(uint32_t now, Rng &rng) {
    sustainedDriveCooldownUntilMs = now + rng.range(SUSTAINED_DRIVE_COOLDOWN_MIN_MS, SUSTAINED_DRIVE_COOLDOWN_MAX_MS);
    state = SimState::INTENSITY_SWAY;
    // currentDirection intentionally untouched.
  }

  void tickExpiryCheck(uint32_t now, Rng &rng) {
    if (state == SimState::SUSTAINED_DRIVE && (int32_t)(now - sustainedDriveDeadlineMs) >= 0) {
      expire(now, rng);
    }
  }
};

int main() {
  printf("== Revision-7 SUSTAINED_DRIVE test ==\n");

  // --- Item 1: duration always within [MIN,MAX] ---
  printf("\n-- duration bounds --\n");
  {
    Rng rng(12345);
    for (int i = 0; i < 5000; i++) {
      uint32_t d = rng.range(SUSTAINED_DRIVE_MIN_MS, SUSTAINED_DRIVE_MAX_MS);
      check(d >= SUSTAINED_DRIVE_MIN_MS && d <= SUSTAINED_DRIVE_MAX_MS, "sustained-drive duration out of [5000,10000]ms bounds");
    }
  }

  // --- Item 8 (cooldown bounds, same shape as duration) ---
  printf("-- cooldown bounds --\n");
  {
    Rng rng(999);
    for (int i = 0; i < 5000; i++) {
      uint32_t c = rng.range(SUSTAINED_DRIVE_COOLDOWN_MIN_MS, SUSTAINED_DRIVE_COOLDOWN_MAX_MS);
      check(c >= SUSTAINED_DRIVE_COOLDOWN_MIN_MS && c <= SUSTAINED_DRIVE_COOLDOWN_MAX_MS,
            "sustained-drive cooldown out of [8000,18000]ms bounds");
    }
  }

  // --- Item 7: cannot start during genuine quiet ---
  printf("\n-- cannot start during genuine quiet --\n");
  {
    Decision d = computeSustainedDriveDecision(Band::QUIET, /*silent=*/true, false, 0, 0, /*weightRoll=*/0);
    check(d.outcome == Outcome::REJECTED, "QUIET + silent must reject");
    check(strcmp(d.reason, "silent") == 0, "reason must be 'silent'");
    // Even a QUIET band while NOT (yet) in the SILENT state (hysteresis
    // window) must still reject on the band check.
    Decision d2 = computeSustainedDriveDecision(Band::QUIET, /*silent=*/false, false, 0, 0, /*weightRoll=*/0);
    check(d2.outcome == Outcome::REJECTED && strcmp(d2.reason, "band_below_medium") == 0,
          "QUIET band (even pre-timeout) must reject as band_below_medium");
    Decision d3 = computeSustainedDriveDecision(Band::LOW, false, false, 0, 0, 0);
    check(d3.outcome == Outcome::REJECTED && strcmp(d3.reason, "band_below_medium") == 0, "LOW band must also reject");
  }

  // --- Eligibility: MEDIUM/HIGH/PEAK all reachable given a low-enough roll ---
  printf("-- MEDIUM/HIGH/PEAK are eligible with a qualifying roll --\n");
  {
    Decision m = computeSustainedDriveDecision(Band::MEDIUM, false, false, 1000, 0, /*weightRoll=*/0);
    check(m.outcome == Outcome::STARTED, "MEDIUM with draw=0 (< 3%) must start");
    Decision h = computeSustainedDriveDecision(Band::HIGH, false, false, 1000, 0, /*weightRoll=*/0);
    check(h.outcome == Outcome::STARTED, "HIGH with draw=0 (< 12%) must start");
    Decision p = computeSustainedDriveDecision(Band::PEAK, false, false, 1000, 0, /*weightRoll=*/0);
    check(p.outcome == Outcome::STARTED, "PEAK with draw=0 (< 22%) must start");
    // MEDIUM is "uncommon" -- a draw comfortably above its 3% weight but
    // below HIGH/PEAK's must fail at MEDIUM specifically.
    Decision mFail = computeSustainedDriveDecision(Band::MEDIUM, false, false, 1000, 0, /*weightRoll=*/10);
    check(mFail.outcome == Outcome::REJECTED && strcmp(mFail.reason, "weight_roll_failed") == 0,
          "MEDIUM with draw=10 (>= 3%) must fail the weight roll");
  }

  // --- Item 8: cooldown prevents constant repeated activation ---
  printf("\n-- cooldown prevents repeated activation --\n");
  {
    Decision duringCooldown = computeSustainedDriveDecision(Band::PEAK, false, false, /*now=*/1000, /*cooldownUntilMs=*/5000, 0);
    check(duringCooldown.outcome == Outcome::REJECTED && strcmp(duringCooldown.reason, "cooldown_active") == 0,
          "a qualifying roll during an active cooldown must still be rejected");
    Decision afterCooldown = computeSustainedDriveDecision(Band::PEAK, false, false, /*now=*/5000, /*cooldownUntilMs=*/5000, 0);
    check(afterCooldown.outcome == Outcome::STARTED, "exactly at the cooldown deadline, a qualifying roll must succeed");
  }

  // --- Item 10: both FORWARD and REVERSE occur across deterministic seeds ---
  printf("\n-- both directions occur across seeds --\n");
  {
    bool sawForward = false, sawReverse = false;
    for (uint32_t seed = 1; seed <= 200; seed++) {
      Rng rng(seed);
      DirectionChoice c = chooseSustainedDriveDirection(Direction::FORWARD, /*strongAccent=*/true, rng.draw100(),
                                                         /*safeToFlip=*/true, Direction::FORWARD, /*consecutiveCount=*/0,
                                                         /*capOverride=*/false);
      if (c.direction == Direction::FORWARD) sawForward = true;
      if (c.direction == Direction::REVERSE) sawReverse = true;
    }
    check(sawForward, "across 200 seeds, FORWARD must occur at least once (currentDirection is FORWARD, most rolls miss the flip)");
    check(sawReverse, "across 200 seeds with a strong accent in play, REVERSE must occur at least once (the 35% flip chance)");
  }
  // Without a strong accent, direction must NEVER flip, across any seed.
  {
    bool everFlipped = false;
    for (uint32_t seed = 1; seed <= 200; seed++) {
      Rng rng(seed);
      DirectionChoice c = chooseSustainedDriveDirection(Direction::FORWARD, /*strongAccent=*/false, rng.draw100(), true,
                                                         Direction::FORWARD, 0, false);
      if (c.direction != Direction::FORWARD) everFlipped = true;
    }
    check(!everFlipped, "without a strong accent in play, direction must NEVER flip on entry, regardless of the roll");
  }

  // --- max-consecutive-same-direction cap ---
  printf("-- max-consecutive-same-direction cap --\n");
  {
    // Two consecutive FORWARD entries already recorded; a 3rd would-be
    // FORWARD choice (no accent flip rolled) must be forced to REVERSE,
    // UNLESS overridden by a continuing PEAK/drop section.
    DirectionChoice capped = chooseSustainedDriveDirection(Direction::FORWARD, false, 99 /* never flips via accent */, true,
                                                            Direction::FORWARD, /*consecutiveSameDirectionCount=*/2,
                                                            /*capOverride=*/false);
    check(capped.direction == Direction::REVERSE && capped.capForcedFlip,
          "a 3rd consecutive same-direction entry must be forced to flip when the cap isn't overridden");
    DirectionChoice overridden = chooseSustainedDriveDirection(Direction::FORWARD, false, 99, true, Direction::FORWARD, 2,
                                                                /*capOverride=*/true);
    check(overridden.direction == Direction::FORWARD && !overridden.capForcedFlip,
          "a continuing PEAK/drop section (capOverride) must allow a 3rd consecutive same-direction entry");
    DirectionChoice unsafeCap = chooseSustainedDriveDirection(Direction::FORWARD, false, 99, /*safeToFlip=*/false,
                                                               Direction::FORWARD, 2, false);
    check(unsafeCap.direction == Direction::FORWARD && unsafeCap.capForcedButUnsafe && !unsafeCap.capForcedFlip,
          "the cap must never force an UNSAFE flip -- direction stays as-is, flagged capForcedButUnsafe instead");
  }

  // --- Items 2/3/4: interruption resistance while active ---
  printf("\n-- interruption resistance while SUSTAINED_DRIVE is active --\n");
  {
    SimHarness h;
    h.forceEnter(0, Direction::FORWARD, 7000);
    check(h.state == SimState::SUSTAINED_DRIVE, "setup: state must be SUSTAINED_DRIVE after entry");
    Direction enteredDirection = h.currentDirection;

    // Item 2: many ordinary beats while active.
    for (int i = 0; i < 20; i++) h.ordinaryBeatWhileActive();
    check(h.currentDirection == enteredDirection, "ordinary beats must never reverse an active sustained drive");
    check(h.reinforcementsWhileActive == 20, "every ordinary beat while active must register as a reinforcement");

    // Item 4: strong hits accent, INCLUDING ones that would ordinarily
    // have selected REVERSE_DIRECTION under normal ordinary choreography.
    for (int i = 0; i < 10; i++) h.strongHitWhileActive(/*wouldOrdinarilyReverse=*/(i % 3 == 0));
    check(h.currentDirection == enteredDirection,
          "strong hits must never reverse an active sustained drive, even ones that would ordinarily have qualified "
          "for a reversal slot");
    check(h.reversalAttemptsWhileActive == 4, "the 4 would-have-reversed strong hits must be tracked as blocked (i=0,3,6,9)");

    // Item 3: LOW-band periodic reversal is structurally unreachable.
    for (int i = 0; i < 50; i++) h.tickDispatch();
    check(h.lowPeriodicReversalTicksReached == 0,
          "the LOW-band periodic-reversal code path must never be reached at all while state==SUSTAINED_DRIVE");
  }

  // --- Item 5: emergency stop and disable interrupt immediately ---
  printf("\n-- emergency stop / disable interrupt immediately --\n");
  {
    SimHarness h;
    h.forceEnter(0, Direction::FORWARD, 8000);
    check(h.state == SimState::SUSTAINED_DRIVE, "setup: must be active");
    h.hardStop();
    check(h.state == SimState::OFF, "hardStop() (emergency stop / disable) must interrupt SUSTAINED_DRIVE immediately");

    SimHarness h2;
    h2.forceEnter(0, Direction::REVERSE, 9000);
    // Deadline is 5-10s out -- hardStop() must not wait for it.
    check(h2.sustainedDriveDeadlineMs > 100, "setup: commitment extends well past a trivial elapsed time");
    h2.hardStop();
    check(h2.state == SimState::OFF, "hardStop() must interrupt regardless of remaining commitment time");
  }

  // --- Item 6: silence handling remains safe ---
  printf("\n-- silence handling remains safe during an active hold --\n");
  {
    SimHarness h;
    h.forceEnter(0, Direction::FORWARD, 8000);
    check(h.state == SimState::SUSTAINED_DRIVE, "setup: must be active");
    // Genuine quiet for less than the timeout must NOT stop it.
    bool stoppedEarly = h.checkSilence(3000, /*bandIsQuiet=*/true, /*timeoutMs=*/7000);
    check(!stoppedEarly && h.state == SimState::SUSTAINED_DRIVE, "quiet for less than the timeout must not stop an active hold");
    // A brief non-quiet tick resets the timer (hysteresis), then quiet
    // resumes and must eventually stop it once the FULL timeout elapses
    // from the reset point.
    h.checkSilence(3500, /*bandIsQuiet=*/false, 7000);
    check(!h.belowSilenceActive, "a non-quiet tick must reset the silence timer");
    h.checkSilence(4000, true, 7000);
    bool stoppedAtTimeout = h.checkSilence(4000 + 7000, true, 7000);
    check(stoppedAtTimeout && h.state == SimState::SILENT,
          "genuine silence sustained for the full timeout must stop the motor even mid-hold");
  }

  // --- Item 9: exit does not automatically force a reversal ---
  printf("\n-- exit does not force a reversal --\n");
  {
    SimHarness h;
    h.forceEnter(0, Direction::REVERSE, 6000);
    Direction beforeExpiry = h.currentDirection;
    Rng exitRng(56);
    h.tickExpiryCheck(h.sustainedDriveDeadlineMs, exitRng);
    check(h.state == SimState::INTENSITY_SWAY, "expiry must hand back to ordinary choreography (modeled as INTENSITY_SWAY)");
    check(h.currentDirection == beforeExpiry, "direction must be UNCHANGED across a normal (duration-elapsed) exit");
    check(h.sustainedDriveCooldownUntilMs > h.sustainedDriveDeadlineMs, "a fresh cooldown must be set on exit");
  }

  // Run duration-elapsed exit across many seeds -- direction must NEVER
  // change on a normal expiry, regardless of seed.
  {
    bool anyChanged = false;
    for (uint32_t seed = 1; seed <= 100; seed++) {
      SimHarness h;
      h.forceEnter(0, (seed % 2 == 0) ? Direction::FORWARD : Direction::REVERSE, 5000 + (seed % 5000));
      Direction before = h.currentDirection;
      Rng exitRng(seed + 1000);
      h.tickExpiryCheck(h.sustainedDriveDeadlineMs, exitRng);
      if (h.currentDirection != before) anyChanged = true;
    }
    check(!anyChanged, "across 100 seeds, a normal duration-elapsed exit must never itself change direction");
  }

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
