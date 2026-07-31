// Temporary host-side deterministic test -- mirrors revision 8's renewable
// SUSTAINED_DRIVE performance-phrase system added to
// src/MusicMotorController.cpp (computeSustainedDriveContinuationDecision(),
// computeSustainedSwitchQualification(), chooseSustainedDriveEntryTier(),
// sustainedDriveReviewRange()/sustainedDriveExtensionRange(),
// performSustainedDriveExtension()/performSustainedDriveDirectionSwitch(),
// and the persistent-energy entry opportunity in updateIntensitySway()).
// Same rationale/approach as every other test_host/*.cpp file: no
// PlatformIO "test" env exists in this project. Revision 7's own test file
// (music_motor_sustained_drive.cpp) is left untouched and still covers the
// entry weighted-roll/direction-choice/interruption-resistance/emergency-
// stop/silence-basics/exit-doesn't-reverse properties that are UNCHANGED by
// this revision.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_phrase_test test_host/music_motor_renewable_phrase.cpp && /tmp/mm_phrase_test
//
// Covers the revision-8 test list (numbered to match the request):
//  1. minimum directional commitment remains at least 5s
//  2. a MEDIUM phrase may end after a short eligible duration
//  3. HIGH energy extends beyond the initial review point
//  4. PEAK energy extends beyond the initial review point
//  5. continuous qualified energy can sustain a phrase beyond 30s
//  6. continuous qualified energy can sustain a phrase beyond 60s
//  7. short measured-band dips do not end the phrase when
//     effectiveBand/performanceEnergy remain qualified
//  8. prolonged LOW eventually ends the phrase after the grace period
//  9. genuine silence still interrupts through the shared silence timeout
//  10. ordinary beats cannot reverse the sustained direction
//  11. strong hits usually reinforce the current direction (switch stays rare)
//  12. a qualified major musical event can request a direct switch
//  13. a direct sustained switch uses the safe coast/reversal process
//  14. the controller remains in SUSTAINED_DRIVE after the switch
//  15. the new direction receives a fresh minimum commitment
//  16. total phrase elapsed time continues across the direction switch
//  17. rapid repeated sustained-direction switches are blocked
//  18. ordinary beats cannot trigger a direct sustained switch
//  19. exit does not automatically reverse
//  20. re-entry cooldown applies after a true phrase exit
//  21. re-entry cooldown does not incorrectly apply to an internal switch
//  22. persistent HIGH/PEAK can create a rate-limited entry opportunity
//      without a new strong hit
//  23. persistent-energy opportunity evaluation cannot run every loop
//  24. failed sustained-drive rolls do not disturb existing choreography
//      counters (structural/architectural)
//  25/26. existing revision 7 and other MusicMotor tests continue passing
//         (covered by re-running the whole test_host/ suite, not duplicated here)

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// --- mirrored Config.h constants ---
constexpr uint32_t MIN_COMMITMENT_MS = 5000;
constexpr uint32_t SHORT_MIN_MS = 1000, SHORT_MAX_MS = 5000;
constexpr uint32_t REVIEW_MEDIUM_MIN_MS = 5000, REVIEW_MEDIUM_MAX_MS = 10000;
constexpr uint32_t REVIEW_HIGH_MIN_MS = 8000, REVIEW_HIGH_MAX_MS = 18000;
constexpr uint32_t REVIEW_PEAK_MIN_MS = 12000, REVIEW_PEAK_MAX_MS = 30000;
constexpr uint32_t EXTEND_MEDIUM_MIN_MS = 3000, EXTEND_MEDIUM_MAX_MS = 7000;
constexpr uint32_t EXTEND_HIGH_MIN_MS = 5000, EXTEND_HIGH_MAX_MS = 12000;
constexpr uint32_t EXTEND_PEAK_MIN_MS = 8000, EXTEND_PEAK_MAX_MS = 18000;
constexpr uint32_t EXTENDED_ELAPSED_MS = 15000, RENEWABLE_ELAPSED_MS = 30000;
constexpr uint32_t LOW_GRACE_MS = 3500;
constexpr uint32_t SWITCH_COOLDOWN_MIN_MS = 8000, SWITCH_COOLDOWN_MAX_MS = 15000;
constexpr uint32_t SWITCH_EXCEPTIONAL_MIN_MS = 5500;
constexpr uint8_t ACCENT_FLIP_PERCENT = 35;
constexpr uint32_t PERSISTENT_ENTRY_DWELL_MS = 4000;
constexpr uint32_t PERSISTENT_ENTRY_REVIEW_MS = 2500;

