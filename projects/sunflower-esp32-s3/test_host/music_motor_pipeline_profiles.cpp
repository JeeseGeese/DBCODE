// Temporary host-side deterministic test -- mirrors the FULL per-tick
// pipeline added/changed in revision 5 of src/MusicMotorController.cpp
// (raw -> fastEnergy/songEnergy/baselineEnergy -> transientDelta ->
// intensityBand -> beat/strongHit -> performanceEnergy -> effectiveBand ->
// dual-path drop hold -> quiet timer), run tick-by-tick over synthetic
// multi-second signal profiles. Same rationale/approach as the other
// test_host/*.cpp files: no PlatformIO "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_pipeline_test test_host/music_motor_pipeline_profiles.cpp && /tmp/mm_pipeline_test
//
// This is explicitly a MULTI-SONG regression test (per the revision-5
// correction: "do not overfit to one song" / "validate with multiple
// synthetic signal profiles"). Each profile below stands in for a class of
// real material this controller has to handle:
//
//   1. room noise / silence          -- must stay QUIET, must eventually stop
//   2. ordinary moderate music       -- must reach at least LOW/MEDIUM, beats fire
//   3. stronger transient-heavy song -- must reach HIGH/PEAK, strong hits fire,
//                                       drop hold activates via the immediate
//                                       (strong-hit) path
//   4. sustained compressed high-energy, WEAK transients -- must reach
//                                       HIGH/PEAK from sustained level alone,
//                                       drop hold activates via the NEW
//                                       sustained (non-transient) path even
//                                       though strong hits rarely/never fire
//   5. weak/smooth "reference song" (worst case, matches the original
//      physical diagnostic capture: raw ~0.04-0.19, transientDelta
//      ~0.00-0.08) -- explicitly NOT expected to reach MEDIUM/HIGH/PEAK
//      under the unmodified thresholds (that is now an accepted, documented
//      outcome, not a regression -- see this revision's report); asserts
//      only that the pipeline stays well-behaved (bounded, no NaN,
//      effectiveBand never lends BELOW the measured band) and that the
//      persistent-quiet stop timer still works correctly for it.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// --- mirrored Config.h constants (post revision-5 correction -- see
// include/Config.h's own comments for why each was or wasn't changed) ---
constexpr float FAST_ATTACK = 0.60f;
constexpr float FAST_RELEASE = 0.15f;
constexpr float SONG_ATTACK = 0.10f;
constexpr float SONG_RELEASE = 0.035f;
constexpr float BASELINE_ADAPT_RATE = 0.022f;
constexpr float BEAT_DELTA_THRESHOLD = 0.10f;
constexpr float STRONG_HIT_DELTA_THRESHOLD = 0.22f;
constexpr float LOW_T = 0.10f, MEDIUM_T = 0.25f, HIGH_T = 0.48f, PEAK_T = 0.72f;
constexpr float HYSTERESIS = 0.04f;
constexpr float PERF_ATTACK = 0.12f;
constexpr float PERF_RELEASE = 0.006f;
constexpr float PERF_STRONG_HIT_BUMP = 0.18f;
constexpr float PERF_BEAT_BUMP = 0.05f;
constexpr uint32_t BEAT_COOLDOWN_MS = 160;
constexpr uint32_t STRONG_HIT_COOLDOWN_MS = 300;
constexpr uint32_t DROP_HOLD_INITIAL_MS = 2200;
constexpr uint32_t DROP_HOLD_MAX_MS = 4000;
constexpr uint32_t DROP_HOLD_SUSTAIN_CONFIRM_MS = 1800;
constexpr uint32_t SILENCE_TIMEOUT_MS = 7000;
constexpr uint32_t TICK_MS = 15;

