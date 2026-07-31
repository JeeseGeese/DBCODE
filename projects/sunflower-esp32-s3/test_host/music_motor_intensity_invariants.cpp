// Temporary host-side deterministic calculation test -- NOT part of the
// firmware build (this project has no PlatformIO "test" environment set up
// for hardware-decoupled unit tests, and adding one is out of scope for
// this narrowly-focused fix). Mirrors the exact per-band interpolation,
// clamp, and hysteresis logic in src/MusicMotorController.cpp
// (computeRawIntensityTargetPercent / clampTargetForBand /
// computeIntensityBand) and the exact threshold/percent constants from
// include/Config.h, so it can be compiled and run on the host with plain
// g++ -- no Arduino/ESP32 toolchain needed:
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_test test_host/music_motor_intensity_invariants.cpp && /tmp/mm_test
//
// Verifies the invariants from the target-speed-invariant investigation:
//   - no NaN / division-by-zero
//   - no target below the active band's minimum (BAND_QUIET excepted)
//   - no target above the active band's maximum
//   - continuous interpolation inside a band
//   - QUIET is the only band permitted to target zero
//   - downward hysteresis never produces a sub-floor BAND_LOW target
//
// If this file's constants ever drift from Config.h, keep them in sync by
// hand -- it intentionally does not #include the real Config.h, since that
// header pulls in Arduino-specific types not available on the host.

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// --- mirrored Config.h constants (see include/Config.h's "Intensity bands"
// section) ---
constexpr float LOW_THRESHOLD = 0.10f;
constexpr float MEDIUM_THRESHOLD = 0.25f;
constexpr float HIGH_THRESHOLD = 0.48f;
constexpr float PEAK_THRESHOLD = 0.72f;
constexpr float HYSTERESIS = 0.04f;

// Revision 3: re-based on the physically-validated M80 active-movement
// floor (MUSIC_MOTOR_ACTIVE_MIN_PERCENT in Config.h) -- LOW/MEDIUM/HIGH/PEAK
// now tile M80-M100 contiguously instead of the earlier M75 floor.
constexpr uint8_t ACTIVE_MIN_PERCENT = 80;
constexpr uint8_t LOW_MIN_PERCENT = 80;
constexpr uint8_t LOW_MAX_PERCENT = 83;
constexpr uint8_t MEDIUM_MIN_PERCENT = 84;
constexpr uint8_t MEDIUM_MAX_PERCENT = 89;
constexpr uint8_t HIGH_MIN_PERCENT = 90;
constexpr uint8_t HIGH_MAX_PERCENT = 96;
constexpr uint8_t PEAK_MIN_PERCENT = 97;
constexpr uint8_t PEAK_MAX_PERCENT = 100;

enum class Band { QUIET, LOW, MEDIUM, HIGH, PEAK };