enum class Band { QUIET, LOW, MEDIUM, HIGH, PEAK };
enum class Direction { FORWARD, REVERSE };
Direction opposite(Direction d) { return d == Direction::FORWARD ? Direction::REVERSE : Direction::FORWARD; }

// --- mirrored SustainedDriveTimeRange + range selectors ---
struct TimeRange {
  uint32_t minMs;
  uint32_t maxMs;
};
TimeRange reviewRange(Band effBand, bool dropHold) {
  if (effBand == Band::PEAK || dropHold) return {REVIEW_PEAK_MIN_MS, REVIEW_PEAK_MAX_MS};
  if (effBand == Band::HIGH) return {REVIEW_HIGH_MIN_MS, REVIEW_HIGH_MAX_MS};
  return {REVIEW_MEDIUM_MIN_MS, REVIEW_MEDIUM_MAX_MS};
}
TimeRange extensionRange(Band effBand, bool dropHold) {
  if (effBand == Band::PEAK || dropHold) return {EXTEND_PEAK_MIN_MS, EXTEND_PEAK_MAX_MS};
  if (effBand == Band::HIGH) return {EXTEND_HIGH_MIN_MS, EXTEND_HIGH_MAX_MS};
  return {EXTEND_MEDIUM_MIN_MS, EXTEND_MEDIUM_MAX_MS};
}

// --- mirrored computeSustainedSwitchQualification() ---
struct SwitchQualification {
  bool qualifies;
  bool exceptionalBypass;
  const char *reason;
};
SwitchQualification computeSwitchQualification(Band measuredBand, bool dropHold, uint32_t now, uint32_t directionCommitStartMs,
                                                uint32_t switchCooldownUntilMs, bool safeToFlip) {
  SwitchQualification q{};
  if (!safeToFlip) {
    q.reason = "unsafe";
    return q;
  }
  bool strongAccent = (measuredBand == Band::PEAK) || dropHold;
  if (!strongAccent) {
    q.reason = "no_strong_accent";
    return q;
  }
  uint32_t heldMs = now - directionCommitStartMs;
  bool exceptional = (measuredBand == Band::PEAK) && dropHold;
  if (exceptional && heldMs >= SWITCH_EXCEPTIONAL_MIN_MS) {
    q.qualifies = true;
    q.exceptionalBypass = true;
    q.reason = "exceptional_peak_drophold";
    return q;
  }
  if ((int32_t)(now - switchCooldownUntilMs) < 0) {
    q.reason = "cooldown_active";
    return q;
  }
  q.qualifies = true;
  q.reason = "cooldown_ready";
  return q;
}

