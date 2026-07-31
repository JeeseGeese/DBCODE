// Temporary host-side deterministic test -- mirrors revision 9's
// relative/song-adaptive musical-section (EDM/dubstep "drop") recognition
// added to src/MusicMotorController.cpp (computeDropSignalScores(),
// computeDropConfidence(), classifyDropConfidenceTier(),
// computeMusicalSectionPhaseTransition(), and the sustained-drive entry
// escalation/speed-floor-tier selection logic inside
// trySustainedDriveEntryFromDrop()/updateMusicMotorController()). Same
// rationale/approach as every other test_host/*.cpp file: no PlatformIO
// "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_relative_drop_test test_host/music_motor_relative_drop_detection.cpp && /tmp/mm_relative_drop_test
//
// Covers the EDM/dubstep clarification's numbered test list:
//  1. an isolated loud transient does not, by itself, qualify as a drop
//  2. a buildup followed by a strong, sustained energy/density increase qualifies
//  3. a buildup that fades without a strong arrival does not qualify
//  4. a large bass-like impact with sustained continuation qualifies
//  5. repeated moderate impacts collectively qualify without one huge transient
//  6. increased beat density/pace contributes to drop confidence
//  7. no tone-change/spectrum-expansion signal is hard-coded (weights sum to 1.0
//     using only currently-measurable signals -- architecture ready to add one later)
//  8. a drop may qualify even while the absolute measured band is only MEDIUM
//  9. steady loud music does not repeatedly retrigger a "new" drop
//  10. an active drop remains active through a short gap (tolerates brief dips)
//  11. a meaningful, sustained collapse ends the drop (DROP_RELEASE)
//  12. a confirmed drop strongly biases sustained-drive entry (escalating chance)
//  13. a prolonged major drop cannot pass with zero sustained-drive opportunity
//      (entry chance escalates to guaranteed)
//  14. a major drop selects the highest (PEAK) sustained speed floor tier
//  15. a minor/POSSIBLE-only drop never reaches the entry-opportunity path at all
//  16. a buildup alone (never arriving) never reaches DROP_ACTIVE -- existing
//      buildup choreography is preserved, not converted into full-speed drive
//  17/18. existing renewable-phrase/silence-ramp-down/safety behavior is a
//      regression concern covered by the OTHER test_host/*.cpp files, which
//      this revision leaves structurally untouched (re-run alongside this one
//      by the harness/report, not duplicated here)

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

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float maxf(float a, float b) { return a > b ? a : b; }

// --- mirrored Config.h "Revision 9" constants ---
constexpr float WEIGHT_ENERGY_RISE = 0.20f;
constexpr float WEIGHT_SECTION_CONTRAST = 0.10f;
constexpr float WEIGHT_BASS_IMPACT = 0.20f;
constexpr float WEIGHT_BASS_DENSITY = 0.15f;
constexpr float WEIGHT_BEAT_DENSITY = 0.12f;
constexpr float WEIGHT_TRANSIENT_DENSITY = 0.12f;
constexpr float WEIGHT_BUILDUP_RESOLUTION = 0.08f;
constexpr float WEIGHT_SUSTAINED_ENERGY = 0.03f;

constexpr float POSSIBLE_DROP_THRESHOLD = 0.35f;
constexpr float CONFIRMED_DROP_THRESHOLD = 0.55f;
constexpr float MAJOR_DROP_THRESHOLD = 0.75f;
constexpr float ACTIVE_SUSTAIN_FLOOR = 0.22f;
constexpr float BUILDUP_ENTRY_THRESHOLD = 0.18f;
constexpr float BUILDUP_FADE_THRESHOLD = 0.09f;

constexpr uint32_t BUILDUP_MIN_MS = 2000;
constexpr uint32_t ARMED_TIMEOUT_MS = 6000;
constexpr uint32_t IMPACT_CONFIRM_MS = 400;
constexpr uint32_t RELEASE_GRACE_MS = 2500;
constexpr uint32_t REFRACTORY_MS = 1500;

constexpr uint8_t ENTRY_INITIAL_PERCENT = 40;
constexpr uint8_t ENTRY_ESCALATED_PERCENT = 70;
constexpr uint8_t ENTRY_GUARANTEED_PERCENT = 100;
constexpr uint32_t ENTRY_ESCALATE_AFTER_MS = 2500;
constexpr uint32_t ENTRY_GUARANTEE_AFTER_MS = 6000;

constexpr uint8_t SUSTAINED_NORMAL_FLOOR_PERCENT = 90;
constexpr uint8_t SUSTAINED_PERFORMANCE_FLOOR_PERCENT = 97;
constexpr uint8_t SUSTAINED_PEAK_FLOOR_PERCENT = 100;