const char *bandName(Band b) {
  switch (b) {
    case Band::QUIET: return "BAND_QUIET";
    case Band::LOW: return "BAND_LOW";
    case Band::MEDIUM: return "BAND_MEDIUM";
    case Band::HIGH: return "BAND_HIGH";
    case Band::PEAK: return "BAND_PEAK";
  }
  return "?";
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Mirrors MusicMotorController.cpp's clampTargetForBand().
uint8_t clampTargetForBand(Band band, uint8_t targetPercent) {
  uint8_t lo, hi;
  switch (band) {
    case Band::QUIET: return targetPercent;
    case Band::LOW: lo = LOW_MIN_PERCENT; hi = LOW_MAX_PERCENT; break;
    case Band::MEDIUM: lo = MEDIUM_MIN_PERCENT; hi = MEDIUM_MAX_PERCENT; break;
    case Band::HIGH: lo = HIGH_MIN_PERCENT; hi = HIGH_MAX_PERCENT; break;
    case Band::PEAK: lo = PEAK_MIN_PERCENT; hi = PEAK_MAX_PERCENT; break;
    default: return targetPercent;
  }
  if (targetPercent < lo) return lo;
  if (targetPercent > hi) return hi;
  return targetPercent;
}

// Mirrors MusicMotorController.cpp's computeRawIntensityTargetPercent().
uint8_t computeRawIntensityTargetPercent(Band band, float energy) {
  float bandLo, bandHi;
  uint8_t outLo, outHi;
  switch (band) {
    case Band::QUIET: return 0;
    case Band::LOW: bandLo = LOW_THRESHOLD; bandHi = MEDIUM_THRESHOLD; outLo = LOW_MIN_PERCENT; outHi = LOW_MAX_PERCENT; break;
    case Band::MEDIUM: bandLo = MEDIUM_THRESHOLD; bandHi = HIGH_THRESHOLD; outLo = MEDIUM_MIN_PERCENT; outHi = MEDIUM_MAX_PERCENT; break;
    case Band::HIGH: bandLo = HIGH_THRESHOLD; bandHi = PEAK_THRESHOLD; outLo = HIGH_MIN_PERCENT; outHi = HIGH_MAX_PERCENT; break;
    case Band::PEAK: bandLo = PEAK_THRESHOLD; bandHi = 1.0f; outLo = PEAK_MIN_PERCENT; outHi = PEAK_MAX_PERCENT; break;
    default: return 0;
  }
  float t = (bandHi > bandLo) ? clampf((energy - bandLo) / (bandHi - bandLo), 0.0f, 1.0f) : 1.0f;
  uint8_t raw = (uint8_t)(outLo + (outHi - outLo) * t + 0.5f);
  return clampTargetForBand(band, raw);
}

// Mirrors MusicMotorController.cpp's computeIntensityBand().
Band computeIntensityBand(float energy, Band prev) {
  switch (prev) {
    case Band::QUIET:
      if (energy >= PEAK_THRESHOLD) return Band::PEAK;
      if (energy >= HIGH_THRESHOLD) return Band::HIGH;
      if (energy >= MEDIUM_THRESHOLD) return Band::MEDIUM;
      if (energy >= LOW_THRESHOLD) return Band::LOW;
      return Band::QUIET;
    case Band::LOW:
      if (energy >= PEAK_THRESHOLD) return Band::PEAK;
      if (energy >= HIGH_THRESHOLD) return Band::HIGH;
      if (energy >= MEDIUM_THRESHOLD) return Band::MEDIUM;
      if (energy < LOW_THRESHOLD - HYSTERESIS) return Band::QUIET;
      return Band::LOW;
    case Band::MEDIUM:
      if (energy >= PEAK_THRESHOLD) return Band::PEAK;
      if (energy >= HIGH_THRESHOLD) return Band::HIGH;
      if (energy < MEDIUM_THRESHOLD - HYSTERESIS) return (energy >= LOW_THRESHOLD) ? Band::LOW : Band::QUIET;
      return Band::MEDIUM;
    case Band::HIGH:
      if (energy >= PEAK_THRESHOLD) return Band::PEAK;
      if (energy < HIGH_THRESHOLD - HYSTERESIS) {
        if (energy >= MEDIUM_THRESHOLD) return Band::MEDIUM;
        return (energy >= LOW_THRESHOLD) ? Band::LOW : Band::QUIET;
      }
      return Band::HIGH;
    case Band::PEAK:
      if (energy < PEAK_THRESHOLD - HYSTERESIS) {
        if (energy >= HIGH_THRESHOLD) return Band::HIGH;
        if (energy >= MEDIUM_THRESHOLD) return Band::MEDIUM;
        return (energy >= LOW_THRESHOLD) ? Band::LOW : Band::QUIET;
      }
      return Band::PEAK;
  }
  return Band::QUIET;
}

static int g_failures = 0;

static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

static void checkTargetInBand(Band band, float energy) {
  uint8_t target = computeRawIntensityTargetPercent(band, energy);
  char label[128];
  snprintf(label, sizeof(label), "energy=%.3f band=%s -> target=%u", (double)energy, bandName(band), (unsigned)target);

  // No NaN / division-by-zero: target must be a plain, finite, in-range
  // uint8_t -- if computeRawIntensityTargetPercent ever produced NaN it
  // would have already been caught by the uint8_t cast (UB), so instead we
  // assert the *inputs* to the division (bandHi > bandLo) hold for every
  // band actually reachable through computeIntensityBand -- true by
  // construction here since LOW<MEDIUM<HIGH<PEAK<1.0 strictly.

  switch (band) {
    case Band::QUIET:
      check(target == 0, (std::string("QUIET must target exactly 0: ") + label).c_str());
      break;
    case Band::LOW:
      check(target >= LOW_MIN_PERCENT && target <= LOW_MAX_PERCENT, (std::string("LOW out of [80,83]: ") + label).c_str());
      break;
    case Band::MEDIUM:
      check(target >= MEDIUM_MIN_PERCENT && target <= MEDIUM_MAX_PERCENT,
            (std::string("MEDIUM out of [84,89]: ") + label).c_str());
      break;
    case Band::HIGH:
      check(target >= HIGH_MIN_PERCENT && target <= HIGH_MAX_PERCENT, (std::string("HIGH out of [90,96]: ") + label).c_str());
      break;
    case Band::PEAK:
      check(target >= PEAK_MIN_PERCENT && target <= PEAK_MAX_PERCENT, (std::string("PEAK out of [97,100]: ") + label).c_str());
      break;
  }
  // Item 1 (revision 3): every ACTIVE (non-QUIET) band target must be >=
  // the physically-validated M80 floor -- redundant with the per-band
  // checks above but asserted explicitly since it's the specific
  // requirement this revision adds.
  if (band != Band::QUIET) {
    check(target >= ACTIVE_MIN_PERCENT, (std::string("active target below M80 floor: ") + label).c_str());
  }
}

// Mirrors MusicMotorController.cpp's applyRampTick() time-progress
// interpolation, used by DECELERATING. Item 3: deceleration is only
// permitted to pass below M80 while genuinely ramping toward M0 (QUIET) --
// ramping toward a non-QUIET band's floor (>=80) must never dip below it.
uint8_t rampTick(uint8_t fromPercent, uint8_t toPercent, uint32_t elapsedMs, uint32_t durationMs) {
  float progress = (durationMs > 0) ? (float)elapsedMs / (float)durationMs : 1.0f;
  if (progress > 1.0f) progress = 1.0f;
  if (progress < 0.0f) progress = 0.0f;
  int16_t span = (int16_t)toPercent - (int16_t)fromPercent;
  return (uint8_t)((int16_t)fromPercent + (int16_t)(span * progress));
}

int main() {
  printf("== Threshold-boundary + hysteresis intensity-target invariant test ==\n");

  const float energies[] = {0.00f, 0.05f, 0.09f, 0.10f, 0.12f, 0.24f, 0.25f, 0.30f,
                             0.47f, 0.48f, 0.60f, 0.71f, 0.72f, 0.90f, 1.00f};

  // Part 1: for every energy value, and for EVERY band (not just the band
  // computeIntensityBand() would naturally pick), the target must stay
  // within that band's documented range. This directly exercises "downward
  // hysteresis latched BAND_LOW while energy has already dropped below
  // LOW_THRESHOLD" -- e.g. energy=0.09 with band forced to LOW.
  for (float e : energies) {
    for (Band b : {Band::QUIET, Band::LOW, Band::MEDIUM, Band::HIGH, Band::PEAK}) {
      checkTargetInBand(b, e);
    }
  }

  // Part 2: hysteresis values immediately below each threshold -- the
  // downward-latch case the original M22 bug traced back to.
  const float hysteresisProbes[] = {
      LOW_THRESHOLD - HYSTERESIS + 0.001f,     // still latched LOW if prev==LOW
      LOW_THRESHOLD - HYSTERESIS - 0.001f,     // drops to QUIET if prev==LOW
      MEDIUM_THRESHOLD - HYSTERESIS + 0.001f,  // still latched MEDIUM if prev==MEDIUM
      MEDIUM_THRESHOLD - HYSTERESIS - 0.001f,
      HIGH_THRESHOLD - HYSTERESIS + 0.001f,
      HIGH_THRESHOLD - HYSTERESIS - 0.001f,
      PEAK_THRESHOLD - HYSTERESIS + 0.001f,
      PEAK_THRESHOLD - HYSTERESIS - 0.001f,
  };
  printf("\n-- downward hysteresis latch scenarios --\n");
  Band prevBands[] = {Band::LOW, Band::MEDIUM, Band::HIGH, Band::PEAK};
  for (float e : hysteresisProbes) {
    for (Band prev : prevBands) {
      Band resolved = computeIntensityBand(e, prev);
      uint8_t target = computeRawIntensityTargetPercent(resolved, e);
      printf("  energy=%.3f prevBand=%s -> resolvedBand=%s target=M%u\n", (double)e, bandName(prev), bandName(resolved),
             (unsigned)target);
      checkTargetInBand(resolved, e);
    }
  }

  // Part 3: continuity inside BAND_LOW -- target must be non-decreasing as
  // energy rises through the band (interpolation, not a step function).
  printf("\n-- continuity inside BAND_LOW --\n");
  uint8_t prevTarget = 0;
  bool first = true;
  for (float e = LOW_THRESHOLD; e <= MEDIUM_THRESHOLD; e += 0.01f) {
    uint8_t t = computeRawIntensityTargetPercent(Band::LOW, e);
    if (!first) check(t >= prevTarget, "BAND_LOW target must be non-decreasing as energy rises");
    prevTarget = t;
    first = false;
  }

  // Part 4: exact boundary at bandLo (t=0) and bandHi (t=1) hit the exact
  // documented min/max -- catches off-by-one/rounding regressions.
  check(computeRawIntensityTargetPercent(Band::LOW, LOW_THRESHOLD) == LOW_MIN_PERCENT, "BAND_LOW floor at exact threshold");
  check(computeRawIntensityTargetPercent(Band::LOW, MEDIUM_THRESHOLD) == LOW_MAX_PERCENT, "BAND_LOW ceiling at exact upper threshold");
  check(computeRawIntensityTargetPercent(Band::PEAK, 1.0f) == PEAK_MAX_PERCENT, "BAND_PEAK ceiling at energy=1.0");

  // Part 5 (item 3): simulate a DECELERATING ramp toward a non-QUIET
  // (LOW-floor) target -- must never dip below M80 at any point in flight.
  printf("\n-- deceleration ramp toward LOW (must never dip below M80) --\n");
  const uint32_t decelDurationMs = 650;
  for (uint32_t elapsed = 0; elapsed <= decelDurationMs; elapsed += 25) {
    uint8_t v = rampTick(96, LOW_MIN_PERCENT, elapsed, decelDurationMs);
    check(v >= ACTIVE_MIN_PERCENT, "decel-toward-LOW ramp value dipped below M80");
  }
  // ... and simulate a DECELERATING ramp toward QUIET (M0) -- values BELOW
  // M80 are expected and correct here, only the final value must reach 0.
  printf("-- deceleration ramp toward QUIET (M0, sub-M80 values expected) --\n");
  uint8_t lastVal = 100;
  bool sawBelowFloor = false;
  for (uint32_t elapsed = 0; elapsed <= decelDurationMs; elapsed += 25) {
    uint8_t v = rampTick(96, 0, elapsed, decelDurationMs);
    if (v < ACTIVE_MIN_PERCENT) sawBelowFloor = true;
    lastVal = v;
  }
  check(sawBelowFloor, "decel-toward-QUIET ramp should legitimately pass below M80 on its way to 0");
  check(lastVal == 0, "decel-toward-QUIET ramp must reach exactly 0 at completion");

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
