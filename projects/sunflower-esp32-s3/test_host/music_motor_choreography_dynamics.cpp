// Temporary host-side deterministic test -- mirrors revision 10's physical
// choreography/dynamic-range refinement added to src/MusicMotorController.cpp
// (classifyMotionTier(), computeSpeedAuthorityCap(), computeQuietBuildupQualification(),
// computeMotionDutyTransition(), pickDropEntryChancePercent(),
// selectDropPhraseType(), buildDropPhraseSteps()). Same rationale/approach as
// every other test_host/*.cpp file: no PlatformIO "test" env exists in this
// project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_dynamics_test test_host/music_motor_choreography_dynamics.cpp && /tmp/mm_dynamics_test
//
// Covers the revision 10 numbered test lists (both the dynamic-range list
// and the drop-phrase-vocabulary clarification list):
//  1. a quiet room with no musical activity does not qualify as QUIET_BUILDUP
//  2. a quiet musical buildup (BUILDUP/DROP_ARMED phase + audio above the
//     room-noise floor) qualifies as QUIET_BUILDUP
//  3. QUIET_BUILDUP qualification requires audio above the noise floor even
//     during a nominally-BUILDUP phase (guards against silence mislabeled
//     as buildup)
//  4. LOW measured audio caps the commanded ceiling to the mellow cap
//     regardless of how high a lent/raw target is
//  5. QUIET measured audio caps to the quiet-buildup ceiling only after the
//     grace window elapses -- not immediately
//  6. MEDIUM measured audio bounds lending to at most one bounded raise
//     above the natural (unlent) MEDIUM target
//  7. HIGH/PEAK measured audio gets full (uncapped) authority
//  8. MotionTier classification -- MAJOR_DROP_DRIVE/CONFIRMED_DROP_DRIVE/
//     HIGH_ENERGY/GROOVE/MELLOW/QUIET_BUILDUP/REST each select correctly
//  9. movement duty-cycle alternates pulse/rest and honors window duration
//  10. a confirmed drop's entry chance escalates over time and eventually
//      reaches guaranteed
//  11. a major drop's entry chance is near-immediate from the very first
//      eligible tick, and guaranteed once effectiveBand is already >=MEDIUM
//      -- "cannot remain indefinitely in ordinary sway from random rolls"
//  12. DROP_BOOTY_SHAKE/SUSTAINED_REVERSAL are excluded from the candidate
//      set entirely when their per-drop limits are exhausted or the drop
//      hasn't been active long enough (eligibility gates randomness)
//  13. dense rhythmic evidence (high beatDensity+bassDensity) favors
//      DROP_BOOTY_SHAKE; smooth sustained evidence favors FULL_SUSTAIN
//  14. a fresh impact cue during an already-long sustained phrase favors
//      SUSTAINED_REVERSAL
//  15. anti-repeat weighting reduces (but does not eliminate) the most
//      recently used phrase's selection probability
//  16. buildDropPhraseSteps() produces the documented step shapes for every
//      phrase type (FULL_SUSTAIN/SUSTAINED_REVERSAL/DROP_PUNCH_AND_HOLD/
//      DOUBLE_PUNCH/DROP_BOOTY_SHAKE), each terminating at the drop's own
//      validated speed tier -- "major drops always receive high-energy
//      movement even when FULL_SUSTAIN is not selected"
//  17. DROP_BOOTY_SHAKE's step count stays within the configured bound and
//      alternates direction on every punch (never repeats the same
//      direction twice in a row)
//  18. no phrase step ever exceeds M100

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

// --- mirrored types/constants (see MusicMotorController.h/.cpp) ---
enum class MusicIntensityBand { BAND_QUIET, BAND_LOW, BAND_MEDIUM, BAND_HIGH, BAND_PEAK };
enum class MusicMotorDirection { FORWARD, REVERSE };
static MusicMotorDirection opposite(MusicMotorDirection d) {
  return d == MusicMotorDirection::FORWARD ? MusicMotorDirection::REVERSE : MusicMotorDirection::FORWARD;
}
enum class MusicalSectionPhase { NEUTRAL, BUILDUP, DROP_ARMED, DROP_IMPACT, DROP_ACTIVE, DROP_RELEASE };
enum class DropConfidenceTier { NONE, POSSIBLE_DROP, CONFIRMED_DROP, MAJOR_DROP };
enum class MotionTier { REST, QUIET_BUILDUP, MELLOW, GROOVE, HIGH_ENERGY, CONFIRMED_DROP_DRIVE, MAJOR_DROP_DRIVE };
enum class DropPhraseType { FULL_SUSTAIN, SUSTAINED_REVERSAL, DROP_BOOTY_SHAKE, DROP_PUNCH_AND_HOLD, DOUBLE_PUNCH, SUSTAIN_WITH_ACCENTS };