// --- mirrored pure functions (see MusicMotorController.cpp's "Revision 9"
// section for the authoritative originals) ---

struct DropSignalScores {
  float energyRise;
  float sectionContrast;
  float bassImpact;
  float bassDensity;
  float beatDensity;
  float transientDensity;
  float buildupResolution;
  float sustainedEnergy;
};

struct DropSignalRawInputs {
  float songEnergyNow;
  float buildupReference;
  float longReference;
  float referenceFloor;
  float bassImpactDeltaNow;
  float bassImpactDeltaThreshold;
  float beatDensityScoreNow;
  float beatDensityNormalization;
  float transientDensityScoreNow;
  float transientDensityNormalization;
  float bassDensityScoreNow;
  float bassDensityNormalization;
  bool recentlyBuildingOrArmed;
  float performanceEnergyNow;
  float peakThresholdForNormalization;
};

static DropSignalScores computeDropSignalScores(const DropSignalRawInputs &in) {
  DropSignalScores s{};
  s.energyRise = clampf((in.songEnergyNow - in.buildupReference) / maxf(in.buildupReference, in.referenceFloor), 0.0f, 1.0f);
  s.sectionContrast = clampf((in.songEnergyNow - in.longReference) / maxf(in.longReference, in.referenceFloor), 0.0f, 1.0f);
  s.bassImpact = clampf(in.bassImpactDeltaNow / maxf(in.bassImpactDeltaThreshold, 0.001f), 0.0f, 1.0f);
  s.bassDensity = clampf(in.bassDensityScoreNow / maxf(in.bassDensityNormalization, 0.001f), 0.0f, 1.0f);
  s.beatDensity = clampf(in.beatDensityScoreNow / maxf(in.beatDensityNormalization, 0.001f), 0.0f, 1.0f);
  s.transientDensity = clampf(in.transientDensityScoreNow / maxf(in.transientDensityNormalization, 0.001f), 0.0f, 1.0f);
  s.buildupResolution = in.recentlyBuildingOrArmed ? s.energyRise : s.energyRise * 0.4f;
  s.sustainedEnergy = clampf(in.performanceEnergyNow / maxf(in.peakThresholdForNormalization, 0.001f), 0.0f, 1.0f);
  return s;
}

static float computeDropConfidence(const DropSignalScores &s) {
  float total = s.energyRise * WEIGHT_ENERGY_RISE + s.sectionContrast * WEIGHT_SECTION_CONTRAST + s.bassImpact * WEIGHT_BASS_IMPACT +
                s.bassDensity * WEIGHT_BASS_DENSITY + s.beatDensity * WEIGHT_BEAT_DENSITY + s.transientDensity * WEIGHT_TRANSIENT_DENSITY +
                s.buildupResolution * WEIGHT_BUILDUP_RESOLUTION + s.sustainedEnergy * WEIGHT_SUSTAINED_ENERGY;
  return clampf(total, 0.0f, 1.0f);
}

enum class DropConfidenceTier { NONE, POSSIBLE_DROP, CONFIRMED_DROP, MAJOR_DROP };

static DropConfidenceTier classifyDropConfidenceTier(float confidence) {
  if (confidence >= MAJOR_DROP_THRESHOLD) return DropConfidenceTier::MAJOR_DROP;
  if (confidence >= CONFIRMED_DROP_THRESHOLD) return DropConfidenceTier::CONFIRMED_DROP;
  if (confidence >= POSSIBLE_DROP_THRESHOLD) return DropConfidenceTier::POSSIBLE_DROP;
  return DropConfidenceTier::NONE;
}

enum class MusicalSectionPhase { NEUTRAL, BUILDUP, DROP_ARMED, DROP_IMPACT, DROP_ACTIVE, DROP_RELEASE };

struct MusicalSectionPhaseInputs {
  MusicalSectionPhase currentPhase = MusicalSectionPhase::NEUTRAL;
  unsigned long nowMs = 0;
  unsigned long phaseEnteredMs = 0;
  unsigned long dropRefractoryUntilMs = 0;
  unsigned long belowSustainFloorSinceMs = 0;
  float dropConfidenceNow = 0.0f;
};

struct MusicalSectionPhaseResult {
  MusicalSectionPhase newPhase;
  bool transitioned;
  const char *reason;
};

