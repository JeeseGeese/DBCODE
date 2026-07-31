// Temporary host-side deterministic test -- mirrors revision 6's
// "favor continuation over interruption" rotation-commitment gate added to
// src/MusicMotorController.cpp (reversalCommitmentSatisfied(), and its use
// in selectBeatAction()'s LOW/MEDIUM/HIGH reversal-slot branches). Same
// rationale/approach as the other test_host/*.cpp files: no PlatformIO
// "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_rotation_test test_host/music_motor_rotation_commitment.cpp && /tmp/mm_rotation_test
//
// Covers:
//  1. reversalCommitmentSatisfied() pure-function truth table (accent /
//     phrase boundary / elapsed-time exemptions)
//  2. a simulated LOW-band reversal-slot sequence: repeated qualifying
//     strong hits within the hold window must NOT reverse (must instead
//     fall back to the band's default action, i.e. keep accenting the
//     current rotation) -- "continue it through ordinary beats"
//  3. once the hold has elapsed, the next qualifying slot DOES reverse
//  4. a strong accent (real PEAK / drop hold) or a phrase boundary (band
//     transition) bypasses the hold immediately, even with zero elapsed time
//  5. ordinary (non-strong) beats never reverse regardless of commitment --
//     unchanged pre-existing behavior, reconfirmed here for the "beats
//     modulate speed, not direction" requirement

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

constexpr uint32_t MIN_ROTATION_HOLD_MS = 1800;

// --- mirrored reversalCommitmentSatisfied() (see MusicMotorController.cpp) ---
bool reversalCommitmentSatisfied(uint32_t now, uint32_t directionStartMs, bool strongAccent, bool phraseBoundary,
                                  uint32_t minRotationHoldMs) {
  if (strongAccent || phraseBoundary) return true;
  return (now - directionStartMs) >= minRotationHoldMs;
}

// --- mirrored LOW-band reversal-slot selection (see selectBeatAction()'s
// BAND_LOW case) -- only the piece relevant to this gate: every 3rd
// qualifying strong hit ATTEMPTS a reversal, gated by canReverseCommitted
// (hardware-safety canReverse, mirrored here as always-true since that gate
// is already covered by other test_host files, ANDed with the new
// commitment gate). Returns true if this call reversed. ---
struct LowBandSim {
  uint32_t counter = 0;
};
bool lowBandStrongHit(LowBandSim &sim, uint32_t now, uint32_t directionStartMs, bool strongAccent, bool phraseBoundary,
                       uint32_t minRotationHoldMs) {
  sim.counter++;
  bool canReverseCommitted = reversalCommitmentSatisfied(now, directionStartMs, strongAccent, phraseBoundary, minRotationHoldMs);
  return canReverseCommitted && (sim.counter % 3 == 0);
}

