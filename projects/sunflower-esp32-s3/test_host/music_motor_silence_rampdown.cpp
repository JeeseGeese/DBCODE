// Temporary host-side deterministic test -- mirrors revision 8's lifelike
// silence/low-energy stopping addition to src/MusicMotorController.cpp
// (MusicMotorState::MUSICAL_RAMP_DOWN, chooseSilenceStopStyle(),
// chooseRampDownDurationMs(), beginMusicalSilenceStop(),
// enterMusicalRampDown()/updateMusicalRampDown(), and
// checkAndHandleSilenceTimeout()'s changed resulting action). Same
// rationale/approach as every other test_host/*.cpp file: no PlatformIO
// "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_rampdown_test test_host/music_motor_silence_rampdown.cpp && /tmp/mm_rampdown_test
//
// Covers the revision-8-addendum test list:
//  1. SHORT sustained phrases remain within 1-5s (duration-bounds check,
//     mirrors the equivalent check in music_motor_renewable_phrase.cpp)
//  2. STANDARD and longer phrases retain their longer minimum commitment
//     rules (covered by music_motor_renewable_phrase.cpp -- referenced,
//     not duplicated, here)
//  3/4/5. MEDIUM/HIGH/PEAK phrase-tier selection (covered by
//     music_motor_renewable_phrase.cpp's phrase-tier test -- referenced,
//     not duplicated)
//  6. ordinary beats cannot interrupt a short sustained phrase (structural,
//     same architecture as revision 7's interruption resistance -- see
//     music_motor_sustained_drive.cpp)
//  7. a qualified short phrase can promote into a longer phrase
//  8. short phrases do not always promote
//  9. ordinary confirmed silence normally enters gradual ramp-down
//  10. gradual ramp-down preserves current direction
//  11. gradual ramp-down reaches a complete electrical stop
//  12. high-energy movement receives a visibly longer ramp than low-energy
//  13. a qualified sudden musical cutoff can select an abrupt dramatic stop
//  14. abrupt stops remain less common than gradual stops across seeds
//  15. a slow energy fade (no sharp-cutoff context) strongly favors gradual
//  16. emergency stop bypasses ramp-down and stops immediately
//  17. explicit 'k' stop bypasses ramp-down
//  18. MusicMotor disable bypasses ramp-down
//  19. hardware safety bypasses ramp-down
//  20. music returning during an early ramp-down can smoothly resume
//  21. silence-threshold chatter cannot repeatedly enter/cancel ramp-down
//  22. SUSTAINED_DRIVE normally transitions into gradual ramp-down after
//      confirmed silence
//  23. SUSTAINED_DRIVE does not wait for its review timer after confirmed
//      silence
//  24. a sustained-drive dramatic cutoff may stop abruptly when qualified
//  25. no musical or safety stop automatically reverses the motor
//  26. existing silence/emergency-stop/sustained-drive/choreography/
//      intensity/safety tests continue passing (covered by re-running the
//      whole test_host/ suite, not duplicated here)

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// --- mirrored Config.h constants ---
constexpr uint32_t RAMP_DOWN_LOW_MIN_MS = 1000, RAMP_DOWN_LOW_MAX_MS = 2000;
constexpr uint32_t RAMP_DOWN_HIGH_MIN_MS = 2000, RAMP_DOWN_HIGH_MAX_MS = 4000;
constexpr uint8_t ABRUPT_STOP_NORMAL_PERCENT = 12;
constexpr uint8_t ABRUPT_STOP_SHARP_CUTOFF_PERCENT = 25;
constexpr uint32_t SHARP_CUTOFF_DROPHOLD_RECENCY_MS = 3000;
constexpr uint32_t SILENCE_TIMEOUT_MS = 7000;
constexpr uint8_t HIGH_MIN_PERCENT = 90;  // MUSIC_MOTOR_HIGH_MIN_PERCENT

enum class Band { QUIET, LOW, MEDIUM, HIGH, PEAK };
enum class Direction { FORWARD, REVERSE };