constexpr uint32_t QUIET_CAP_GRACE_MS = 700;
constexpr uint8_t MEDIUM_BOUNDED_RAISE_PERCENT = 6;
constexpr uint8_t MOTION_QUIET_BUILDUP_PERCENT = 80;
constexpr uint8_t MOTION_MELLOW_MAX_PERCENT = 83;
constexpr float ROOM_NOISE_FLOOR = 0.04f;
constexpr uint32_t QUIET_BUILDUP_MAX_QUIET_MS = 25000;

constexpr uint8_t ENTRY_INITIAL_PERCENT = 40;
constexpr uint8_t ENTRY_ESCALATED_PERCENT = 70;
constexpr uint8_t ENTRY_GUARANTEED_PERCENT = 100;
constexpr uint32_t ENTRY_ESCALATE_AFTER_MS = 2500;
constexpr uint32_t ENTRY_MAJOR_IMMEDIATE_PERCENT = 85;
constexpr uint32_t ENTRY_MAJOR_GUARANTEE_AFTER_MS = 1200;
constexpr uint32_t ENTRY_CONFIRMED_GUARANTEE_AFTER_MS = 3500;

constexpr uint8_t DROP_PHRASE_PUNCH_PERCENT = 100;
constexpr uint8_t MAX_DROP_PHRASE_STEPS = 4;
constexpr float ANTIREPEAT_WEIGHT_MULTIPLIER = 0.4f;

// --- mirrored pure functions ---

MotionTier classifyMotionTier(MusicIntensityBand measuredBand, MusicIntensityBand effBand, bool dropHoldActiveNow,
                               MusicalSectionPhase phase, DropConfidenceTier tier, bool relativeEnabled, bool quietBuildupQualifies) {
  bool relativeMajor = relativeEnabled && phase == MusicalSectionPhase::DROP_ACTIVE && tier == DropConfidenceTier::MAJOR_DROP;
  bool relativeConfirmed =
      relativeEnabled && phase == MusicalSectionPhase::DROP_ACTIVE && (int)tier >= (int)DropConfidenceTier::CONFIRMED_DROP;
  if ((measuredBand == MusicIntensityBand::BAND_PEAK && dropHoldActiveNow) || relativeMajor) return MotionTier::MAJOR_DROP_DRIVE;
  if (effBand == MusicIntensityBand::BAND_PEAK || dropHoldActiveNow || relativeConfirmed) return MotionTier::CONFIRMED_DROP_DRIVE;
  if (measuredBand == MusicIntensityBand::BAND_HIGH || effBand == MusicIntensityBand::BAND_HIGH) return MotionTier::HIGH_ENERGY;
  if (measuredBand == MusicIntensityBand::BAND_MEDIUM) return MotionTier::GROOVE;
  if (measuredBand == MusicIntensityBand::BAND_LOW) return MotionTier::MELLOW;
  return quietBuildupQualifies ? MotionTier::QUIET_BUILDUP : MotionTier::REST;
}

struct SpeedAuthorityCapResult {
  uint8_t ceilingPercent;
  const char *source;
};
SpeedAuthorityCapResult computeSpeedAuthorityCap(MusicIntensityBand measuredBand, bool quietGraceExpired, uint8_t quietCapPercent,
                                                  uint8_t mellowCapPercent, uint8_t naturalMediumPercent, uint8_t mediumBoundedRaisePercent) {
  switch (measuredBand) {
    case MusicIntensityBand::BAND_QUIET:
      if (!quietGraceExpired) return {100, "none_grace"};
      return {quietCapPercent, "quiet_cap"};
    case MusicIntensityBand::BAND_LOW:
      return {mellowCapPercent, "mellow_cap"};
    case MusicIntensityBand::BAND_MEDIUM: {
      uint16_t cap = (uint16_t)naturalMediumPercent + (uint16_t)mediumBoundedRaisePercent;
      if (cap > 100) cap = 100;
      return {(uint8_t)cap, "medium_bounded_raise_cap"};
    }
    default:
      return {100, "none_full_authority"};
  }
}

bool computeQuietBuildupQualification(bool relativeEnabled, MusicalSectionPhase phase, float audioEnergyNow, float roomNoiseFloor,
                                       unsigned long quietDurationMs, unsigned long maxQuietMs) {
  if (!relativeEnabled) return false;
  if (phase != MusicalSectionPhase::BUILDUP && phase != MusicalSectionPhase::DROP_ARMED) return false;
  if (audioEnergyNow <= roomNoiseFloor) return false;
  if (quietDurationMs > maxQuietMs) return false;
  return true;
}

struct MotionDutyTransitionResult {
  bool pulseOn;
  bool changed;
  unsigned long nextWindowEndMs;
};
MotionDutyTransitionResult computeMotionDutyTransition(bool currentPulseOn, unsigned long now, unsigned long windowEndMs,
                                                         uint32_t pulseDurationMs, uint32_t restDurationMs) {
  MotionDutyTransitionResult r{currentPulseOn, false, windowEndMs};
  if ((long)(now - windowEndMs) < 0) return r;
  r.pulseOn = !currentPulseOn;
  r.changed = true;
  r.nextWindowEndMs = now + (r.pulseOn ? pulseDurationMs : restDurationMs);
  return r;
}