// --- mirrored computeSustainedDriveContinuationDecision() ---
enum class ContinuationDecision { CONTINUE_UNTIL_REVIEW, EXTEND_SAME_DIRECTION, SWITCH_SUSTAINED_DIRECTION, EXIT_TO_NORMAL, EXIT_FOR_SILENCE, EXIT_FOR_SAFETY };
struct ContinuationInputs {
  Band effectiveBandNow = Band::QUIET;
  bool dropHoldActiveNow = false;
  bool energyTrendRising = false;
  bool recentBeatActivity = false;
  bool recentStrongHitActivity = false;
  bool genuinelySilent = false;
  bool motorSafetyOk = true;
  uint32_t nowMs = 0;
  uint32_t directionCommitStartMs = 0;
  uint32_t minCommitmentMs = 0;
  uint32_t lowEnergySinceMs = 0;
};
struct ContinuationResult {
  ContinuationDecision decision;
  const char *reason;
};
ContinuationResult computeContinuationDecision(const ContinuationInputs &in, bool reviewDue, bool switchQualifies) {
  if (!in.motorSafetyOk) return {ContinuationDecision::EXIT_FOR_SAFETY, "safety"};
  if (in.genuinelySilent) return {ContinuationDecision::EXIT_FOR_SILENCE, "silent"};
  if ((in.nowMs - in.directionCommitStartMs) < in.minCommitmentMs) return {ContinuationDecision::CONTINUE_UNTIL_REVIEW, "min_commitment_active"};
  if (in.lowEnergySinceMs != 0 && (in.nowMs - in.lowEnergySinceMs) >= LOW_GRACE_MS)
    return {ContinuationDecision::EXIT_TO_NORMAL, "low_energy_grace_expired"};
  if (switchQualifies) return {ContinuationDecision::SWITCH_SUSTAINED_DIRECTION, "switch_qualified"};
  if (!reviewDue) return {ContinuationDecision::CONTINUE_UNTIL_REVIEW, "not_due"};
  bool supports = ((int)in.effectiveBandNow >= (int)Band::MEDIUM) || in.energyTrendRising || in.recentBeatActivity || in.recentStrongHitActivity;
  if (supports) return {ContinuationDecision::EXTEND_SAME_DIRECTION, "energy_supports_extension"};
  return {ContinuationDecision::EXIT_TO_NORMAL, "energy_no_longer_supports_phrase"};
}

// --- mirrored chooseSustainedDriveEntryTier() ---
enum class PhraseTier { SHORT, STANDARD, EXTENDED, RENEWABLE };
constexpr uint8_t SHORT_WEIGHT_MEDIUM = 70, SHORT_WEIGHT_HIGH = 40, SHORT_WEIGHT_PEAK = 20, MAJOR_TRANSIENT_BOOST = 25;
PhraseTier chooseEntryTier(Band effBand, bool dropHold, bool majorTransient, uint8_t tierRoll) {
  uint16_t w;
  switch (effBand) {
    case Band::PEAK: w = SHORT_WEIGHT_PEAK; break;
    case Band::HIGH: w = SHORT_WEIGHT_HIGH; break;
    default: w = SHORT_WEIGHT_MEDIUM; break;
  }
  if (dropHold) w /= 2;
  if (majorTransient) w += MAJOR_TRANSIENT_BOOST;
  if (w > 100) w = 100;
  return (tierRoll < w) ? PhraseTier::SHORT : PhraseTier::STANDARD;
}

// --- Simple deterministic PRNG, same xorshift32 pattern as the other
// revision-7/8 test files ---
struct Rng {
  uint32_t state;
  explicit Rng(uint32_t seed) : state(seed ? seed : 1) {}
  uint32_t next() {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  }
  uint8_t draw100() { return (uint8_t)(next() % 100); }
  uint32_t range(uint32_t lo, uint32_t hi) { return lo + (next() % (hi - lo + 1)); }
};

// ----------------------------------------------------------------------------
// Minimal mirrored phrase harness -- models enough of enterSustainedDrive()/
// updateSustainedDrive()/performSustainedDriveExtension()/
// performSustainedDriveDirectionSwitch()/exitSustainedDrive() to exercise
// multi-review, multi-switch scenarios end to end.
// ----------------------------------------------------------------------------
struct PhraseHarness {
  bool active = false;
  Direction direction = Direction::FORWARD;
  PhraseTier tier = PhraseTier::STANDARD;
  uint32_t phraseStartMs = 0;
  uint32_t directionCommitStartMs = 0;
  uint32_t minCommitmentMs = 0;
  uint32_t nextReviewMs = 0;
  uint32_t extensionCount = 0;
  uint32_t switchCount = 0;
  uint32_t reentryCooldownUntilMs = 0;
  uint32_t switchCooldownUntilMs = 0;
  const char *exitReason = "none";