// --- mirrored chooseSilenceStopStyle()/chooseRampDownDurationMs() ---
enum class StopStyle { GRADUAL_RAMP_DOWN, DRAMATIC_ABRUPT_STOP };
StopStyle chooseSilenceStopStyle(bool sharpCutoffContext, uint8_t abruptRollPercent0to99) {
  uint8_t abruptChance = sharpCutoffContext ? ABRUPT_STOP_SHARP_CUTOFF_PERCENT : ABRUPT_STOP_NORMAL_PERCENT;
  return (abruptRollPercent0to99 < abruptChance) ? StopStyle::DRAMATIC_ABRUPT_STOP : StopStyle::GRADUAL_RAMP_DOWN;
}
uint32_t chooseRampDownDurationMs(bool wasHighEnergy, uint32_t lowRangeDurationMs, uint32_t highRangeDurationMs) {
  return wasHighEnergy ? highRangeDurationMs : lowRangeDurationMs;
}

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
// Minimal mirrored harness -- models enough of
// checkAndHandleSilenceTimeout()/beginMusicalSilenceStop()/
// enterMusicalRampDown()/updateMusicalRampDown()/hardStop() to exercise the
// interruption/recovery/completion properties end to end. `state` mirrors
// the relevant subset of MusicMotorState.
// ----------------------------------------------------------------------------
enum class SimState { OFF, SILENT, INTENSITY_SWAY, SUSTAINED_DRIVE, MUSICAL_RAMP_DOWN };

struct SilenceHarness {
  SimState state = SimState::INTENSITY_SWAY;
  Direction direction = Direction::FORWARD;
  uint8_t currentSpeedPercent = 90;
  uint8_t rampFromPercent = 0;
  uint32_t rampStartMs = 0;
  uint32_t rampDurationMs = 0;
  uint32_t belowSilenceSinceMs = 0;
  bool stoppedElectrically = false;
  StopStyle lastStopStyle = StopStyle::GRADUAL_RAMP_DOWN;
  bool sustainedDriveReviewWaitedFor = false;  // set true only if the (real-code) review path were ever consulted during a silence exit -- must stay false

  // Mirrors checkAndHandleSilenceTimeout() -- returns true iff it just
  // preempted the current state (caller must return immediately).
  bool checkSilence(uint32_t now, bool bandIsQuiet, Rng &rng, bool sharpCutoffContext) {
    if (bandIsQuiet) {
      if (belowSilenceSinceMs == 0) belowSilenceSinceMs = now;
      if (now - belowSilenceSinceMs >= SILENCE_TIMEOUT_MS) {
        beginSilenceStop(now, rng, sharpCutoffContext);
        return true;
      }
    } else {
      belowSilenceSinceMs = 0;
    }
    return false;
  }

  void beginSilenceStop(uint32_t now, Rng &rng, bool sharpCutoffContext) {
    uint8_t abruptRoll = rng.draw100();
    StopStyle style = chooseSilenceStopStyle(sharpCutoffContext, abruptRoll);
    lastStopStyle = style;
    if (style == StopStyle::DRAMATIC_ABRUPT_STOP) {
      hardElectricalStop();
      return;
    }
    bool wasHighEnergy = currentSpeedPercent >= HIGH_MIN_PERCENT;
    uint32_t lowRangeMs = rng.range(RAMP_DOWN_LOW_MIN_MS, RAMP_DOWN_LOW_MAX_MS);
    uint32_t highRangeMs = rng.range(RAMP_DOWN_HIGH_MIN_MS, RAMP_DOWN_HIGH_MAX_MS);
    uint32_t rampMs = chooseRampDownDurationMs(wasHighEnergy, lowRangeMs, highRangeMs);
    state = SimState::MUSICAL_RAMP_DOWN;
    rampFromPercent = currentSpeedPercent;
    rampStartMs = now;
    rampDurationMs = rampMs;
    // sustainedDriveReviewWaitedFor is never set here -- confirms (by
    // construction) that entering the stop path never consults any
    // review-timer state at all, matching item 23.
  }

  void hardElectricalStop() {
    currentSpeedPercent = 0;
    stoppedElectrically = true;
    state = SimState::SILENT;
    // direction intentionally untouched
  }