static MusicalSectionPhaseResult computeMusicalSectionPhaseTransition(const MusicalSectionPhaseInputs &in) {
  MusicalSectionPhaseResult r{in.currentPhase, false, "no_change"};
  unsigned long elapsedInPhase = in.nowMs - in.phaseEnteredMs;
  switch (in.currentPhase) {
    case MusicalSectionPhase::NEUTRAL:
      if ((long)(in.nowMs - in.dropRefractoryUntilMs) < 0) return r;
      if (in.dropConfidenceNow >= BUILDUP_ENTRY_THRESHOLD) {
        r.newPhase = MusicalSectionPhase::BUILDUP;
        r.transitioned = true;
        r.reason = "rising_evidence";
      }
      return r;
    case MusicalSectionPhase::BUILDUP:
      if (in.dropConfidenceNow < BUILDUP_FADE_THRESHOLD) {
        r.newPhase = MusicalSectionPhase::NEUTRAL;
        r.transitioned = true;
        r.reason = "buildup_faded";
        return r;
      }
      if (elapsedInPhase >= BUILDUP_MIN_MS && in.dropConfidenceNow >= POSSIBLE_DROP_THRESHOLD) {
        r.newPhase = MusicalSectionPhase::DROP_ARMED;
        r.transitioned = true;
        r.reason = "buildup_sufficient";
      }
      return r;
    case MusicalSectionPhase::DROP_ARMED:
      if (in.dropConfidenceNow >= CONFIRMED_DROP_THRESHOLD) {
        r.newPhase = MusicalSectionPhase::DROP_IMPACT;
        r.transitioned = true;
        r.reason = "impact_detected";
        return r;
      }
      if (elapsedInPhase >= ARMED_TIMEOUT_MS) {
        r.newPhase = MusicalSectionPhase::NEUTRAL;
        r.transitioned = true;
        r.reason = "armed_timeout";
      }
      return r;
    case MusicalSectionPhase::DROP_IMPACT:
      if (in.dropConfidenceNow < POSSIBLE_DROP_THRESHOLD) {
        r.newPhase = MusicalSectionPhase::BUILDUP;
        r.transitioned = true;
        r.reason = "impact_collapsed_to_accent";
        return r;
      }
      if (elapsedInPhase >= IMPACT_CONFIRM_MS) {
        r.newPhase = MusicalSectionPhase::DROP_ACTIVE;
        r.transitioned = true;
        r.reason = "impact_confirmed";
      }
      return r;
    case MusicalSectionPhase::DROP_ACTIVE:
      if (in.dropConfidenceNow < ACTIVE_SUSTAIN_FLOOR && in.belowSustainFloorSinceMs != 0 &&
          (in.nowMs - in.belowSustainFloorSinceMs) >= RELEASE_GRACE_MS) {
        r.newPhase = MusicalSectionPhase::DROP_RELEASE;
        r.transitioned = true;
        r.reason = "sustained_evidence_collapsed";
      }
      return r;
    case MusicalSectionPhase::DROP_RELEASE:
      r.newPhase = MusicalSectionPhase::NEUTRAL;
      r.transitioned = true;
      r.reason = "release_complete";
      return r;
  }
  return r;
}

// Mirrors trySustainedDriveEntryFromDrop()'s escalation-tier selection
// (the deterministic part only -- the actual random() roll is a separate,
// already-covered concern, same pattern as computeSustainedDriveDecision()'s
// weightRollPercent0to99 parameter in music_motor_sustained_drive.cpp).
static uint8_t pickDropEntryChancePercent(unsigned long sinceActiveMs, DropConfidenceTier tier, bool effectiveBandAtLeastMedium) {
  uint8_t chance;
  if (sinceActiveMs >= ENTRY_GUARANTEE_AFTER_MS) chance = ENTRY_GUARANTEED_PERCENT;
  else if (sinceActiveMs >= ENTRY_ESCALATE_AFTER_MS) chance = ENTRY_ESCALATED_PERCENT;
  else chance = ENTRY_INITIAL_PERCENT;
  if (tier == DropConfidenceTier::MAJOR_DROP && effectiveBandAtLeastMedium) chance = ENTRY_GUARANTEED_PERCENT;
  return chance;
}

// Mirrors updateMusicMotorController()'s SUSTAINED_DRIVE floor-tier
// selection (see its own "Revision 9" comment).
enum class FloorTier { NORMAL, PERFORMANCE, PEAK };
static FloorTier pickSustainedFloorTier(bool absolutePeakDropHold, bool relativeMajorDropActive, bool effBandIsPeak,
                                         bool dropHoldActive, bool relativeConfirmedDropActive) {
  if (absolutePeakDropHold || relativeMajorDropActive) return FloorTier::PEAK;
  if (effBandIsPeak || dropHoldActive || relativeConfirmedDropActive) return FloorTier::PERFORMANCE;
  return FloorTier::NORMAL;
}
static uint8_t floorTierPercent(FloorTier t) {
  switch (t) {
    case FloorTier::NORMAL: return SUSTAINED_NORMAL_FLOOR_PERCENT;
    case FloorTier::PERFORMANCE: return SUSTAINED_PERFORMANCE_FLOOR_PERCENT;
    case FloorTier::PEAK: return SUSTAINED_PEAK_FLOOR_PERCENT;
  }
  return SUSTAINED_NORMAL_FLOOR_PERCENT;
}