struct DropEntryChanceResult {
  uint8_t percent;
  const char *escalation;
};
DropEntryChanceResult pickDropEntryChancePercent(unsigned long sinceActiveMs, DropConfidenceTier tier, bool effectiveBandAtLeastMedium) {
  if (tier == DropConfidenceTier::MAJOR_DROP) {
    if (sinceActiveMs >= ENTRY_MAJOR_GUARANTEE_AFTER_MS || effectiveBandAtLeastMedium) return {ENTRY_GUARANTEED_PERCENT, "guaranteed_major"};
    return {(uint8_t)ENTRY_MAJOR_IMMEDIATE_PERCENT, "major_immediate"};
  }
  if (sinceActiveMs >= ENTRY_CONFIRMED_GUARANTEE_AFTER_MS) return {ENTRY_GUARANTEED_PERCENT, "guaranteed_confirmed"};
  if (sinceActiveMs >= ENTRY_ESCALATE_AFTER_MS) return {ENTRY_ESCALATED_PERCENT, "escalated"};
  return {ENTRY_INITIAL_PERCENT, "initial"};
}

struct DropPhraseEvidence {
  float sustainedEnergyScore = 0.0f;
  float beatDensityScore = 0.0f;
  float bassDensityScore = 0.0f;
  float transientDensityScore = 0.0f;
  bool isReselection = false;
  bool freshImpactCue = false;
  unsigned long dropActiveElapsedMs = 0;
  uint8_t boothShakesUsedThisDrop = 0;
  uint8_t sustainedReversalsUsedThisDrop = 0;
  uint8_t maxBoothShakesPerDrop = 0;
  uint8_t maxSustainedReversalsPerDrop = 0;
  DropPhraseType lastPhraseUsed = DropPhraseType::FULL_SUSTAIN;
  bool hasLastPhrase = false;
  uint32_t minActiveMsForReversal = 0;
  uint32_t minActiveMsForShake = 0;
  float antiRepeatMultiplier = 1.0f;
};
struct DropPhraseChoice {
  DropPhraseType type;
  const char *reason;
};
struct Candidate {
  DropPhraseType type;
  uint16_t weight;
  const char *reason;
};
DropPhraseChoice selectDropPhraseType(const DropPhraseEvidence &e, uint16_t weightRoll0to999) {
  Candidate candidates[6];
  int n = 0;

  bool reversalEligible = e.sustainedReversalsUsedThisDrop < e.maxSustainedReversalsPerDrop && e.dropActiveElapsedMs >= e.minActiveMsForReversal;
  bool shakeEligible = e.boothShakesUsedThisDrop < e.maxBoothShakesPerDrop && e.dropActiveElapsedMs >= e.minActiveMsForShake;
  bool introPhraseEligible = !e.isReselection || e.freshImpactCue;

  uint16_t fullSustainWeight = 30;
  if (e.sustainedEnergyScore > 0.6f && e.beatDensityScore < 0.4f) fullSustainWeight += 25;
  candidates[n++] = {DropPhraseType::FULL_SUSTAIN, fullSustainWeight, "smooth_sustained_energy"};

  uint16_t accentsWeight = 20;
  if (e.beatDensityScore >= 0.3f && e.beatDensityScore < 0.7f) accentsWeight += 15;
  candidates[n++] = {DropPhraseType::SUSTAIN_WITH_ACCENTS, accentsWeight, "moderate_beat_density"};

  if (shakeEligible) {
    uint16_t shakeWeight = 10;
    if (e.beatDensityScore >= 0.6f && e.bassDensityScore >= 0.6f) shakeWeight += 40;
    candidates[n++] = {DropPhraseType::DROP_BOOTY_SHAKE, shakeWeight, "dense_bass_cluster"};
  }

  if (introPhraseEligible) {
    uint16_t punchWeight = e.isReselection ? 8 : 15;
    if (e.freshImpactCue) punchWeight += 15;
    candidates[n++] = {DropPhraseType::DROP_PUNCH_AND_HOLD, punchWeight, e.isReselection ? "fresh_impact_cue" : "dramatic_opening_impact"};
  }

  if (introPhraseEligible && e.transientDensityScore >= 0.5f) {
    uint16_t doubleWeight = (uint16_t)((e.isReselection ? 6 : 10) + (e.transientDensityScore * 20.0f));
    candidates[n++] = {DropPhraseType::DOUBLE_PUNCH, doubleWeight, "repeated_clustered_impacts"};
  }

  if (reversalEligible) {
    uint16_t reversalWeight = e.isReselection ? 12 : 6;
    if (e.isReselection && e.freshImpactCue) reversalWeight += 30;
    candidates[n++] = {DropPhraseType::SUSTAINED_REVERSAL, reversalWeight, "impact_during_long_phrase"};
  }

  if (e.hasLastPhrase && n > 1) {
    for (int i = 0; i < n; i++) {
      if (candidates[i].type == e.lastPhraseUsed) {
        candidates[i].weight = (uint16_t)(candidates[i].weight * e.antiRepeatMultiplier);
        if (candidates[i].weight == 0) candidates[i].weight = 1;
      }
    }
  }

  uint32_t total = 0;
  for (int i = 0; i < n; i++) total += candidates[i].weight;
  uint32_t pick = (total == 0) ? 0 : ((uint32_t)weightRoll0to999 * total) / 1000;
  uint32_t cursor = 0;
  for (int i = 0; i < n; i++) {
    cursor += candidates[i].weight;
    if (pick < cursor) return {candidates[i].type, candidates[i].reason};
  }
  return {DropPhraseType::FULL_SUSTAIN, "fallback"};
}

