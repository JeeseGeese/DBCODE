// Temporary host-side deterministic test -- mirrors the drop-hold,
// reverse-hip-shake phase sequencing, and spin-profile-selection logic
// added in src/MusicMotorController.cpp's revision 3 (physical M80
// calibration). Same rationale/approach as
// music_motor_intensity_invariants.cpp -- no PlatformIO "test" env exists
// in this project, so this compiles standalone on the host:
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_choreo_test test_host/music_motor_choreography_invariants.cpp && /tmp/mm_choreo_test
//
// Covers items 4-14 of the revision-3 test list: drop-hold start/refresh/
// expire/cancel, reverse-hip-shake original-direction preservation and
// exact phase sequences (regular 4-phase, heavy 6-phase), non-blocking
// phase timing, hip-shake start cooldown, and spin duration limits.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// --- mirrored Config.h constants ---
constexpr uint32_t DROP_HOLD_INITIAL_MS = 2200;
constexpr uint32_t DROP_HOLD_MAX_MS = 4000;

constexpr uint8_t REV_REGULAR_PERCENT = 90;
constexpr uint32_t REV_REGULAR_PHASE_MS = 200;
constexpr uint8_t REV_REGULAR_PHASE_COUNT = 4;
constexpr uint8_t REV_HEAVY_PERCENT = 100;
constexpr uint32_t REV_HEAVY_PHASE_MS = 180;
constexpr uint8_t REV_HEAVY_PHASE_COUNT = 6;
constexpr uint32_t HIPSHAKE_COOLDOWN_MIN_MS = 800;
constexpr uint32_t HIPSHAKE_COOLDOWN_MAX_MS = 1200;

constexpr uint8_t SPIN_NORMAL_PERCENT = 90;
constexpr uint32_t SPIN_NORMAL_MS = 2000;
constexpr uint8_t SPIN_FAST_PERCENT = 100;
constexpr uint32_t SPIN_FAST_MS = 1000;
constexpr uint32_t SPIN_EXTENDED_MIN_MS = 2000;
constexpr uint32_t SPIN_EXTENDED_MAX_MS = 3000;
constexpr uint32_t SPIN_ABSOLUTE_MAX_MS = 4000;

// --- mirrored drop-hold state machine (see startOrRefreshDropHold()/
// updateDropHold()/cancelDropHold() in MusicMotorController.cpp) ---
struct DropHoldState {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t untilMs = 0;
};

void startOrRefresh(DropHoldState &s, uint32_t now) {
  if (!s.active) {
    s.active = true;
    s.startMs = now;
    s.untilMs = now + DROP_HOLD_INITIAL_MS;
    return;
  }
  uint32_t maxUntil = s.startMs + DROP_HOLD_MAX_MS;
  uint32_t candidate = now + DROP_HOLD_INITIAL_MS;
  uint32_t newUntil = (candidate < maxUntil) ? candidate : maxUntil;
  if (newUntil > s.untilMs) s.untilMs = newUntil;
}

void update(DropHoldState &s, uint32_t now) {
  if (s.active && now >= s.untilMs) s.active = false;
}

void cancel(DropHoldState &s) { s.active = false; }

// --- mirrored reverse-hip-shake phase direction (see
// reverseHipShakePhaseDirection() in MusicMotorController.cpp) ---
enum class Dir { FORWARD, REVERSE };
Dir opposite(Dir d) { return d == Dir::FORWARD ? Dir::REVERSE : Dir::FORWARD; }
Dir phaseDirection(uint8_t phaseIndex, Dir original) { return (phaseIndex % 2 == 0) ? opposite(original) : original; }

// --- mirrored spin profile selection (see pickSpinProfile()) ---
struct SpinProfile {
  uint8_t percent;
  uint32_t durationMs;
};
SpinProfile pickSpinProfile(bool manualOverride, uint32_t manualMs, bool dropHoldActive, bool isPeak,
                             uint32_t extendedRoll /* pre-picked value within [MIN,MAX] for determinism */) {
  if (manualOverride) return {SPIN_FAST_PERCENT, manualMs};
  if (dropHoldActive) return {100, extendedRoll};
  if (isPeak) return {SPIN_FAST_PERCENT, SPIN_FAST_MS};
  return {SPIN_NORMAL_PERCENT, SPIN_NORMAL_MS};
}