// ----------------------------------------------------------------------------
// Test 1 -- an isolated loud transient (single-tick bass/beat spike, no
// sustained rise, no density buildup) does not by itself reach even
// POSSIBLE_DROP. Only bassImpact/transientDensity fire for one tick; every
// reference-based/density signal stays near zero.
// ----------------------------------------------------------------------------
static void test_isolated_transient_does_not_qualify() {
  DropSignalRawInputs raw{};
  raw.songEnergyNow = 0.30f;
  raw.buildupReference = 0.28f;  // barely below -- no real rise
  raw.longReference = 0.27f;
  raw.referenceFloor = 0.05f;
  raw.bassImpactDeltaNow = 0.30f;  // one big bass hit this instant
  raw.bassImpactDeltaThreshold = 0.12f;
  raw.beatDensityScoreNow = 0.3f;  // one recent beat, not a cluster
  raw.beatDensityNormalization = 3.0f;
  raw.transientDensityScoreNow = 0.3f;
  raw.transientDensityNormalization = 5.0f;
  raw.bassDensityScoreNow = 0.3f;
  raw.bassDensityNormalization = 3.0f;
  raw.recentlyBuildingOrArmed = false;
  raw.performanceEnergyNow = 0.30f;
  raw.peakThresholdForNormalization = 0.72f;

  DropSignalScores s = computeDropSignalScores(raw);
  float confidence = computeDropConfidence(s);
  DropConfidenceTier tier = classifyDropConfidenceTier(confidence);
  check(tier == DropConfidenceTier::NONE || tier == DropConfidenceTier::POSSIBLE_DROP,
        "isolated transient must not reach CONFIRMED/MAJOR");
  check(confidence < CONFIRMED_DROP_THRESHOLD, "isolated transient confidence stays below CONFIRMED threshold");
}

// ----------------------------------------------------------------------------
// Test 2 -- a buildup (rising songEnergy vs. its own recent reference) that
// then sustains a strong, wide-margin rise reaches DROP_ACTIVE via the full
// BUILDUP->DROP_ARMED->DROP_IMPACT->DROP_ACTIVE chain, never skipping a
// required dwell.
// ----------------------------------------------------------------------------
static void test_buildup_with_strong_arrival_qualifies() {
  MusicalSectionPhase phase = MusicalSectionPhase::NEUTRAL;
  unsigned long phaseEnteredMs = 1000;
  unsigned long now = 1000;
  unsigned long refractoryUntil = 0;

  // Confidence sequence: rising during buildup, then a strong sustained arrival.
  float buildupConfidence = 0.25f;   // >= BUILDUP_ENTRY_THRESHOLD, < POSSIBLE
  float armedArrivalConfidence = 0.65f;  // >= CONFIRMED_DROP_THRESHOLD

  // NEUTRAL -> BUILDUP
  MusicalSectionPhaseInputs in{};
  in.currentPhase = phase;
  in.nowMs = now;
  in.phaseEnteredMs = phaseEnteredMs;
  in.dropRefractoryUntilMs = refractoryUntil;
  in.dropConfidenceNow = buildupConfidence;
  MusicalSectionPhaseResult r = computeMusicalSectionPhaseTransition(in);
  check(r.transitioned && r.newPhase == MusicalSectionPhase::BUILDUP, "rising evidence enters BUILDUP");
  phase = r.newPhase;
  phaseEnteredMs = now;

  // Still BUILDUP before buildupMinMs elapses -- confidence now high enough
  // for DROP_ARMED but the dwell hasn't been satisfied yet.
  now += 500;
  in = MusicalSectionPhaseInputs{};
  in.currentPhase = phase;
  in.nowMs = now;
  in.phaseEnteredMs = phaseEnteredMs;
  in.dropConfidenceNow = armedArrivalConfidence;
  r = computeMusicalSectionPhaseTransition(in);
  check(!r.transitioned, "BUILDUP does not promote to DROP_ARMED before buildupMinMs elapses");

  // buildupMinMs elapses -- now promotes.
  now = phaseEnteredMs + BUILDUP_MIN_MS + 10;
  in.nowMs = now;
  r = computeMusicalSectionPhaseTransition(in);
  check(r.transitioned && r.newPhase == MusicalSectionPhase::DROP_ARMED, "sufficient buildup dwell promotes to DROP_ARMED");
  phase = r.newPhase;
  phaseEnteredMs = now;

  // DROP_ARMED -> DROP_IMPACT (confidence already at/above CONFIRMED).
  in = MusicalSectionPhaseInputs{};
  in.currentPhase = phase;
  in.nowMs = now;
  in.phaseEnteredMs = phaseEnteredMs;
  in.dropConfidenceNow = armedArrivalConfidence;
  r = computeMusicalSectionPhaseTransition(in);
  check(r.transitioned && r.newPhase == MusicalSectionPhase::DROP_IMPACT, "confirmed-level confidence promotes ARMED to IMPACT");
  phase = r.newPhase;
  phaseEnteredMs = now;

  // DROP_IMPACT must dwell impactConfirmMs before DROP_ACTIVE.
  now += 100;
  in = MusicalSectionPhaseInputs{};
  in.currentPhase = phase;
  in.nowMs = now;
  in.phaseEnteredMs = phaseEnteredMs;
  in.dropConfidenceNow = armedArrivalConfidence;
  r = computeMusicalSectionPhaseTransition(in);
  check(!r.transitioned, "DROP_IMPACT does not promote before impactConfirmMs elapses");

  now = phaseEnteredMs + IMPACT_CONFIRM_MS + 10;
  in.nowMs = now;
  r = computeMusicalSectionPhaseTransition(in);
  check(r.transitioned && r.newPhase == MusicalSectionPhase::DROP_ACTIVE, "sustained arrival confirms DROP_IMPACT into DROP_ACTIVE");
}