struct DropPhraseStep {
  bool isPunch;
  MusicMotorDirection direction;
  uint8_t targetPercent;
  uint32_t durationMs;
};
uint8_t buildDropPhraseSteps(DropPhraseType type, MusicMotorDirection currentDir, uint8_t dropSpeedPercent, uint32_t d1, uint32_t d2,
                              uint32_t d3, DropPhraseStep out[MAX_DROP_PHRASE_STEPS]) {
  MusicMotorDirection opp = opposite(currentDir);
  switch (type) {
    case DropPhraseType::FULL_SUSTAIN:
    case DropPhraseType::SUSTAIN_WITH_ACCENTS:
      out[0] = {false, currentDir, dropSpeedPercent, 0};
      return 1;
    case DropPhraseType::SUSTAINED_REVERSAL:
      out[0] = {false, opp, dropSpeedPercent, 0};
      return 1;
    case DropPhraseType::DROP_PUNCH_AND_HOLD:
      out[0] = {true, opp, DROP_PHRASE_PUNCH_PERCENT, d1};
      out[1] = {false, currentDir, dropSpeedPercent, 0};
      return 2;
    case DropPhraseType::DOUBLE_PUNCH:
      out[0] = {true, opp, DROP_PHRASE_PUNCH_PERCENT, d1};
      out[1] = {true, currentDir, DROP_PHRASE_PUNCH_PERCENT, d2};
      out[2] = {false, currentDir, dropSpeedPercent, 0};
      return 3;
    case DropPhraseType::DROP_BOOTY_SHAKE: {
      MusicMotorDirection d = opp;
      uint32_t durations[3] = {d1, d2, d3};
      for (int i = 0; i < 3; i++) {
        out[i] = {true, d, DROP_PHRASE_PUNCH_PERCENT, durations[i]};
        d = opposite(d);
      }
      out[3] = {false, d, dropSpeedPercent, 0};
      return 4;
    }
  }
  out[0] = {false, currentDir, dropSpeedPercent, 0};
  return 1;
}

// ----------------------------------------------------------------------------
static void test_quiet_room_no_music_does_not_qualify_buildup() {
  bool q = computeQuietBuildupQualification(true, MusicalSectionPhase::NEUTRAL, 0.0f, ROOM_NOISE_FLOOR, 5000, QUIET_BUILDUP_MAX_QUIET_MS);
  check(!q, "empty room (NEUTRAL phase, zero energy) does not qualify as quiet buildup");
}

static void test_quiet_musical_buildup_qualifies() {
  bool q = computeQuietBuildupQualification(true, MusicalSectionPhase::BUILDUP, 0.10f, ROOM_NOISE_FLOOR, 1000, QUIET_BUILDUP_MAX_QUIET_MS);
  check(q, "BUILDUP phase with audio above the room-noise floor qualifies as quiet buildup");
  bool q2 = computeQuietBuildupQualification(true, MusicalSectionPhase::DROP_ARMED, 0.10f, ROOM_NOISE_FLOOR, 1000, QUIET_BUILDUP_MAX_QUIET_MS);
  check(q2, "DROP_ARMED phase with audio above the room-noise floor also qualifies");
}

static void test_buildup_phase_but_silent_does_not_qualify() {
  bool q = computeQuietBuildupQualification(true, MusicalSectionPhase::BUILDUP, 0.01f, ROOM_NOISE_FLOOR, 1000, QUIET_BUILDUP_MAX_QUIET_MS);
  check(!q, "BUILDUP phase with audio AT/BELOW the room-noise floor does not qualify (guards against stale phase + true silence)");
}

static void test_low_band_caps_regardless_of_lent_target() {
  SpeedAuthorityCapResult cap = computeSpeedAuthorityCap(MusicIntensityBand::BAND_LOW, true, MOTION_QUIET_BUILDUP_PERCENT,
                                                          MOTION_MELLOW_MAX_PERCENT, 84, MEDIUM_BOUNDED_RAISE_PERCENT);
  check(cap.ceilingPercent == MOTION_MELLOW_MAX_PERCENT, "LOW measured band caps ceiling to the mellow cap");
  check(strcmp(cap.source, "mellow_cap") == 0, "LOW cap reports mellow_cap as the source");
}