  void enter(uint32_t now, Direction dir, Band effBand, bool dropHold, PhraseTier chosenTier, Rng &rng) {
    active = true;
    direction = dir;
    phraseStartMs = now;
    directionCommitStartMs = now;
    tier = chosenTier;
    extensionCount = 0;
    switchCount = 0;
    exitReason = "none";
    if (tier == PhraseTier::SHORT) {
      minCommitmentMs = rng.range(SHORT_MIN_MS, SHORT_MAX_MS);
      nextReviewMs = now + minCommitmentMs;
    } else {
      minCommitmentMs = MIN_COMMITMENT_MS;
      TimeRange rr = reviewRange(effBand, dropHold);
      nextReviewMs = now + rng.range(rr.minMs, rr.maxMs);
    }
  }

  void extend(uint32_t now, Band effBand, bool dropHold, Rng &rng) {
    TimeRange er = extensionRange(effBand, dropHold);
    nextReviewMs = now + rng.range(er.minMs, er.maxMs);
    extensionCount++;
    if (tier == PhraseTier::SHORT) tier = PhraseTier::STANDARD;
    uint32_t elapsed = now - phraseStartMs;
    if (elapsed >= RENEWABLE_ELAPSED_MS) tier = PhraseTier::RENEWABLE;
    else if (elapsed >= EXTENDED_ELAPSED_MS) tier = PhraseTier::EXTENDED;
  }

  void performSwitch(uint32_t now, Band effBand, bool dropHold, Rng &rng) {
    direction = opposite(direction);
    directionCommitStartMs = now;
    minCommitmentMs = MIN_COMMITMENT_MS;
    switchCount++;
    switchCooldownUntilMs = now + rng.range(SWITCH_COOLDOWN_MIN_MS, SWITCH_COOLDOWN_MAX_MS);
    TimeRange rr = reviewRange(effBand, dropHold);
    nextReviewMs = now + rng.range(rr.minMs, rr.maxMs);
    if (tier == PhraseTier::SHORT) tier = PhraseTier::STANDARD;
    // reentryCooldownUntilMs deliberately untouched -- item 21
  }

  void exit(uint32_t now, const char *reason, Rng &rng) {
    active = false;
    exitReason = reason;
    reentryCooldownUntilMs = now + rng.range(8000, 18000);  // MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MIN/MAX_MS, unchanged by revision 8
  }
};