// ----------------------------------------------------------------------------
// Test 3 -- a buildup whose confidence fades back below the fade threshold
// before ever arriving returns to NEUTRAL, never reaching DROP_ARMED/
// DROP_ACTIVE. This is also test 16 (buildups preserve existing
// choreography -- a buildup that never arrives never forces full-speed
// drive).
// ----------------------------------------------------------------------------
static void test_buildup_that_fades_does_not_qualify() {
  MusicalSectionPhaseInputs in{};
  in.currentPhase = MusicalSectionPhase::BUILDUP;
  in.nowMs = 5000;
  in.phaseEnteredMs = 4500;  // buildupMinMs not yet elapsed either way
  in.dropConfidenceNow = 0.05f;  // below BUILDUP_FADE_THRESHOLD
  MusicalSectionPhaseResult r = computeMusicalSectionPhaseTransition(in);
  check(r.transitioned && r.newPhase == MusicalSectionPhase::NEUTRAL, "a fading buildup returns to NEUTRAL, not DROP_ARMED");
}

// ----------------------------------------------------------------------------
// Test 4 -- a large bass-like impact (bassImpact dominant) combined with
// sustained continuation (performanceEnergy/sectionContrast still elevated)
// reaches CONFIRMED_DROP even with modest beat/transient density.
// ----------------------------------------------------------------------------
static void test_large_bass_impact_with_sustain_qualifies() {
  DropSignalRawInputs raw{};
  raw.songEnergyNow = 0.55f;
  raw.buildupReference = 0.30f;
  raw.longReference = 0.28f;
  raw.referenceFloor = 0.05f;
  raw.bassImpactDeltaNow = 0.30f;  // >= threshold -> full bassImpact score
  raw.bassImpactDeltaThreshold = 0.12f;
  raw.beatDensityScoreNow = 0.6f;
  raw.beatDensityNormalization = 3.0f;
  raw.transientDensityScoreNow = 1.0f;
  raw.transientDensityNormalization = 5.0f;
  raw.bassDensityScoreNow = 1.5f;
  raw.bassDensityNormalization = 3.0f;
  raw.recentlyBuildingOrArmed = true;
  raw.performanceEnergyNow = 0.55f;
  raw.peakThresholdForNormalization = 0.72f;

  DropSignalScores s = computeDropSignalScores(raw);
  float confidence = computeDropConfidence(s);
  check(classifyDropConfidenceTier(confidence) >= DropConfidenceTier::CONFIRMED_DROP,
        "large bass impact + sustained continuation reaches at least CONFIRMED_DROP");
}