static void test_quiet_cap_respects_grace_window() {
  SpeedAuthorityCapResult beforeGrace =
      computeSpeedAuthorityCap(MusicIntensityBand::BAND_QUIET, false, MOTION_QUIET_BUILDUP_PERCENT, MOTION_MELLOW_MAX_PERCENT, 84,
                                MEDIUM_BOUNDED_RAISE_PERCENT);
  check(beforeGrace.ceilingPercent == 100, "before the quiet grace window elapses, authority stays uncapped");
  SpeedAuthorityCapResult afterGrace =
      computeSpeedAuthorityCap(MusicIntensityBand::BAND_QUIET, true, MOTION_QUIET_BUILDUP_PERCENT, MOTION_MELLOW_MAX_PERCENT, 84,
                                MEDIUM_BOUNDED_RAISE_PERCENT);
  check(afterGrace.ceilingPercent == MOTION_QUIET_BUILDUP_PERCENT, "after the grace window elapses, QUIET caps to the quiet-buildup ceiling");
}

static void test_medium_bounded_raise() {
  uint8_t naturalMedium = 86;
  SpeedAuthorityCapResult cap = computeSpeedAuthorityCap(MusicIntensityBand::BAND_MEDIUM, true, MOTION_QUIET_BUILDUP_PERCENT,
                                                          MOTION_MELLOW_MAX_PERCENT, naturalMedium, MEDIUM_BOUNDED_RAISE_PERCENT);
  check(cap.ceilingPercent == naturalMedium + MEDIUM_BOUNDED_RAISE_PERCENT, "MEDIUM caps to natural + one bounded raise, not the full lent value");
  check(cap.ceilingPercent >= naturalMedium, "the MEDIUM cap never drops below the natural (unlent) target");
}

static void test_high_peak_full_authority() {
  SpeedAuthorityCapResult capHigh = computeSpeedAuthorityCap(MusicIntensityBand::BAND_HIGH, true, MOTION_QUIET_BUILDUP_PERCENT,
                                                              MOTION_MELLOW_MAX_PERCENT, 84, MEDIUM_BOUNDED_RAISE_PERCENT);
  SpeedAuthorityCapResult capPeak = computeSpeedAuthorityCap(MusicIntensityBand::BAND_PEAK, true, MOTION_QUIET_BUILDUP_PERCENT,
                                                              MOTION_MELLOW_MAX_PERCENT, 84, MEDIUM_BOUNDED_RAISE_PERCENT);
  check(capHigh.ceilingPercent == 100, "HIGH measured band gets full (uncapped) authority");
  check(capPeak.ceilingPercent == 100, "PEAK measured band gets full (uncapped) authority");
}

static void test_motion_tier_classification() {
  check(classifyMotionTier(MusicIntensityBand::BAND_PEAK, MusicIntensityBand::BAND_PEAK, true, MusicalSectionPhase::NEUTRAL,
                            DropConfidenceTier::NONE, false, false) == MotionTier::MAJOR_DROP_DRIVE,
        "absolute PEAK + active DropHold classifies as MAJOR_DROP_DRIVE");
  check(classifyMotionTier(MusicIntensityBand::BAND_MEDIUM, MusicIntensityBand::BAND_MEDIUM, false, MusicalSectionPhase::DROP_ACTIVE,
                            DropConfidenceTier::MAJOR_DROP, true, false) == MotionTier::MAJOR_DROP_DRIVE,
        "a relative MAJOR_DROP classifies as MAJOR_DROP_DRIVE even at measured MEDIUM");
  check(classifyMotionTier(MusicIntensityBand::BAND_MEDIUM, MusicIntensityBand::BAND_MEDIUM, false, MusicalSectionPhase::DROP_ACTIVE,
                            DropConfidenceTier::CONFIRMED_DROP, true, false) == MotionTier::CONFIRMED_DROP_DRIVE,
        "a relative CONFIRMED_DROP classifies as CONFIRMED_DROP_DRIVE");
  check(classifyMotionTier(MusicIntensityBand::BAND_HIGH, MusicIntensityBand::BAND_HIGH, false, MusicalSectionPhase::NEUTRAL,
                            DropConfidenceTier::NONE, false, false) == MotionTier::HIGH_ENERGY,
        "ordinary HIGH classifies as HIGH_ENERGY");
  check(classifyMotionTier(MusicIntensityBand::BAND_MEDIUM, MusicIntensityBand::BAND_MEDIUM, false, MusicalSectionPhase::NEUTRAL,
                            DropConfidenceTier::NONE, false, false) == MotionTier::GROOVE,
        "ordinary MEDIUM classifies as GROOVE");
  check(classifyMotionTier(MusicIntensityBand::BAND_LOW, MusicIntensityBand::BAND_LOW, false, MusicalSectionPhase::NEUTRAL,
                            DropConfidenceTier::NONE, false, false) == MotionTier::MELLOW,
        "ordinary LOW classifies as MELLOW");
  check(classifyMotionTier(MusicIntensityBand::BAND_QUIET, MusicIntensityBand::BAND_QUIET, false, MusicalSectionPhase::BUILDUP,
                            DropConfidenceTier::NONE, true, true) == MotionTier::QUIET_BUILDUP,
        "QUIET + qualifying buildup classifies as QUIET_BUILDUP");
  check(classifyMotionTier(MusicIntensityBand::BAND_QUIET, MusicIntensityBand::BAND_QUIET, false, MusicalSectionPhase::NEUTRAL,
                            DropConfidenceTier::NONE, true, false) == MotionTier::REST,
        "QUIET without a qualifying buildup classifies as REST");
}