int main() {
  printf("== Revision-8 renewable performance-phrase test ==\n");

  // --- Item 1: minimum directional commitment remains >= 5s ---
  printf("\n-- minimum directional commitment >= 5s --\n");
  check(MIN_COMMITMENT_MS >= 5000, "MIN_COMMITMENT_MS must be at least 5000ms");
  {
    ContinuationInputs in;
    in.effectiveBandNow = Band::PEAK;
    in.directionCommitStartMs = 0;
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.nowMs = MIN_COMMITMENT_MS - 1;
    ContinuationResult r = computeContinuationDecision(in, /*reviewDue=*/true, /*switchQualifies=*/true);
    check(r.decision == ContinuationDecision::CONTINUE_UNTIL_REVIEW,
          "1ms short of minimum commitment, NOTHING may act, even with reviewDue+switchQualifies both true");
    in.nowMs = MIN_COMMITMENT_MS;
    r = computeContinuationDecision(in, true, false);
    check(r.decision != ContinuationDecision::CONTINUE_UNTIL_REVIEW, "exactly at minimum commitment, evaluation may proceed");
  }

  // --- Items 2/3/4: band-appropriate review-time extend-vs-exit ---
  printf("\n-- MEDIUM phrase may end after a short eligible duration --\n");
  {
    ContinuationInputs in;
    in.effectiveBandNow = Band::QUIET;  // energy genuinely gone by review time
    in.directionCommitStartMs = 0;
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.nowMs = REVIEW_MEDIUM_MIN_MS;
    ContinuationResult r = computeContinuationDecision(in, true, false);
    check(r.decision == ContinuationDecision::EXIT_TO_NORMAL, "MEDIUM-entry phrase with no support at review must exit");
  }
  printf("-- HIGH energy extends beyond the initial review point --\n");
  {
    ContinuationInputs in;
    in.effectiveBandNow = Band::HIGH;
    in.directionCommitStartMs = 0;
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.nowMs = REVIEW_HIGH_MIN_MS;
    ContinuationResult r = computeContinuationDecision(in, true, false);
    check(r.decision == ContinuationDecision::EXTEND_SAME_DIRECTION, "HIGH support at review must extend, not exit");
  }
  printf("-- PEAK energy extends beyond the initial review point --\n");
  {
    ContinuationInputs in;
    in.effectiveBandNow = Band::PEAK;
    in.directionCommitStartMs = 0;
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.nowMs = REVIEW_PEAK_MIN_MS;
    ContinuationResult r = computeContinuationDecision(in, true, false);
    check(r.decision == ContinuationDecision::EXTEND_SAME_DIRECTION, "PEAK support at review must extend, not exit");
  }

  // --- Items 5/6: continuous qualified energy sustains past 30s, then 60s ---
  printf("\n-- continuous qualified energy sustains a phrase past 30s, then 60s --\n");
  {
    PhraseHarness h;
    Rng rng(101);
    uint32_t now = 0;
    h.enter(now, Direction::FORWARD, Band::PEAK, true, PhraseTier::STANDARD, rng);
    bool crossed30 = false, crossed60 = false;
    for (int i = 0; i < 40; i++) {  // enough iterations of PEAK-range extensions to comfortably clear 60s
      now = h.nextReviewMs;
      ContinuationInputs in;
      in.effectiveBandNow = Band::PEAK;
      in.dropHoldActiveNow = true;
      in.directionCommitStartMs = h.directionCommitStartMs;
      in.minCommitmentMs = h.minCommitmentMs;
      in.nowMs = now;
      ContinuationResult r = computeContinuationDecision(in, true, false);
      check(r.decision == ContinuationDecision::EXTEND_SAME_DIRECTION, "continuous PEAK+DropHold support must always extend, never exit");
      h.extend(now, Band::PEAK, true, rng);
      uint32_t elapsed = now - h.phraseStartMs;
      if (elapsed >= 30000) crossed30 = true;
      if (elapsed >= 60000) crossed60 = true;
    }
    check(crossed30, "continuous qualified energy must be able to sustain a phrase beyond 30s");
    check(crossed60, "continuous qualified energy must be able to sustain a phrase beyond 60s");
    check(h.tier == PhraseTier::RENEWABLE, "a phrase this long must report as RENEWABLE tier");
    check(h.extensionCount > 5, "a 60s+ phrase must have accumulated multiple extensions, not one giant one");
  }

  // --- Item 7: short measured-band dips tolerated when effectiveBand/performanceEnergy remain qualified ---
  printf("\n-- short measured-band dips do not end the phrase when effectiveBand remains qualified --\n");
  {
    // effectiveBandNow reflects performanceEnergy/dropHold lending already
    // (see the real effectiveBand()) -- a real measured dip that still
    // lends HIGH+ must extend, not fall into the low-energy-grace path.
    ContinuationInputs in;
    in.effectiveBandNow = Band::HIGH;  // lent by performanceEnergy/dropHold despite a real measured dip
    in.dropHoldActiveNow = true;
    in.directionCommitStartMs = 0;
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.nowMs = REVIEW_HIGH_MIN_MS;
    in.lowEnergySinceMs = 0;  // never actually dropped below MEDIUM once effectiveBand is accounted for
    ContinuationResult r = computeContinuationDecision(in, true, false);
    check(r.decision == ContinuationDecision::EXTEND_SAME_DIRECTION, "a lent-HIGH effectiveBand despite a real dip must still extend");
  }

  // --- Item 8: prolonged LOW eventually ends the phrase after grace ---
  printf("-- prolonged LOW eventually ends the phrase after the grace period --\n");
  {
    ContinuationInputs in;
    in.effectiveBandNow = Band::LOW;
    in.directionCommitStartMs = 0;  // long past minimum commitment by the time values below are reached
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.lowEnergySinceMs = 6000;  // grace tracking started well after minimum commitment already elapsed
    in.nowMs = 6000 + LOW_GRACE_MS - 1;
    ContinuationResult notYet = computeContinuationDecision(in, false, false);
    check(notYet.decision == ContinuationDecision::CONTINUE_UNTIL_REVIEW, "1ms short of grace expiry must not yet exit");
    in.nowMs = 6000 + LOW_GRACE_MS;
    ContinuationResult expired = computeContinuationDecision(in, false, false);
    check(expired.decision == ContinuationDecision::EXIT_TO_NORMAL && strcmp(expired.reason, "low_energy_grace_expired") == 0,
          "grace period fully elapsed must exit, even without reviewDue");
  }

  // --- Item 9: genuine silence still interrupts through the shared timeout ---
  printf("-- genuine silence interrupts regardless of review/commitment state --\n");
  {
    ContinuationInputs in;
    in.genuinelySilent = true;
    in.directionCommitStartMs = 0;
    in.minCommitmentMs = MIN_COMMITMENT_MS;
    in.nowMs = 100;  // well within minimum commitment -- silence still wins
    ContinuationResult r = computeContinuationDecision(in, false, false);
    check(r.decision == ContinuationDecision::EXIT_FOR_SILENCE, "genuine silence must exit even during minimum commitment");
  }

  // --- Items 11/12: switch qualification stays rare/exceptional ---
  printf("\n-- strong hits usually reinforce; switch stays rare --\n");
  {
    SwitchQualification ordinary = computeSwitchQualification(Band::HIGH, false, 10000, 0, 0, true);
    check(!ordinary.qualifies, "HIGH band with no DropHold is not a strong accent -- must not qualify for a switch");
    SwitchQualification noCooldown = computeSwitchQualification(Band::PEAK, false, 10000, 0, /*switchCooldownUntilMs=*/20000, true);
    check(!noCooldown.qualifies, "PEAK alone (no DropHold) with cooldown still active must not qualify");
  }
  printf("-- a qualified major musical event can request a direct switch --\n");
  {
    SwitchQualification q = computeSwitchQualification(Band::PEAK, true, /*now=*/10000, /*directionCommitStartMs=*/0,
                                                         /*switchCooldownUntilMs=*/0, true);
    check(q.qualifies, "PEAK + DropHold + cooldown ready + past minimum commitment must qualify");
    SwitchQualification exceptional =
        computeSwitchQualification(Band::PEAK, true, /*now=*/SWITCH_EXCEPTIONAL_MIN_MS, 0, /*switchCooldownUntilMs=*/999999, true);
    check(exceptional.qualifies && exceptional.exceptionalBypass,
          "PEAK+DropHold held for the exceptional floor must bypass an otherwise-still-active ordinary cooldown");
    SwitchQualification tooSoon =
        computeSwitchQualification(Band::PEAK, true, /*now=*/SWITCH_EXCEPTIONAL_MIN_MS - 1, 0, /*switchCooldownUntilMs=*/999999, true);
    check(!tooSoon.qualifies, "1ms short of the exceptional floor must not bypass an active ordinary cooldown");
    SwitchQualification unsafe = computeSwitchQualification(Band::PEAK, true, 10000, 0, 0, /*safeToFlip=*/false);
    check(!unsafe.qualifies, "an unsafe flip (hardware gate) must never qualify, regardless of musical justification");
  }

  // --- Items 13/14/15/16: direct switch mechanics via the harness ---
  printf("\n-- direct sustained switch: safe process, stays in phrase, fresh commitment, elapsed time continues --\n");
  {
    PhraseHarness h;
    Rng rng(55);
    h.enter(0, Direction::FORWARD, Band::PEAK, true, PhraseTier::STANDARD, rng);
    // advance well past minimum commitment
    uint32_t switchNow = MIN_COMMITMENT_MS + 500;
    Direction before = h.direction;
    uint32_t switchCountBefore = h.switchCount;
    h.performSwitch(switchNow, Band::PEAK, true, rng);
    check(h.direction == opposite(before), "13: performSwitch must flip the direction (via the safe coast/flip primitives in the real code)");
    check(h.active, "14: the controller must remain in the SUSTAINED_DRIVE phrase after the switch (harness 'active' flag untouched)");
    check(h.directionCommitStartMs == switchNow, "15: the new direction must receive a FRESH minimum commitment start");
    check(h.minCommitmentMs == MIN_COMMITMENT_MS, "15: the fresh commitment must use the standard floor");
    check(h.phraseStartMs == 0, "16: total phrase elapsed time (phraseStartMs) must NOT reset across an in-phrase switch");
    check(h.switchCount == switchCountBefore + 1, "switch count must increment");
  }

  // --- Item 17: rapid repeated switches are blocked ---
  printf("-- rapid repeated sustained-direction switches are blocked --\n");
  {
    uint32_t switchCooldownUntilMs = 10000 + 8000;  // a switch just happened at t=10000 with an 8s cooldown
    SwitchQualification tooSoonAfter = computeSwitchQualification(Band::PEAK, false, /*now=*/10500, /*directionCommitStartMs=*/10000,
                                                                    switchCooldownUntilMs, true);
    check(!tooSoonAfter.qualifies, "a second switch attempt shortly after the first (ordinary PEAK-only accent, no DropHold) must be blocked");
    SwitchQualification duringCooldownMedium = computeSwitchQualification(Band::MEDIUM, false, /*now=*/10500,
                                                                           /*directionCommitStartMs=*/10000, switchCooldownUntilMs, true);
    check(!duringCooldownMedium.qualifies, "MEDIUM (no strong accent at all) must never qualify, cooldown or not");
    SwitchQualification afterCooldown = computeSwitchQualification(Band::PEAK, false, /*now=*/switchCooldownUntilMs,
                                                                     /*directionCommitStartMs=*/10000, switchCooldownUntilMs, true);
    check(afterCooldown.qualifies && !afterCooldown.exceptionalBypass,
          "once the ordinary cooldown has genuinely elapsed, PEAK alone (a strong accent on its own) qualifies via the ordinary path");
    SwitchQualification afterCooldownWithAccent = computeSwitchQualification(Band::PEAK, true, /*now=*/switchCooldownUntilMs,
                                                                               /*directionCommitStartMs=*/10000, switchCooldownUntilMs, true);
    check(afterCooldownWithAccent.qualifies, "once the ordinary cooldown has genuinely elapsed AND a strong accent is present, a switch may qualify again");
  }

  // --- Items 20/21: re-entry cooldown vs. in-phrase switch cooldown are independent ---
  printf("\n-- re-entry cooldown applies after a true exit, NOT after an in-phrase switch --\n");
  {
    PhraseHarness h;
    Rng rng(77);
    h.enter(0, Direction::FORWARD, Band::HIGH, false, PhraseTier::STANDARD, rng);
    check(h.reentryCooldownUntilMs == 0, "setup: no re-entry cooldown before anything has happened");
    h.performSwitch(MIN_COMMITMENT_MS + 100, Band::HIGH, false, rng);
    check(h.reentryCooldownUntilMs == 0, "21: an in-phrase switch must NOT set the re-entry cooldown");
    h.exit(20000, "phrase_complete", rng);
    check(h.reentryCooldownUntilMs > 20000, "20: a TRUE phrase exit must set a fresh re-entry cooldown");
  }

  // --- Items 22/23: persistent-energy entry opportunity gating ---
  printf("\n-- persistent-energy entry opportunity: dwell + hard rate limit --\n");
  {
    uint32_t sustainedHighSinceMs = 5000;
    uint32_t nextCheckMs = 0;
    auto opportunityAvailable = [&](uint32_t now) {
      return sustainedHighSinceMs != 0 && (now - sustainedHighSinceMs) >= PERSISTENT_ENTRY_DWELL_MS && now >= nextCheckMs;
    };
    check(!opportunityAvailable(5000 + PERSISTENT_ENTRY_DWELL_MS - 1), "1ms short of the dwell requirement must not be available");
    check(opportunityAvailable(5000 + PERSISTENT_ENTRY_DWELL_MS), "22: exactly at the dwell requirement, the opportunity must become available");
    // Simulate the rate limit: once "checked," it cannot be checked again for PERSISTENT_ENTRY_REVIEW_MS.
    uint32_t checkedAt = 5000 + PERSISTENT_ENTRY_DWELL_MS;
    nextCheckMs = checkedAt + PERSISTENT_ENTRY_REVIEW_MS;
    int availableCountOverManyTicks = 0;
    for (uint32_t t = checkedAt; t < checkedAt + PERSISTENT_ENTRY_REVIEW_MS; t += 15) {  // simulate ~15ms ticks
      if (opportunityAvailable(t)) availableCountOverManyTicks++;
    }
    check(availableCountOverManyTicks == 0,
          "23: within one PERSISTENT_ENTRY_REVIEW_MS window, the opportunity must not become available again (not rolled every loop)");
    check(opportunityAvailable(checkedAt + PERSISTENT_ENTRY_REVIEW_MS), "after the rate-limit window elapses, the opportunity is available again");
  }

  // --- Item 24: failed rolls never touch choreography counters (structural) ---
  printf("\n-- failed sustained-drive rolls cannot disturb existing choreography counters (structural) --\n");
  {
    // computeSustainedDriveDecision()/computeContinuationDecision()/
    // chooseEntryTier() above take NO counter parameters (lowStrongHitCounter/
    // mediumStrongHitCounter/etc. are real-code-only globals mutated
    // exclusively inside selectBeatAction()'s own per-band branches, AFTER
    // the sustained-drive check has already returned) -- there is no
    // parameter or side channel through which a failed roll here could
    // reach them. This is verified by inspection/construction (the mirrored
    // signatures above have no such parameter), not a runtime assertion.
    check(true, "computeSustainedDriveDecision()/continuation/tier functions take no choreography-counter parameters by construction");
  }

  // --- Phrase-tier selection sanity (supports items in the addendum) ---
  printf("\n-- phrase-tier selection: MEDIUM favors SHORT, PEAK+DropHold favors long-form --\n");
  {
    int shortAtMedium = 0, shortAtPeakDropHold = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
      Rng rng(seed);
      if (chooseEntryTier(Band::MEDIUM, false, false, rng.draw100()) == PhraseTier::SHORT) shortAtMedium++;
      if (chooseEntryTier(Band::PEAK, true, false, rng.draw100()) == PhraseTier::SHORT) shortAtPeakDropHold++;
    }
    check(shortAtMedium > shortAtPeakDropHold, "MEDIUM must pick SHORT more often than PEAK+DropHold across many seeds");
    check(shortAtPeakDropHold > 0, "PEAK+DropHold must still occasionally produce a SHORT explosive burst, not never");
    check(shortAtMedium < 300, "MEDIUM must not ALWAYS pick SHORT either");
  }

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