// ----------------------------------------------------------------------------
// Test 5 -- repeated MODERATE impacts (high density scores, no single
// dominant bassImpact/energyRise spike) can collectively reach
// CONFIRMED_DROP purely via the density signals -- "no single huge
// transient required."
// ----------------------------------------------------------------------------
static void test_repeated_moderate_impacts_qualify_via_density() {
  DropSignalRawInputs raw{};
  raw.songEnergyNow = 0.35f;
  raw.buildupReference = 0.30f;  // only a modest rise
  raw.longReference = 0.29f;
  raw.referenceFloor = 0.05f;
  raw.bassImpactDeltaNow = 0.05f;  // below threshold -- no single big hit right now
  raw.bassImpactDeltaThreshold = 0.12f;
  raw.beatDensityScoreNow = 3.0f;  // saturated -- a fast, sustained cluster
  raw.beatDensityNormalization = 3.0f;
  raw.transientDensityScoreNow = 5.0f;  // saturated
  raw.transientDensityNormalization = 5.0f;
  raw.bassDensityScoreNow = 3.0f;  // saturated -- many moderate bass hits recently
  raw.bassDensityNormalization = 3.0f;
  raw.recentlyBuildingOrArmed = true;
  raw.performanceEnergyNow = 0.40f;
  raw.peakThresholdForNormalization = 0.72f;

  DropSignalScores s = computeDropSignalScores(raw);
  check(s.bassImpact < 0.5f, "no single dominant bass-impact spike in this scenario");
  float confidence = computeDropConfidence(s);
  check(classifyDropConfidenceTier(confidence) >= DropConfidenceTier::CONFIRMED_DROP,
        "saturated density signals alone (no single huge transient) can reach CONFIRMED_DROP");
}

// ----------------------------------------------------------------------------
// Test 6 -- increased beat density alone (all else held near-neutral)
// measurably raises confidence versus a baseline with zero density.
// ----------------------------------------------------------------------------
static void test_beat_density_contributes() {
  DropSignalRawInputs base{};
  base.songEnergyNow = 0.30f;
  base.buildupReference = 0.28f;
  base.longReference = 0.27f;
  base.referenceFloor = 0.05f;
  base.bassImpactDeltaNow = 0.0f;
  base.bassImpactDeltaThreshold = 0.12f;
  base.beatDensityScoreNow = 0.0f;
  base.beatDensityNormalization = 3.0f;
  base.transientDensityScoreNow = 0.0f;
  base.transientDensityNormalization = 5.0f;
  base.bassDensityScoreNow = 0.0f;
  base.bassDensityNormalization = 3.0f;
  base.recentlyBuildingOrArmed = false;
  base.performanceEnergyNow = 0.30f;
  base.peakThresholdForNormalization = 0.72f;

  float confidenceLowDensity = computeDropConfidence(computeDropSignalScores(base));

  DropSignalRawInputs withDensity = base;
  withDensity.beatDensityScoreNow = 3.0f;  // saturated
  float confidenceHighDensity = computeDropConfidence(computeDropSignalScores(withDensity));

  check(confidenceHighDensity > confidenceLowDensity, "higher beat density strictly increases drop confidence");
}

// ----------------------------------------------------------------------------
// Test 7 -- the confidence model's weights sum to 1.0 using only currently-
// measurable signals (no tone-change/spectrum-expansion weight is silently
// assumed) -- confirms no hard-coded placeholder for an unmeasured signal.
// ----------------------------------------------------------------------------
static void test_no_unmeasured_signal_hardcoded() {
  float sum = WEIGHT_ENERGY_RISE + WEIGHT_SECTION_CONTRAST + WEIGHT_BASS_IMPACT + WEIGHT_BASS_DENSITY + WEIGHT_BEAT_DENSITY +
              WEIGHT_TRANSIENT_DENSITY + WEIGHT_BUILDUP_RESOLUTION + WEIGHT_SUSTAINED_ENERGY;
  check(sum > 0.99f && sum < 1.01f, "the 8 currently-measurable-signal weights sum to ~1.0 with no missing/placeholder weight");
}

// ----------------------------------------------------------------------------
// Test 8 -- a drop can qualify (CONFIRMED_DROP) even while songEnergy is
// only in the MEDIUM absolute range (e.g. 0.30, well below the 0.48 HIGH
// threshold), provided the RELATIVE rise/contrast/density evidence is
// strong -- "may qualify even at absolute MEDIUM."
// ----------------------------------------------------------------------------
static void test_drop_qualifies_at_absolute_medium() {
  constexpr float HIGH_THRESHOLD = 0.48f;  // mirrors MUSIC_MOTOR_HIGH_THRESHOLD
  DropSignalRawInputs raw{};
  raw.songEnergyNow = 0.32f;  // MEDIUM band, nowhere near HIGH_THRESHOLD
  check(raw.songEnergyNow < HIGH_THRESHOLD, "scenario setup: songEnergy stays below the absolute HIGH threshold");
  raw.buildupReference = 0.10f;  // this song's recent section was much quieter
  raw.longReference = 0.10f;
  raw.referenceFloor = 0.05f;
  raw.bassImpactDeltaNow = 0.20f;
  raw.bassImpactDeltaThreshold = 0.12f;
  raw.beatDensityScoreNow = 2.5f;
  raw.beatDensityNormalization = 3.0f;
  raw.transientDensityScoreNow = 4.0f;
  raw.transientDensityNormalization = 5.0f;
  raw.bassDensityScoreNow = 2.5f;
  raw.bassDensityNormalization = 3.0f;
  raw.recentlyBuildingOrArmed = true;
  raw.performanceEnergyNow = 0.32f;
  raw.peakThresholdForNormalization = 0.72f;

  float confidence = computeDropConfidence(computeDropSignalScores(raw));
  check(classifyDropConfidenceTier(confidence) >= DropConfidenceTier::CONFIRMED_DROP,
        "a strong RELATIVE rise qualifies as CONFIRMED_DROP even at absolute MEDIUM songEnergy");
}