static void test_duty_cycle_alternation() {
  bool pulseOn = false;
  unsigned long windowEnd = 1000;
  MotionDutyTransitionResult r = computeMotionDutyTransition(pulseOn, 500, windowEnd, 800, 4000);
  check(!r.changed, "duty window does not change before its own deadline");
  r = computeMotionDutyTransition(pulseOn, 1000, windowEnd, 800, 4000);
  check(r.changed && r.pulseOn, "duty window flips to PULSE_ON once its deadline is reached");
  check(r.nextWindowEndMs == 1000 + 800, "PULSE_ON window is sized by the pulse duration");
  r = computeMotionDutyTransition(true, 1800, r.nextWindowEndMs, 800, 4000);
  check(r.changed && !r.pulseOn, "duty window flips to REST once the pulse window elapses");
  check(r.nextWindowEndMs == 1800 + 4000, "REST window is sized by the rest duration");
}

static void test_confirmed_drop_entry_escalates() {
  DropEntryChanceResult initial = pickDropEntryChancePercent(500, DropConfidenceTier::CONFIRMED_DROP, false);
  DropEntryChanceResult escalated = pickDropEntryChancePercent(ENTRY_ESCALATE_AFTER_MS + 100, DropConfidenceTier::CONFIRMED_DROP, false);
  DropEntryChanceResult guaranteed =
      pickDropEntryChancePercent(ENTRY_CONFIRMED_GUARANTEE_AFTER_MS + 100, DropConfidenceTier::CONFIRMED_DROP, false);
  check(initial.percent == ENTRY_INITIAL_PERCENT, "confirmed-drop entry starts at the initial chance");
  check(escalated.percent == ENTRY_ESCALATED_PERCENT, "confirmed-drop entry escalates after ENTRY_ESCALATE_AFTER_MS");
  check(guaranteed.percent == ENTRY_GUARANTEED_PERCENT, "confirmed-drop entry reaches guaranteed after its own (shortened) timer");
}

static void test_major_drop_entry_near_immediate_and_guaranteed() {
  DropEntryChanceResult immediate = pickDropEntryChancePercent(10, DropConfidenceTier::MAJOR_DROP, false);
  check(immediate.percent == ENTRY_MAJOR_IMMEDIATE_PERCENT, "a major drop's very first eligible tick already has a high (not 40%) chance");
  DropEntryChanceResult bandGuaranteed = pickDropEntryChancePercent(10, DropConfidenceTier::MAJOR_DROP, true);
  check(bandGuaranteed.percent == ENTRY_GUARANTEED_PERCENT,
        "a major drop is guaranteed immediately once effectiveBand is already >=MEDIUM -- cannot lose entry to a random roll indefinitely");
  DropEntryChanceResult timeGuaranteed = pickDropEntryChancePercent(ENTRY_MAJOR_GUARANTEE_AFTER_MS + 1, DropConfidenceTier::MAJOR_DROP, false);
  check(timeGuaranteed.percent == ENTRY_GUARANTEED_PERCENT, "a major drop is guaranteed after its own short timer regardless of band");
}

static void test_ineligible_phrases_excluded_from_selection() {
  DropPhraseEvidence e{};
  e.isReselection = true;
  e.dropActiveElapsedMs = 1000;  // below both minActiveMsForReversal/Shake
  e.minActiveMsForReversal = 6000;
  e.minActiveMsForShake = 3000;
  e.maxBoothShakesPerDrop = 2;
  e.maxSustainedReversalsPerDrop = 1;
  e.beatDensityScore = 0.9f;
  e.bassDensityScore = 0.9f;  // would strongly favor DROP_BOOTY_SHAKE if eligible
  e.freshImpactCue = true;    // would favor SUSTAINED_REVERSAL if eligible
  for (uint16_t roll = 0; roll < 1000; roll += 7) {
    DropPhraseChoice choice = selectDropPhraseType(e, roll);
    check(choice.type != DropPhraseType::DROP_BOOTY_SHAKE, "DROP_BOOTY_SHAKE never selected before its minimum active duration");
    check(choice.type != DropPhraseType::SUSTAINED_REVERSAL, "SUSTAINED_REVERSAL never selected before its minimum active duration");
  }
}