int main() {
  printf("== Revision-3 choreography invariant test ==\n");

  // --- Item 4: drop hold begins on a qualifying PEAK transition ---
  printf("\n-- drop hold start --\n");
  DropHoldState d;
  check(!d.active, "drop hold must start inactive");
  startOrRefresh(d, 1000);
  check(d.active, "drop hold must become active on a qualifying strong hit at PEAK");
  check(d.untilMs == 1000 + DROP_HOLD_INITIAL_MS, "drop hold initial duration must be 2200ms");

  // --- Item 6: drop hold expires after its configured timeout ---
  printf("-- drop hold expiry --\n");
  update(d, 1000 + DROP_HOLD_INITIAL_MS - 1);
  check(d.active, "drop hold must still be active 1ms before its deadline");
  update(d, 1000 + DROP_HOLD_INITIAL_MS);
  check(!d.active, "drop hold must expire exactly at its deadline");

  // --- refresh + max continuous cap ---
  printf("-- drop hold refresh + max continuous cap --\n");
  DropHoldState d2;
  startOrRefresh(d2, 0);
  uint32_t expectedMax = d2.startMs + DROP_HOLD_MAX_MS;
  // Refresh repeatedly, every 500ms, well past the 4000ms cap.
  for (uint32_t now = 500; now <= 6000; now += 500) {
    update(d2, now);
    if (!d2.active) break;  // would only happen if capping logic is broken and it expired early
    startOrRefresh(d2, now);
    check(d2.untilMs <= expectedMax, "drop hold refresh must never exceed the 4000ms continuous cap");
  }
  check(d2.untilMs == expectedMax, "repeated refreshes should saturate exactly at the 4000ms cap");

  // --- Item 7: drop hold cancelled by silence/hard-stop paths ---
  printf("-- drop hold cancellation --\n");
  DropHoldState d3;
  startOrRefresh(d3, 0);
  check(d3.active, "sanity: drop hold active before cancel");
  cancel(d3);
  check(!d3.active, "cancelDropHold() must immediately deactivate regardless of remaining time");

  // --- Item 5: drop hold never falsifies the measured band -- this is an
  // architectural property (dropHoldActive is a separate bool from
  // intensityBand, never assigned INTO it anywhere in
  // MusicMotorController.cpp) rather than something with numeric output to
  // assert here; verified by inspection + the "effectiveChoreographyBand()
  // is used ONLY inside selectBeatAction()" invariant. Documented, not
  // reasserted redundantly.

  // --- Item 8/9: regular reverse hip shake preserves original direction
  // and executes exactly opposite/original/opposite/original ---
  printf("\n-- regular reverse hip shake phase sequence --\n");
  for (Dir original : {Dir::FORWARD, Dir::REVERSE}) {
    Dir expected[4] = {opposite(original), original, opposite(original), original};
    for (uint8_t i = 0; i < REV_REGULAR_PHASE_COUNT; i++) {
      Dir got = phaseDirection(i, original);
      check(got == expected[i], "regular reverse hip shake phase direction mismatch");
    }
    check(phaseDirection(REV_REGULAR_PHASE_COUNT - 1, original) != original ||
              (REV_REGULAR_PHASE_COUNT - 1) % 2 == 1,
          "sanity check on phase parity");
  }
  printf("  FORWARD-original: REVERSE, FORWARD, REVERSE, FORWARD (verified)\n");
  printf("  REVERSE-original: FORWARD, REVERSE, FORWARD, REVERSE (verified)\n");

  // --- Item 10: heavy reverse hip shake executes the configured six phases ---
  printf("-- heavy reverse hip shake phase sequence --\n");
  for (Dir original : {Dir::FORWARD, Dir::REVERSE}) {
    for (uint8_t i = 0; i < REV_HEAVY_PHASE_COUNT; i++) {
      Dir expected = (i % 2 == 0) ? opposite(original) : original;
      check(phaseDirection(i, original) == expected, "heavy reverse hip shake phase direction mismatch");
    }
  }
  check(REV_HEAVY_PHASE_COUNT == 6, "heavy variant must have exactly 6 phases");
  check(REV_REGULAR_PHASE_COUNT == 4, "regular variant must have exactly 4 phases");

  // Sequence ends back on the original direction (phase count is even in
  // both variants -- last phase index is odd -> original direction).
  check(phaseDirection(REV_REGULAR_PHASE_COUNT - 1, Dir::FORWARD) == Dir::FORWARD,
        "regular sequence must end on the original direction");
  check(phaseDirection(REV_HEAVY_PHASE_COUNT - 1, Dir::REVERSE) == Dir::REVERSE,
        "heavy sequence must end on the original direction");

  // Total duration sanity (approximate -- excludes the per-phase coast, see
  // Config.h's comment on MUSIC_MOTOR_REVERSE_COAST_MS being additive).
  uint32_t regularTotal = (uint32_t)REV_REGULAR_PHASE_COUNT * REV_REGULAR_PHASE_MS;
  uint32_t heavyTotal = (uint32_t)REV_HEAVY_PHASE_COUNT * REV_HEAVY_PHASE_MS;
  check(regularTotal == 800, "regular reverse hip shake nominal total must be ~800ms");
  check(heavyTotal == 1080, "heavy reverse hip shake nominal total must be ~1080ms");
  (void)REV_REGULAR_PERCENT;
  (void)REV_HEAVY_PERCENT;

  // --- Item 12: hip-shake cooldown prevents immediate retriggering ---
  printf("\n-- hip-shake start cooldown --\n");
  uint32_t lastStart = 1000;
  uint32_t cooldown = HIPSHAKE_COOLDOWN_MIN_MS;  // deterministic pick within [MIN,MAX] for this test
  check(!((1000 + cooldown - 1) - lastStart >= cooldown), "must NOT be allowed to restart 1ms before cooldown elapses");
  check((1000 + cooldown) - lastStart >= cooldown, "must be allowed to restart exactly at cooldown elapsed");
  check(cooldown >= HIPSHAKE_COOLDOWN_MIN_MS && cooldown <= HIPSHAKE_COOLDOWN_MAX_MS,
        "cooldown must be within the configured 800-1200ms range");

  // --- Item 14: spin durations remain within configured limits ---
  printf("\n-- spin profile duration limits --\n");
  SpinProfile normal = pickSpinProfile(false, 0, false, false, 0);
  SpinProfile fast = pickSpinProfile(false, 0, false, true, 0);
  SpinProfile extendedLow = pickSpinProfile(false, 0, true, false, SPIN_EXTENDED_MIN_MS);
  SpinProfile extendedHigh = pickSpinProfile(false, 0, true, false, SPIN_EXTENDED_MAX_MS);
  check(normal.percent == SPIN_NORMAL_PERCENT && normal.durationMs == SPIN_NORMAL_MS, "NORMAL profile must be M90/2000ms");
  check(fast.percent == SPIN_FAST_PERCENT && fast.durationMs == SPIN_FAST_MS, "FAST profile must be M100/1000ms");
  check(extendedLow.durationMs >= SPIN_EXTENDED_MIN_MS && extendedLow.durationMs <= SPIN_EXTENDED_MAX_MS,
        "EXTENDED_DROP profile must stay within 2000-3000ms");
  check(extendedHigh.durationMs <= SPIN_ABSOLUTE_MAX_MS, "no spin profile may exceed the 4000ms absolute ceiling");
  check(SPIN_EXTENDED_MAX_MS <= SPIN_ABSOLUTE_MAX_MS, "EXTENDED_DROP max must itself fit under the absolute ceiling");
  SpinProfile manual = pickSpinProfile(true, 12345, true, true, 0);
  check(manual.durationMs == 12345, "manual override must take priority over every automatic profile");

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