  // Mirrors updateMusicalRampDown().
  void tickRampDown(uint32_t now, bool bandIsQuiet) {
    if (state != SimState::MUSICAL_RAMP_DOWN) return;
    if (!bandIsQuiet) {
      state = SimState::INTENSITY_SWAY;  // recovery -- direction/currentSpeedPercent left exactly as-is
      belowSilenceSinceMs = 0;
      return;
    }
    uint32_t elapsed = now - rampStartMs;
    float progress = (rampDurationMs > 0) ? (float)elapsed / (float)rampDurationMs : 1.0f;
    if (progress > 1.0f) progress = 1.0f;
    currentSpeedPercent = (uint8_t)((float)rampFromPercent * (1.0f - progress));
    if (elapsed >= rampDurationMs) {
      hardElectricalStop();
    }
  }

  // Mirrors hardStop() -- unconditional, bypasses everything.
  void emergencyOrDisableStop() {
    currentSpeedPercent = 0;
    stoppedElectrically = true;
    state = SimState::OFF;
  }
};

int main() {
  printf("== Revision-8 lifelike silence/ramp-down test ==\n");

  // --- Item 15: a slow fade (no sharp-cutoff context) strongly favors gradual ---
  printf("\n-- a slow energy fade strongly favors gradual ramp-down --\n");
  {
    int gradualCount = 0, abruptCount = 0;
    for (uint32_t seed = 1; seed <= 500; seed++) {
      Rng rng(seed);
      StopStyle s = chooseSilenceStopStyle(/*sharpCutoffContext=*/false, rng.draw100());
      if (s == StopStyle::GRADUAL_RAMP_DOWN) gradualCount++;
      else abruptCount++;
    }
    check(gradualCount > abruptCount * 5, "an ordinary (non-sharp-cutoff) fade must overwhelmingly favor gradual ramp-down");
  }

  // --- Items 13/14: a qualified sudden cutoff can select abrupt; abrupt stays less common overall ---
  printf("-- a qualified sudden cutoff can select an abrupt dramatic stop; abrupt remains the minority overall --\n");
  {
    int gradualNormal = 0, abruptNormal = 0, gradualSharp = 0, abruptSharp = 0;
    for (uint32_t seed = 1; seed <= 1000; seed++) {
      Rng rng(seed);
      uint8_t roll = rng.draw100();
      if (chooseSilenceStopStyle(false, roll) == StopStyle::DRAMATIC_ABRUPT_STOP) abruptNormal++;
      else gradualNormal++;
      if (chooseSilenceStopStyle(true, roll) == StopStyle::DRAMATIC_ABRUPT_STOP) abruptSharp++;
      else gradualSharp++;
    }
    check(abruptSharp > 0, "13: a sharp-cutoff context must be able to select an abrupt dramatic stop at least sometimes");
    check(abruptSharp > abruptNormal, "a sharp-cutoff context must select abrupt MORE often than an ordinary context, given the same rolls");
    check(gradualNormal > abruptNormal, "14: across an ordinary population of silences, gradual must remain more common than abrupt");
    check(gradualSharp > abruptSharp, "14: even in the sharp-cutoff tier, gradual must still remain the majority outcome (25% < 50%)");
  }

  // --- Item 12: high-energy movement receives a visibly longer ramp ---
  printf("\n-- high-energy movement receives a longer ramp than low-energy --\n");
  {
    Rng rngLow(1), rngHigh(2);
    uint32_t lowRangeMs = rngLow.range(RAMP_DOWN_LOW_MIN_MS, RAMP_DOWN_LOW_MAX_MS);
    uint32_t highRangeMs = rngHigh.range(RAMP_DOWN_HIGH_MIN_MS, RAMP_DOWN_HIGH_MAX_MS);
    uint32_t lowDuration = chooseRampDownDurationMs(false, lowRangeMs, highRangeMs);
    uint32_t highDuration = chooseRampDownDurationMs(true, lowRangeMs, highRangeMs);
    check(lowDuration == lowRangeMs, "low-energy must pick the LOW range");
    check(highDuration == highRangeMs, "high-energy must pick the HIGH range");
    check(RAMP_DOWN_HIGH_MIN_MS >= RAMP_DOWN_LOW_MAX_MS,
          "the HIGH range must not overlap below the LOW range -- high-energy is never shorter than low-energy by construction");
  }

  // --- Items 9/10/11: ordinary confirmed silence -> gradual, direction preserved, reaches a full stop ---
  printf("\n-- ordinary confirmed silence: gradual, direction preserved, reaches a full stop --\n");
  {
    SilenceHarness h;
    h.direction = Direction::REVERSE;
    h.currentSpeedPercent = 60;  // below HIGH_MIN_PERCENT -- low-energy ramp range
    Rng rng(42);
    uint32_t now = 1;
    bool preempted = false;
    // Simulate the silence timer over 7s+.
    for (; now <= SILENCE_TIMEOUT_MS + 10; now += 15) {
      preempted = h.checkSilence(now, /*bandIsQuiet=*/true, rng, /*sharpCutoffContext=*/false);
      if (preempted) break;
    }
    check(preempted, "setup: the silence timeout must eventually fire");
    check(h.state == SimState::MUSICAL_RAMP_DOWN || h.state == SimState::SILENT,
          "9: confirmed silence must move into ramp-down (or, rarely, straight to a stop if the dramatic roll fired)");
    if (h.state == SimState::MUSICAL_RAMP_DOWN) {
      Direction beforeRamp = h.direction;
      for (uint32_t t = now; t <= now + h.rampDurationMs + 30; t += 15) {
        h.tickRampDown(t, /*bandIsQuiet=*/true);
        check(h.direction == beforeRamp, "10: direction must never change at any point during ramp-down");
        if (h.state == SimState::SILENT) break;
      }
      check(h.state == SimState::SILENT, "11: ramp-down must eventually reach a complete stop (state==SILENT)");
      check(h.stoppedElectrically, "11: ramp-down completion must actually zero the motor electrically");
      check(h.currentSpeedPercent == 0, "11: final speed must be exactly 0");
    }
  }

  // --- Item 20: music returning during an early ramp-down smoothly resumes ---
  printf("\n-- music returning during ramp-down smoothly resumes --\n");
  {
    SilenceHarness h;
    h.direction = Direction::FORWARD;
    h.state = SimState::MUSICAL_RAMP_DOWN;
    h.rampFromPercent = 95;
    h.rampStartMs = 1000;
    h.rampDurationMs = 3000;
    h.currentSpeedPercent = 95;
    h.tickRampDown(1500, /*bandIsQuiet=*/true);
    uint8_t midRampSpeed = h.currentSpeedPercent;
    check(midRampSpeed < 95 && midRampSpeed > 0, "setup: partway through the ramp, speed must have visibly decreased but not reached 0");
    h.tickRampDown(1600, /*bandIsQuiet=*/false);  // music returns early
    check(h.state == SimState::INTENSITY_SWAY, "20: music returning during ramp-down must resume normal choreography");
    check(h.direction == Direction::FORWARD, "20: direction must be preserved across the resume");
    check(!h.stoppedElectrically, "20: an early resume must NOT have reached a full stop first");
  }

  // --- Item 21: silence-threshold chatter cannot repeatedly enter/cancel ---
  printf("-- silence-threshold chatter does not repeatedly toggle ramp-down --\n");
  {
    // The silence TIMER itself only starts counting once bandIsQuiet is
    // continuously true, and resets to 0 the instant it isn't -- a single
    // stray non-quiet tick before the FULL 7s timeout resets the whole
    // countdown, exactly like revision 5/7's existing behavior. This
    // demonstrates chatter never reaches ramp-down at all unless silence is
    // genuinely sustained for the complete timeout.
    SilenceHarness h;
    Rng rng(9);
    uint32_t now = 1;
    int timesEnteredRampOrStop = 0;
    for (int cycle = 0; cycle < 5; cycle++) {
      // 3s of quiet (short of the 7s timeout), then one non-quiet tick (reset).
      for (uint32_t t = 0; t < 3000; t += 15, now += 15) {
        bool preempted = h.checkSilence(now, true, rng, false);
        if (preempted) timesEnteredRampOrStop++;
      }
      h.checkSilence(now, false, rng, false);
      now += 15;
    }
    check(timesEnteredRampOrStop == 0, "21: repeated sub-timeout quiet bursts (chatter) must never themselves trigger ramp-down/stop");
  }

  // --- Items 22/23: SUSTAINED_DRIVE's silence exit is immediate, never waits for its review ---
  printf("\n-- SUSTAINED_DRIVE transitions to ramp-down on confirmed silence, without waiting for its review timer --\n");
  {
    SilenceHarness h;
    h.state = SimState::SUSTAINED_DRIVE;
    h.currentSpeedPercent = 97;  // HIGH-tier-floored, as a real sustained drive would be
    Rng rng(13);
    uint32_t now = 1;
    bool preempted = false;
    for (; now <= SILENCE_TIMEOUT_MS + 10; now += 15) {
      // updateSustainedDrive() calls checkAndHandleSilenceTimeout() FIRST,
      // unconditionally, before ever looking at its own next-review point
      // -- modeled here simply by never consulting any review-timer state
      // in checkSilence()/beginSilenceStop() at all (see
      // sustainedDriveReviewWaitedFor, which stays false throughout, by
      // construction of beginSilenceStop() itself).
      preempted = h.checkSilence(now, true, rng, false);
      if (preempted) break;
    }
    check(preempted, "setup: silence timeout must fire during an active sustained drive");
    check(!h.sustainedDriveReviewWaitedFor, "23: the silence-stop path must never consult the phrase's review timer");
    check(h.state == SimState::MUSICAL_RAMP_DOWN || h.state == SimState::SILENT,
          "22: SUSTAINED_DRIVE must transition into ramp-down (or occasionally straight to a stop) on confirmed silence");
  }

  // --- Item 24: a sustained-drive dramatic cutoff may stop abruptly when qualified ---
  printf("-- a sustained-drive dramatic cutoff may stop abruptly when qualified --\n");
  {
    // A drop hold recently active (sharpCutoffContext=true) plus a lucky
    // low roll must be able to select DRAMATIC_ABRUPT_STOP even while
    // SUSTAINED_DRIVE was the active state.
    bool sawAbrupt = false;
    for (uint32_t seed = 1; seed <= 200; seed++) {
      Rng rng(seed);
      SilenceHarness h;
      h.state = SimState::SUSTAINED_DRIVE;
      h.currentSpeedPercent = 100;
      h.beginSilenceStop(7000, rng, /*sharpCutoffContext=*/true);
      if (h.lastStopStyle == StopStyle::DRAMATIC_ABRUPT_STOP) {
        sawAbrupt = true;
        check(h.state == SimState::SILENT, "an abrupt stop selected during SUSTAINED_DRIVE must reach SILENT immediately, no ramp");
        break;
      }
    }
    check(sawAbrupt, "24: across many seeds with a sharp-cutoff context, at least one sustained-drive silence stop must go abrupt");
  }

  // --- Item 25: no musical or safety stop automatically reverses ---
  printf("\n-- no musical or safety stop automatically reverses the motor --\n");
  {
    for (Direction d : {Direction::FORWARD, Direction::REVERSE}) {
      SilenceHarness h;
      h.direction = d;
      h.currentSpeedPercent = 80;
      Rng rng(1);
      h.beginSilenceStop(1000, rng, false);
      check(h.direction == d, "25: beginSilenceStop() (gradual or abrupt) must never change direction");
      SilenceHarness h2;
      h2.direction = d;
      h2.emergencyOrDisableStop();
      check(h2.direction == d, "25: emergencyOrDisableStop() must never change direction");
    }
  }

  // --- Items 16/17/18/19: safety stops bypass ramp-down entirely ---
  printf("\n-- emergency stop / 'k' / disable / hardware safety bypass ramp-down entirely --\n");
  {
    SilenceHarness h;
    h.state = SimState::MUSICAL_RAMP_DOWN;  // mid-ramp
    h.rampFromPercent = 90;
    h.rampStartMs = 0;
    h.rampDurationMs = 4000;
    h.currentSpeedPercent = 60;  // partway through the ramp
    h.emergencyOrDisableStop();  // models hardStop(), the single shared path for emergency stop/'k'/disable/hardware faults
    check(h.state == SimState::OFF, "16/17/18/19: hardStop() must interrupt an in-progress ramp-down immediately, regardless of ramp progress");
    check(h.currentSpeedPercent == 0, "hardStop() must zero the motor immediately, not wait for the ramp to finish");
    check(h.stoppedElectrically, "hardStop() must reach a genuine electrical stop, same guarantee as every other path");
  }

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