enum class Band { QUIET, LOW, MEDIUM, HIGH, PEAK };
const char *bandName(Band b) {
  switch (b) {
    case Band::QUIET: return "QUIET";
    case Band::LOW: return "LOW";
    case Band::MEDIUM: return "MEDIUM";
    case Band::HIGH: return "HIGH";
    case Band::PEAK: return "PEAK";
  }
  return "?";
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Mirrors computeIntensityBand() -- hysteresis-aware, previous-band-dependent.
Band computeIntensityBand(float energy, Band prev) {
  switch (prev) {
    case Band::QUIET:
      if (energy >= PEAK_T) return Band::PEAK;
      if (energy >= HIGH_T) return Band::HIGH;
      if (energy >= MEDIUM_T) return Band::MEDIUM;
      if (energy >= LOW_T) return Band::LOW;
      return Band::QUIET;
    case Band::LOW:
      if (energy >= PEAK_T) return Band::PEAK;
      if (energy >= HIGH_T) return Band::HIGH;
      if (energy >= MEDIUM_T) return Band::MEDIUM;
      if (energy < LOW_T - HYSTERESIS) return Band::QUIET;
      return Band::LOW;
    case Band::MEDIUM:
      if (energy >= PEAK_T) return Band::PEAK;
      if (energy >= HIGH_T) return Band::HIGH;
      if (energy < MEDIUM_T - HYSTERESIS) return (energy >= LOW_T) ? Band::LOW : Band::QUIET;
      return Band::MEDIUM;
    case Band::HIGH:
      if (energy >= PEAK_T) return Band::PEAK;
      if (energy < HIGH_T - HYSTERESIS) {
        if (energy >= MEDIUM_T) return Band::MEDIUM;
        return (energy >= LOW_T) ? Band::LOW : Band::QUIET;
      }
      return Band::HIGH;
    case Band::PEAK:
      if (energy < PEAK_T - HYSTERESIS) {
        if (energy >= HIGH_T) return Band::HIGH;
        if (energy >= MEDIUM_T) return Band::MEDIUM;
        return (energy >= LOW_T) ? Band::LOW : Band::QUIET;
      }
      return Band::PEAK;
  }
  return Band::QUIET;
}

// Mirrors computeRawCandidateBand() -- hysteresis-FREE, used for
// performanceEnergy's implied band inside effectiveBand().
Band candidateBand(float energy) {
  if (energy >= PEAK_T) return Band::PEAK;
  if (energy >= HIGH_T) return Band::HIGH;
  if (energy >= MEDIUM_T) return Band::MEDIUM;
  if (energy >= LOW_T) return Band::LOW;
  return Band::QUIET;
}

struct PipelineState {
  float fastEnergy = 0.0f, songEnergy = 0.0f, baselineEnergy = 0.0f, transientDelta = 0.0f;
  float performanceEnergy = 0.0f;
  Band intensityBand = Band::QUIET;
  bool beatCooldownArmed = true;
  // Seeded already-elapsed-by-one-cooldown, mirroring resetRuntimeState()'s
  // `lastBeatMs = millis() - MUSIC_MOTOR_BEAT_COOLDOWN_MS;` (relies on
  // uint32_t wraparound, exactly like the real millis()-based code) -- so
  // the very first qualifying tick isn't spuriously blocked by a
  // now(=0)-minus-lastBeatMs(=0) gap that hasn't "elapsed" yet.
  uint32_t lastBeatMs = 0u - BEAT_COOLDOWN_MS;
  bool strongHitCooldownArmed = true;
  uint32_t lastStrongHitMs = 0u - STRONG_HIT_COOLDOWN_MS;
  uint32_t sustainedHighSinceMs = 0;
  uint32_t belowSilenceThresholdSinceMs = 0;
  bool dropHoldActive = false;
  uint32_t dropHoldStartMs = 0, dropHoldUntilMs = 0;
  int beatCount = 0, strongHitCount = 0;
  bool stoppedForSilence = false;
  bool dropHoldEverActivated = false;
  // Set on EVERY inactive->active transition where that specific transition
  // was sustain-qualified rather than hit-qualified -- accumulates (never
  // reset to false), so on a long section this can legitimately become true
  // even after an earlier hit-qualified activation, once that first session
  // hits MUSIC_MOTOR_DROP_HOLD_MAX_MS and a still-ongoing sustained section
  // re-qualifies it via the sustain path -- see dropHoldFirstActivationViaHit
  // below for "was the very FIRST activation via a strong hit" specifically.
  bool dropHoldActivatedViaSustainOnly = false;
  bool dropHoldHasActivated = false;       // internal: has ANY activation happened yet
  bool dropHoldFirstActivationViaHit = false;
  Band maxBandSeen = Band::QUIET;
};

// effectiveBand() -- lending from performanceEnergy, mirroring
// MusicMotorController.cpp exactly (dropHold->MEDIUM->HIGH lending omitted
// here since these synthetic profiles don't need to exercise that specific
// interaction to validate the multi-song claims this file targets).
Band effectiveBand(const PipelineState &s) {
  Band eff = s.intensityBand;
  Band perf = candidateBand(s.performanceEnergy);
  if ((int)perf > (int)eff) eff = perf;
  return eff;
}

// One MUSIC_MOTOR_TICK_MS tick -- mirrors updateMusicMotorController()'s
// body exactly (order matters -- see that function's own comments).
void tick(PipelineState &s, float raw, uint32_t now) {
  raw = clampf(raw, 0.0f, 1.0f);
  s.fastEnergy = clampf(s.fastEnergy + (raw - s.fastEnergy) * ((raw > s.fastEnergy) ? FAST_ATTACK : FAST_RELEASE), 0.0f, 1.0f);
  s.songEnergy = clampf(s.songEnergy + (raw - s.songEnergy) * ((raw > s.songEnergy) ? SONG_ATTACK : SONG_RELEASE), 0.0f, 1.0f);
  s.baselineEnergy = clampf(s.baselineEnergy + (s.fastEnergy - s.baselineEnergy) * BASELINE_ADAPT_RATE, 0.0f, 1.0f);
  s.transientDelta = std::max(0.0f, s.fastEnergy - s.baselineEnergy);

  Band newBand = computeIntensityBand(s.songEnergy, s.intensityBand);
  s.intensityBand = newBand;
  if ((int)newBand > (int)s.maxBandSeen) s.maxBandSeen = newBand;

  if (s.intensityBand == Band::HIGH || s.intensityBand == Band::PEAK) {
    if (s.sustainedHighSinceMs == 0) s.sustainedHighSinceMs = now;
  } else {
    s.sustainedHighSinceMs = 0;
  }

  bool strongHitRaw = s.transientDelta >= STRONG_HIT_DELTA_THRESHOLD;
  bool beatRaw = !strongHitRaw && (s.transientDelta >= BEAT_DELTA_THRESHOLD);
  bool beatDetected = false, strongHitDetected = false;
  if (strongHitRaw) {
    if (s.strongHitCooldownArmed && (now - s.lastStrongHitMs) >= STRONG_HIT_COOLDOWN_MS) {
      s.strongHitCooldownArmed = false;
      s.lastStrongHitMs = now;
      strongHitDetected = true;
    }
  } else {
    s.strongHitCooldownArmed = true;
  }
  if (beatRaw) {
    if (s.beatCooldownArmed && (now - s.lastBeatMs) >= BEAT_COOLDOWN_MS) {
      s.beatCooldownArmed = false;
      s.lastBeatMs = now;
      beatDetected = true;
    }
  } else {
    s.beatCooldownArmed = true;
  }
  if (beatDetected) s.beatCount++;
  if (strongHitDetected) s.strongHitCount++;

  float rate = (s.songEnergy > s.performanceEnergy) ? PERF_ATTACK : PERF_RELEASE;
  s.performanceEnergy = clampf(s.performanceEnergy + (s.songEnergy - s.performanceEnergy) * rate, 0.0f, 1.0f);
  if (strongHitDetected) {
    s.performanceEnergy = clampf(s.performanceEnergy + PERF_STRONG_HIT_BUMP, 0.0f, 1.0f);
  } else if (beatDetected) {
    s.performanceEnergy = clampf(s.performanceEnergy + PERF_BEAT_BUMP, 0.0f, 1.0f);
  }

  bool sustainQualify = s.sustainedHighSinceMs != 0 && (now - s.sustainedHighSinceMs) >= DROP_HOLD_SUSTAIN_CONFIRM_MS;
  bool hitQualify = strongHitDetected && s.intensityBand == Band::PEAK;
  if (hitQualify || sustainQualify) {
    if (!s.dropHoldActive) {
      s.dropHoldActive = true;
      s.dropHoldStartMs = now;
      s.dropHoldUntilMs = now + DROP_HOLD_INITIAL_MS;
      s.dropHoldEverActivated = true;
      if (!hitQualify) s.dropHoldActivatedViaSustainOnly = true;
      if (!s.dropHoldHasActivated) s.dropHoldFirstActivationViaHit = hitQualify;
      s.dropHoldHasActivated = true;
    } else {
      uint32_t maxUntil = s.dropHoldStartMs + DROP_HOLD_MAX_MS;
      uint32_t candidate = now + DROP_HOLD_INITIAL_MS;
      uint32_t newUntil = (candidate < maxUntil) ? candidate : maxUntil;
      if (newUntil > s.dropHoldUntilMs) s.dropHoldUntilMs = newUntil;
    }
  }
  if (s.dropHoldActive && (int32_t)(now - s.dropHoldUntilMs) >= 0) s.dropHoldActive = false;

  // Quiet timer -- mirrors updateIntensitySway()'s silence-timeout gate
  // exactly (keyed to the REAL measured intensityBand).
  if (s.intensityBand == Band::QUIET) {
    if (s.belowSilenceThresholdSinceMs == 0) s.belowSilenceThresholdSinceMs = now;
    if (!s.stoppedForSilence && (now - s.belowSilenceThresholdSinceMs) >= SILENCE_TIMEOUT_MS) {
      s.stoppedForSilence = true;
    }
  } else {
    s.belowSilenceThresholdSinceMs = 0;
  }
}

// Runs `raw` (a function of elapsed ms) across `durationMs`, one tick every
// TICK_MS, and returns the final state along with basic sanity assertions
// (bounded, finite) checked on every single tick, not just at the end.
template <typename RawFn>
PipelineState runProfile(const char *label, RawFn rawFn, uint32_t durationMs) {
  printf("\n-- %s --\n", label);
  PipelineState s;
  for (uint32_t t = 0; t <= durationMs; t += TICK_MS) {
    float raw = rawFn(t);
    tick(s, raw, t);
    // Cross-cutting invariants, every tick, every profile:
    check(std::isfinite(s.fastEnergy) && s.fastEnergy >= 0.0f && s.fastEnergy <= 1.0f, "fastEnergy out of [0,1] or non-finite");
    check(std::isfinite(s.songEnergy) && s.songEnergy >= 0.0f && s.songEnergy <= 1.0f, "songEnergy out of [0,1] or non-finite");
    check(std::isfinite(s.performanceEnergy) && s.performanceEnergy >= 0.0f && s.performanceEnergy <= 1.0f,
          "performanceEnergy out of [0,1] or non-finite");
    check((int)effectiveBand(s) >= (int)s.intensityBand, "effectiveBand() must never lend BELOW the measured band");
  }
  printf("  final: songEnergy=%.3f performanceEnergy=%.3f band=%s maxBandSeen=%s beats=%d strongHits=%d "
         "dropHoldEverActivated=%d dropHoldFirstViaHit=%d dropHoldEverViaSustain=%d stoppedForSilence=%d\n",
         (double)s.songEnergy, (double)s.performanceEnergy, bandName(s.intensityBand), bandName(s.maxBandSeen), s.beatCount,
         s.strongHitCount, s.dropHoldEverActivated ? 1 : 0, s.dropHoldFirstActivationViaHit ? 1 : 0,
         s.dropHoldActivatedViaSustainOnly ? 1 : 0, s.stoppedForSilence ? 1 : 0);
  return s;
}

int main() {
  printf("== Revision-5 multi-profile pipeline regression test ==\n");

  // --- Profile 1: room noise / silence -- must stay QUIET, must eventually
  // register the persistent-quiet stop. ---
  {
    auto raw = [](uint32_t t) { return 0.03f + 0.01f * sinf((float)t * 0.01f); };
    PipelineState s = runProfile("room noise / silence (10s)", raw, 10000);
    check(s.intensityBand == Band::QUIET, "pure room noise must never leave BAND_QUIET");
    check(s.beatCount == 0 && s.strongHitCount == 0, "pure room noise must never produce a beat or strong hit");
    check(!s.dropHoldEverActivated, "pure room noise must never activate drop hold");
    check(s.stoppedForSilence, "10s of continuous room noise must trigger the persistent-quiet stop (7s timeout)");
  }

  // --- Profile 2: ordinary moderate music -- baseline sits across the
  // LOW/MEDIUM boundary with periodic kick-drum-style transients. ---
  {
    auto raw = [](uint32_t t) {
      float base = 0.28f;
      float phase = fmodf((float)t, 500.0f);
      float kick = (phase < 15.0f) ? 0.18f : 0.0f;  // one tick-wide bump every ~500ms
      return base + kick;
    };
    PipelineState s = runProfile("ordinary moderate music (12s)", raw, 12000);
    check((int)s.maxBandSeen >= (int)Band::LOW, "ordinary moderate music must reach at least BAND_LOW");
    check(s.beatCount > 0, "ordinary moderate music's periodic kicks must produce at least one beat");
  }

  // --- Profile 3: stronger transient-heavy song -- a brief 300ms ramp-in
  // (avoids the same startup-transient artifact as profile 4: raw jumping
  // instantly from 0 would create one giant, slow-to-resolve initial
  // transient that "uses up" the strong-hit cooldown before the band even
  // reaches PEAK, starving the later, realistic periodic spikes -- a test
  // artifact, not a controller behavior), then sharp spikes on a high
  // sustained baseline (so songEnergy itself settles well into PEAK, not
  // just a momentary spike). Must reach PEAK, produce strong hits, and
  // activate drop hold via the immediate (strong-hit) path. ---
  {
    auto raw = [](uint32_t t) {
      constexpr float rampMs = 300.0f, rampFrom = 0.10f, base = 0.75f;
      float level = ((float)t < rampMs) ? rampFrom + (base - rampFrom) * ((float)t / rampMs) : base;
      float phase = fmodf((float)t, 700.0f);
      float spike = ((float)t >= rampMs && phase < 15.0f) ? 0.25f : 0.0f;
      return level + spike;
    };
    PipelineState s = runProfile("stronger transient-heavy song (12s)", raw, 12000);
    check(s.maxBandSeen == Band::PEAK, "transient-heavy song must reach BAND_PEAK");
    check(s.strongHitCount > 0, "transient-heavy song's sharp spikes must produce at least one strong hit");
    check(s.dropHoldEverActivated, "transient-heavy song reaching PEAK with strong hits must activate drop hold");
    // Its very FIRST activation should come from the immediate strong-hit
    // path (it has real transients -- it shouldn't need to wait out the
    // sustain-confirm timer at all). It MAY still additionally re-qualify
    // via the sustain path later in a long section, once an earlier
    // hit-qualified session hits MUSIC_MOTOR_DROP_HOLD_MAX_MS and expires
    // while the section is still genuinely going -- that is the sustain
    // path correctly doing its job for a still-ongoing section, not a sign
    // the immediate path failed, so it is deliberately NOT asserted false
    // here.
    check(s.dropHoldFirstActivationViaHit,
          "a genuinely transient-heavy song's FIRST drop-hold activation should come from the immediate strong-hit "
          "path, not the sustained fallback");
  }

  // --- Profile 4: sustained compressed high-energy, WEAK transients -- a
  // genuinely gentle 4s ramp-in (avoids a startup-transient artifact from
  // raw jumping instantly from 0 -- a first-order lag's steady-state ramp
  // response is rate*timeConstant, so the ramp rate here is deliberately
  // kept well under BASELINE_ADAPT_RATE's own ~675ms time constant times
  // BEAT_DELTA_THRESHOLD, i.e. slow enough that even an ordinary beat never
  // fires from the ramp itself), then a constant high level with only a
  // tiny ripple. Must still reach HIGH/PEAK (from sustained level alone)
  // and activate drop hold via the NEW sustained path, specifically
  // WITHOUT ever having fired a strong hit. ---
  {
    auto raw = [](uint32_t t) {
      constexpr float rampMs = 4000.0f, rampFrom = 0.10f, sustainedBase = 0.55f;
      if ((float)t < rampMs) return rampFrom + (sustainedBase - rampFrom) * ((float)t / rampMs);
      return sustainedBase + 0.03f * sinf(((float)t - rampMs) * 0.05f);
    };
    PipelineState s = runProfile("sustained compressed high-energy, weak transients (18s)", raw, 18000);
    check(s.maxBandSeen == Band::HIGH || s.maxBandSeen == Band::PEAK,
          "sustained compressed high-energy must reach at least BAND_HIGH");
    check(s.strongHitCount == 0,
          "this profile's gentle ramp-in + small ripple is deliberately too weak to ever cross "
          "STRONG_HIT_DELTA_THRESHOLD -- if this fails, the profile itself (not the controller) needs adjusting");
    check(s.dropHoldEverActivated, "sustained HIGH/PEAK dwell alone (no strong hit) must still activate drop hold");
    check(s.dropHoldActivatedViaSustainOnly,
          "drop hold must have been activated via the sustained path specifically, not a strong hit -- "
          "'do not make strong hits mandatory for...drop hold'");
  }

  // --- Profile 5: weak/smooth "reference song" -- the original worst-case
  // physical capture (raw ~0.04-0.19, occasional soft bumps to ~0.19), with
  // a genuinely silent tail. NOT expected to reach MEDIUM/HIGH/PEAK under
  // the unmodified thresholds -- see this revision's report. Only asserts
  // well-behaved bounded output and correct eventual quiet-stop. ---
  {
    auto raw = [](uint32_t t) {
      if (t > 20000) return 0.03f;  // silent tail from 20s onward
      float phase = fmodf((float)t, 2500.0f);
      float bump = (phase < 200.0f) ? 0.06f : 0.0f;  // soft, wide bump toward the ~0.19 ceiling
      return 0.10f + bump;
    };
    PipelineState s = runProfile("weak/smooth reference-song profile, worst case (28s)", raw, 28000);
    check((int)s.maxBandSeen <= (int)Band::MEDIUM,
          "sanity check on the profile itself: this worst-case profile should never exceed MEDIUM under the "
          "unmodified thresholds (documents the accepted residual limitation, not a pass/fail bar on the controller)");
    check(s.stoppedForSilence, "even the worst-case profile must still reach the persistent-quiet stop once its tail goes quiet");
  }

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