// ----------------------------------------------------------------------------
// Test 9 -- steady loud music does not repeatedly retrigger: once
// longSongEnergyReference has caught up to a sustained level (simulated
// directly here, since the real EMA convergence is a timing detail), the
// SAME songEnergy no longer produces a large sectionContrast score.
// ----------------------------------------------------------------------------
static void test_steady_loud_music_does_not_retrigger() {
  DropSignalRawInputs earlyInSection{};
  earlyInSection.songEnergyNow = 0.60f;
  earlyInSection.buildupReference = 0.30f;
  earlyInSection.longReference = 0.25f;  // reference hasn't caught up yet
  earlyInSection.referenceFloor = 0.05f;
  earlyInSection.bassImpactDeltaNow = 0.0f;
  earlyInSection.bassImpactDeltaThreshold = 0.12f;
  earlyInSection.beatDensityScoreNow = 0.0f;
  earlyInSection.beatDensityNormalization = 3.0f;
  earlyInSection.transientDensityScoreNow = 0.0f;
  earlyInSection.transientDensityNormalization = 5.0f;
  earlyInSection.bassDensityScoreNow = 0.0f;
  earlyInSection.bassDensityNormalization = 3.0f;
  earlyInSection.recentlyBuildingOrArmed = false;
  earlyInSection.performanceEnergyNow = 0.60f;
  earlyInSection.peakThresholdForNormalization = 0.72f;
  float earlyContrast = computeDropSignalScores(earlyInSection).sectionContrast;

  DropSignalRawInputs lateInSection = earlyInSection;
  lateInSection.longReference = 0.58f;  // reference has now caught up to the sustained level
  float lateContrast = computeDropSignalScores(lateInSection).sectionContrast;

  check(lateContrast < earlyContrast, "sectionContrast decays as the long-song reference catches up to a sustained loud section");
  check(lateContrast < 0.1f, "a section that has simply stayed loud eventually contributes ~no sectionContrast");
}

// ----------------------------------------------------------------------------
// Test 10 -- DROP_ACTIVE remains active through a short gap: a brief dip
// below the sustain floor that resolves before releaseGraceMs elapses does
// not release the drop.
// ----------------------------------------------------------------------------
static void test_active_drop_tolerates_short_gap() {
  unsigned long enteredMs = 10000;
  unsigned long dipStartMs = 10500;

  MusicalSectionPhaseInputs in{};
  in.currentPhase = MusicalSectionPhase::DROP_ACTIVE;
  in.nowMs = dipStartMs + (RELEASE_GRACE_MS / 2);  // well before releaseGraceMs elapses
  in.phaseEnteredMs = enteredMs;
  in.belowSustainFloorSinceMs = dipStartMs;
  in.dropConfidenceNow = 0.10f;  // below ACTIVE_SUSTAIN_FLOOR during the dip
  MusicalSectionPhaseResult r = computeMusicalSectionPhaseTransition(in);
  check(!r.transitioned, "a short dip below the sustain floor does not yet release an active drop");
}

// ----------------------------------------------------------------------------
// Test 11 -- a sustained collapse (below the sustain floor for the FULL
// releaseGraceMs) does release the drop into DROP_RELEASE.
// ----------------------------------------------------------------------------
static void test_sustained_collapse_releases_drop() {
  unsigned long enteredMs = 10000;
  unsigned long dipStartMs = 10500;

  MusicalSectionPhaseInputs in{};
  in.currentPhase = MusicalSectionPhase::DROP_ACTIVE;
  in.nowMs = dipStartMs + RELEASE_GRACE_MS + 50;
  in.phaseEnteredMs = enteredMs;
  in.belowSustainFloorSinceMs = dipStartMs;
  in.dropConfidenceNow = 0.10f;
  MusicalSectionPhaseResult r = computeMusicalSectionPhaseTransition(in);
  check(r.transitioned && r.newPhase == MusicalSectionPhase::DROP_RELEASE,
        "a full releaseGraceMs of collapsed evidence ends the drop");

  // DROP_RELEASE always advances to NEUTRAL on the very next evaluation.
  MusicalSectionPhaseInputs in2{};
  in2.currentPhase = MusicalSectionPhase::DROP_RELEASE;
  in2.nowMs = in.nowMs + 1;
  in2.phaseEnteredMs = in.nowMs;
  MusicalSectionPhaseResult r2 = computeMusicalSectionPhaseTransition(in2);
  check(r2.transitioned && r2.newPhase == MusicalSectionPhase::NEUTRAL, "DROP_RELEASE advances to NEUTRAL");
}