static void test_exhausted_limits_exclude_phrase() {
  DropPhraseEvidence e{};
  e.isReselection = true;
  e.dropActiveElapsedMs = 30000;
  e.minActiveMsForReversal = 6000;
  e.minActiveMsForShake = 3000;
  e.maxBoothShakesPerDrop = 1;
  e.boothShakesUsedThisDrop = 1;  // limit already used
  e.maxSustainedReversalsPerDrop = 1;
  e.sustainedReversalsUsedThisDrop = 1;  // limit already used
  for (uint16_t roll = 0; roll < 1000; roll += 7) {
    DropPhraseChoice choice = selectDropPhraseType(e, roll);
    check(choice.type != DropPhraseType::DROP_BOOTY_SHAKE, "DROP_BOOTY_SHAKE excluded once its per-drop limit is exhausted");
    check(choice.type != DropPhraseType::SUSTAINED_REVERSAL, "SUSTAINED_REVERSAL excluded once its per-drop limit is exhausted");
  }
}

static void test_dense_rhythm_favors_booty_shake() {
  DropPhraseEvidence e{};
  e.isReselection = false;
  e.dropActiveElapsedMs = 10000;
  e.minActiveMsForShake = 3000;
  e.maxBoothShakesPerDrop = 2;
  e.beatDensityScore = 0.9f;
  e.bassDensityScore = 0.9f;
  int shakeCount = 0;
  const int trials = 1000;
  for (int i = 0; i < trials; i++) {
    DropPhraseChoice choice = selectDropPhraseType(e, (uint16_t)(i % 1000));
    if (choice.type == DropPhraseType::DROP_BOOTY_SHAKE) shakeCount++;
  }
  check(shakeCount > trials / 4, "dense beat+bass density makes DROP_BOOTY_SHAKE a substantial share of selections");
}

static void test_smooth_sustained_favors_full_sustain() {
  DropPhraseEvidence e{};
  e.isReselection = false;
  e.sustainedEnergyScore = 0.8f;
  e.beatDensityScore = 0.1f;
  int fullSustainCount = 0;
  const int trials = 1000;
  for (int i = 0; i < trials; i++) {
    DropPhraseChoice choice = selectDropPhraseType(e, (uint16_t)(i % 1000));
    if (choice.type == DropPhraseType::FULL_SUSTAIN) fullSustainCount++;
  }
  check(fullSustainCount > trials / 3, "smooth sustained energy + low beat density makes FULL_SUSTAIN a substantial share of selections");
}

static void test_fresh_impact_during_long_phrase_favors_reversal() {
  DropPhraseEvidence withCue{};
  withCue.isReselection = true;
  withCue.freshImpactCue = true;
  withCue.dropActiveElapsedMs = 20000;
  withCue.minActiveMsForReversal = 6000;
  withCue.maxSustainedReversalsPerDrop = 1;

  DropPhraseEvidence withoutCue = withCue;
  withoutCue.freshImpactCue = false;

  int reversalWithCue = 0, reversalWithoutCue = 0;
  const int trials = 1000;
  for (int i = 0; i < trials; i++) {
    if (selectDropPhraseType(withCue, (uint16_t)(i % 1000)).type == DropPhraseType::SUSTAINED_REVERSAL) reversalWithCue++;
    if (selectDropPhraseType(withoutCue, (uint16_t)(i % 1000)).type == DropPhraseType::SUSTAINED_REVERSAL) reversalWithoutCue++;
  }
  check(reversalWithCue > reversalWithoutCue, "a fresh impact cue during a long phrase increases SUSTAINED_REVERSAL's selection share");
}

static void test_antirepeat_reduces_but_does_not_eliminate() {
  DropPhraseEvidence e{};
  e.isReselection = false;
  e.hasLastPhrase = true;
  e.lastPhraseUsed = DropPhraseType::FULL_SUSTAIN;
  e.antiRepeatMultiplier = ANTIREPEAT_WEIGHT_MULTIPLIER;
  DropPhraseEvidence noHistory = e;
  noHistory.hasLastPhrase = false;

  int fullSustainWithHistory = 0, fullSustainNoHistory = 0;
  const int trials = 1000;
  for (int i = 0; i < trials; i++) {
    if (selectDropPhraseType(e, (uint16_t)(i % 1000)).type == DropPhraseType::FULL_SUSTAIN) fullSustainWithHistory++;
    if (selectDropPhraseType(noHistory, (uint16_t)(i % 1000)).type == DropPhraseType::FULL_SUSTAIN) fullSustainNoHistory++;
  }
  check(fullSustainWithHistory < fullSustainNoHistory, "anti-repeat weighting reduces the most-recently-used phrase's selection share");
  check(fullSustainWithHistory > 0, "anti-repeat weighting does not eliminate the phrase entirely -- it can still be selected");
}