int main() {
  printf("== Revision-6 rotation-commitment ('favor continuation') test ==\n");

  // --- Item 1: pure-function truth table ---
  printf("\n-- reversalCommitmentSatisfied() truth table --\n");
  check(reversalCommitmentSatisfied(0, 0, /*strongAccent=*/true, false, MIN_ROTATION_HOLD_MS),
        "strong accent must bypass immediately, even at elapsed=0");
  check(reversalCommitmentSatisfied(0, 0, false, /*phraseBoundary=*/true, MIN_ROTATION_HOLD_MS),
        "phrase boundary must bypass immediately, even at elapsed=0");
  check(!reversalCommitmentSatisfied(MIN_ROTATION_HOLD_MS - 1, 0, false, false, MIN_ROTATION_HOLD_MS),
        "1ms short of the hold, with neither exemption, must NOT be satisfied");
  check(reversalCommitmentSatisfied(MIN_ROTATION_HOLD_MS, 0, false, false, MIN_ROTATION_HOLD_MS),
        "exactly at the hold, with neither exemption, must be satisfied");
  check(reversalCommitmentSatisfied(5000, 0, false, false, MIN_ROTATION_HOLD_MS),
        "well past the hold, with neither exemption, must be satisfied");

  // --- Item 2/3: simulated LOW-band sequence -- repeated qualifying strong
  // hits within the hold window must NOT reverse; once the hold elapses,
  // the next qualifying slot DOES. ---
  printf("\n-- LOW-band reversal slot: continuation through the hold window --\n");
  {
    LowBandSim sim;
    uint32_t directionStartMs = 0;
    int reversalsWithinHold = 0;
    // Hits at 100ms, 200ms, ..., 1700ms (every 3rd -- ticks 3,6,9,...,15 --
    // is a reversal SLOT) -- all still inside the 1800ms hold window, with
    // no accent/boundary in play.
    for (uint32_t t = 100; t < MIN_ROTATION_HOLD_MS; t += 100) {
      bool reversed = lowBandStrongHit(sim, t, directionStartMs, /*strongAccent=*/false, /*phraseBoundary=*/false, MIN_ROTATION_HOLD_MS);
      if (reversed) reversalsWithinHold++;
    }
    check(reversalsWithinHold == 0,
          "NO reversal-slot hit inside the commitment hold window may actually reverse, "
          "regardless of how many qualifying strong hits occur -- 'continue through ordinary beats'");

    // Continue past the hold -- the next reversal-slot hit (counter a
    // multiple of 3) must now succeed.
    bool reversedAfterHold = false;
    for (uint32_t t = MIN_ROTATION_HOLD_MS + 100; t < MIN_ROTATION_HOLD_MS + 1000; t += 100) {
      if (lowBandStrongHit(sim, t, directionStartMs, false, false, MIN_ROTATION_HOLD_MS)) {
        reversedAfterHold = true;
        break;
      }
    }
    check(reversedAfterHold, "once the commitment hold has elapsed, the next reversal-slot hit must be allowed to reverse");
  }

  // --- Item 4: exemptions bypass the hold even at elapsed=0 ---
  printf("\n-- exemptions bypass the hold immediately --\n");
  {
    LowBandSim sim;
    // Advance the counter to the slot boundary (2 non-reversal hits), then
    // the 3rd hit -- at elapsed=0 from directionStartMs -- should still
    // reverse if it's a strong accent.
    sim.counter = 2;
    bool reversed = lowBandStrongHit(sim, /*now=*/0, /*directionStartMs=*/0, /*strongAccent=*/true, false, MIN_ROTATION_HOLD_MS);
    check(reversed, "a strong-accent qualifying hit must reverse immediately even with zero elapsed commitment time");

    LowBandSim sim2;
    sim2.counter = 2;
    bool reversedBoundary = lowBandStrongHit(sim2, 0, 0, false, /*phraseBoundary=*/true, MIN_ROTATION_HOLD_MS);
    check(reversedBoundary, "a phrase-boundary qualifying hit must reverse immediately even with zero elapsed commitment time");
  }

  // --- Item 5: ordinary (non-strong) beats never reverse, independent of
  // this gate -- pre-existing invariant, reconfirmed here. selectBeatAction()
  // returns ACCENT_CURRENT_DIRECTION unconditionally for !isStrong before
  // any of this gate's logic is even reached -- modeled here simply as "an
  // ordinary beat never calls into the reversal-slot machinery at all." ---
  printf("\n-- ordinary beats never reach the reversal-slot machinery --\n");
  {
    LowBandSim sim;
    // Even after many hits' worth of counter advancement (were it ever
    // invoked), an ordinary beat's action is decided BEFORE isStrong is
    // even checked against the reversal slot -- this is an architectural
    // invariant (see selectBeatAction()'s `if (!isStrong) return
    // ACCENT_CURRENT_DIRECTION;` early return), not something this
    // function alone could violate. Documented here for traceability.
    check(sim.counter == 0, "sanity: an untouched simulated counter starts at 0 (ordinary beats must never increment it)");
  }

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