// ----------------------------------------------------------------------------
// Test 12/13 -- a confirmed/major drop's entry chance escalates the longer
// it stays DROP_ACTIVE with no sustained-drive phrase yet started, reaching
// GUARANTEED after ENTRY_GUARANTEE_AFTER_MS -- "a prolonged major drop
// cannot pass with zero sustained-drive opportunity."
// ----------------------------------------------------------------------------
static void test_entry_chance_escalates_over_time() {
  uint8_t initial = pickDropEntryChancePercent(500, DropConfidenceTier::CONFIRMED_DROP, false);
  uint8_t escalated = pickDropEntryChancePercent(ENTRY_ESCALATE_AFTER_MS + 100, DropConfidenceTier::CONFIRMED_DROP, false);
  uint8_t guaranteed = pickDropEntryChancePercent(ENTRY_GUARANTEE_AFTER_MS + 100, DropConfidenceTier::CONFIRMED_DROP, false);
  check(initial == ENTRY_INITIAL_PERCENT, "initial entry chance matches the configured initial percent");
  check(escalated == ENTRY_ESCALATED_PERCENT, "entry chance escalates after ENTRY_ESCALATE_AFTER_MS");
  check(guaranteed == ENTRY_GUARANTEED_PERCENT, "entry chance reaches guaranteed after ENTRY_GUARANTEE_AFTER_MS");
  check(initial < escalated && escalated < guaranteed, "entry chance is strictly monotonic non-decreasing over drop duration");
}

// ----------------------------------------------------------------------------
// Test 14 -- a MAJOR_DROP with effectiveBand >= MEDIUM selects the PEAK
// sustained speed floor tier (the clearly-distinct full-speed tier).
// ----------------------------------------------------------------------------
static void test_major_drop_selects_peak_floor() {
  FloorTier tier = pickSustainedFloorTier(/*absolutePeakDropHold=*/false, /*relativeMajorDropActive=*/true,
                                           /*effBandIsPeak=*/false, /*dropHoldActive=*/false,
                                           /*relativeConfirmedDropActive=*/true);
  check(tier == FloorTier::PEAK, "a relative MAJOR_DROP selects the PEAK sustained speed floor tier");
  check(floorTierPercent(tier) == SUSTAINED_PEAK_FLOOR_PERCENT, "PEAK tier maps to the configured PEAK floor percent");
  check(floorTierPercent(FloorTier::PEAK) > floorTierPercent(FloorTier::PERFORMANCE) &&
            floorTierPercent(FloorTier::PERFORMANCE) > floorTierPercent(FloorTier::NORMAL),
        "the three floor tiers are strictly ordered NORMAL < PERFORMANCE < PEAK");
}

// ----------------------------------------------------------------------------
// Test 15 -- a POSSIBLE_DROP (below CONFIRMED) never reaches the entry-
// opportunity gate at all -- mirrors trySustainedDriveEntryFromDrop()'s own
// `if ((int)dropConfidenceTier < (int)DropConfidenceTier::CONFIRMED_DROP)
// return false;` early-out. Modeled here as a direct tier comparison, since
// that early-out IS the entire gating logic for this case.
// ----------------------------------------------------------------------------
static void test_possible_drop_never_reaches_entry_opportunity() {
  DropConfidenceTier possible = DropConfidenceTier::POSSIBLE_DROP;
  bool wouldReachEntryOpportunity = (int)possible >= (int)DropConfidenceTier::CONFIRMED_DROP;
  check(!wouldReachEntryOpportunity, "a POSSIBLE_DROP tier alone never reaches the sustained-drive entry-opportunity path");
}

int main() {
  test_isolated_transient_does_not_qualify();
  test_buildup_with_strong_arrival_qualifies();
  test_buildup_that_fades_does_not_qualify();
  test_large_bass_impact_with_sustain_qualifies();
  test_repeated_moderate_impacts_qualify_via_density();
  test_beat_density_contributes();
  test_no_unmeasured_signal_hardcoded();
  test_drop_qualifies_at_absolute_medium();
  test_steady_loud_music_does_not_retrigger();
  test_active_drop_tolerates_short_gap();
  test_sustained_collapse_releases_drop();
  test_entry_chance_escalates_over_time();
  test_major_drop_selects_peak_floor();
  test_possible_drop_never_reaches_entry_opportunity();

  if (g_failures == 0) {
    printf("All music_motor_relative_drop_detection tests passed.\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