static void test_phrase_step_shapes() {
  DropPhraseStep steps[MAX_DROP_PHRASE_STEPS];
  MusicMotorDirection cur = MusicMotorDirection::FORWARD;
  uint8_t dropSpeed = 100;

  uint8_t n = buildDropPhraseSteps(DropPhraseType::FULL_SUSTAIN, cur, dropSpeed, 0, 0, 0, steps);
  check(n == 1 && !steps[0].isPunch && steps[0].direction == cur && steps[0].targetPercent == dropSpeed,
        "FULL_SUSTAIN is a single terminal sustain step at the drop speed, same direction");

  n = buildDropPhraseSteps(DropPhraseType::SUSTAINED_REVERSAL, cur, dropSpeed, 0, 0, 0, steps);
  check(n == 1 && !steps[0].isPunch && steps[0].direction == opposite(cur) && steps[0].targetPercent == dropSpeed,
        "SUSTAINED_REVERSAL is a single terminal sustain step in the OPPOSITE direction, at the drop speed");

  n = buildDropPhraseSteps(DropPhraseType::DROP_PUNCH_AND_HOLD, cur, dropSpeed, 400, 0, 0, steps);
  check(n == 2 && steps[0].isPunch && steps[0].direction == opposite(cur) && steps[0].targetPercent == DROP_PHRASE_PUNCH_PERCENT,
        "DROP_PUNCH_AND_HOLD opens with an opposite-direction punch at the punch ceiling");
  check(!steps[1].isPunch && steps[1].direction == cur && steps[1].targetPercent == dropSpeed,
        "DROP_PUNCH_AND_HOLD settles into the original direction at the drop speed");

  n = buildDropPhraseSteps(DropPhraseType::DOUBLE_PUNCH, cur, dropSpeed, 400, 400, 0, steps);
  check(n == 3 && steps[0].direction == opposite(cur) && steps[1].direction == cur && !steps[2].isPunch && steps[2].direction == cur,
        "DOUBLE_PUNCH: opposite punch, original punch, then sustains the original direction");

  n = buildDropPhraseSteps(DropPhraseType::DROP_BOOTY_SHAKE, cur, dropSpeed, 400, 400, 400, steps);
  check(n == 4, "DROP_BOOTY_SHAKE produces exactly 4 segments (3 punches + terminal sustain)");
  for (uint8_t i = 1; i < n; i++) {
    check(steps[i].direction != steps[i - 1].direction, "DROP_BOOTY_SHAKE alternates direction on every single segment");
  }
  check(!steps[3].isPunch, "DROP_BOOTY_SHAKE's final segment is the terminal open-ended sustain");
}

static void test_every_phrase_terminates_at_drop_speed_never_below_m80() {
  // "Major drops always receive high-energy movement even when FULL_SUSTAIN
  // is not selected" -- every phrase type's terminal step lands at the
  // drop's own (already-validated) speed tier, never something weaker.
  DropPhraseType types[] = {DropPhraseType::FULL_SUSTAIN,     DropPhraseType::SUSTAINED_REVERSAL,  DropPhraseType::DROP_PUNCH_AND_HOLD,
                             DropPhraseType::DOUBLE_PUNCH,      DropPhraseType::DROP_BOOTY_SHAKE,    DropPhraseType::SUSTAIN_WITH_ACCENTS};
  uint8_t majorDropSpeed = 100;
  for (DropPhraseType t : types) {
    DropPhraseStep steps[MAX_DROP_PHRASE_STEPS];
    uint8_t n = buildDropPhraseSteps(t, MusicMotorDirection::FORWARD, majorDropSpeed, 400, 400, 400, steps);
    check(!steps[n - 1].isPunch, "every phrase type's LAST step is the terminal open-ended sustain step");
    check(steps[n - 1].targetPercent == majorDropSpeed, "every phrase type's terminal step targets the drop's own validated speed tier");
    for (uint8_t i = 0; i < n; i++) {
      check(steps[i].targetPercent <= 100, "no phrase step ever exceeds the validated M100 ceiling");
    }
  }
}

int main() {
  test_quiet_room_no_music_does_not_qualify_buildup();
  test_quiet_musical_buildup_qualifies();
  test_buildup_phase_but_silent_does_not_qualify();
  test_low_band_caps_regardless_of_lent_target();
  test_quiet_cap_respects_grace_window();
  test_medium_bounded_raise();
  test_high_peak_full_authority();
  test_motion_tier_classification();
  test_duty_cycle_alternation();
  test_confirmed_drop_entry_escalates();
  test_major_drop_entry_near_immediate_and_guaranteed();
  test_ineligible_phrases_excluded_from_selection();
  test_exhausted_limits_exclude_phrase();
  test_dense_rhythm_favors_booty_shake();
  test_smooth_sustained_favors_full_sustain();
  test_fresh_impact_during_long_phrase_favors_reversal();
  test_antirepeat_reduces_but_does_not_eliminate();
  test_phrase_step_shapes();
  test_every_phrase_terminates_at_drop_speed_never_below_m80();

  if (g_failures == 0) {
    printf("All music_motor_choreography_dynamics tests passed.\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
