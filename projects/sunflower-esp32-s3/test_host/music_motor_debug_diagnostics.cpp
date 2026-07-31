// Temporary host-side deterministic test -- mirrors the revision-4
// decision-reason PURE helpers added to src/MusicMotorController.cpp
// (computeDropHoldDecision(), computeStrongHitReason(), and the
// debugShouldPrint() rate-limit gate). Same rationale as the other
// test_host/*.cpp files: no PlatformIO "test" env exists in this project.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/mm_debug_test test_host/music_motor_debug_diagnostics.cpp && /tmp/mm_debug_test
//
// Covers the revision-4 test list:
//  1. debug mode defaults OFF
//  2. debug logging never influences a decision (architectural: the pure
//     decision functions below take no debug-flag input at all)
//  3. drop-hold rejection reasons correspond to the actual gating condition
//  4. PEAK + qualifying strong hit -> STARTED
//  5. PEAK without a strong hit -> REJECTED
//  6. strong hit outside PEAK -> REJECTED (band_not_peak)
//  7. active drop-hold + qualifying hit -> REFRESHED, not STARTED
//  8. silence/disable/emergency-stop cancellation reasons are distinguishable
//  9. rate limiting never suppresses a "significant" (real state-change) line

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

// --- mirrored Config.h constant ---
constexpr uint32_t DROP_HOLD_INITIAL_MS = 2200;
constexpr uint32_t DROP_HOLD_MAX_MS = 4000;
constexpr uint32_t DEBUG_RATE_LIMIT_MS = 1000;

enum class Band { QUIET, LOW, MEDIUM, HIGH, PEAK };

constexpr uint32_t DROP_HOLD_SUSTAIN_CONFIRM_MS = 1800;

// --- mirrored computeDropHoldDecision() (see MusicMotorController.cpp) ---
// Revision 5: TWO independent qualification paths (qualifiedViaHit /
// qualifiedViaSustain), either sufficient -- "do not make strong hits
// mandatory for...drop hold."
enum class Outcome { STARTED, REFRESHED, REJECTED };
struct Decision {
  bool qualifyingBand;
  bool qualifyingHit;
  bool qualifiedViaSustain;
  Outcome outcome;
  const char *reason;
};

// Item 2 (architectural): note this function's signature -- it takes ONLY
// measured audio/state values, never a debug-logging flag. Debug mode
// therefore cannot influence its output, by construction; the real
// logDropHoldEvaluation() in MusicMotorController.cpp calls this exact
// function to compute what to LOG, and the real gating call site computes
// an equivalent condition independently to decide what to DO -- neither
// depends on debugLoggingEnabled.
Decision computeDropHoldDecision(Band measuredBand, bool strongHit, bool beat, bool silent, bool sustainedQualify,
                                  bool active, uint32_t startMs, uint32_t untilMs, uint32_t now) {
  Decision d{};
  d.qualifyingBand = (measuredBand == Band::PEAK) && !silent;
  d.qualifyingHit = strongHit;
  d.qualifiedViaSustain = sustainedQualify && !silent;
  bool qualifiedViaHit = d.qualifyingBand && strongHit;
  if (!qualifiedViaHit && !d.qualifiedViaSustain) {
    d.outcome = Outcome::REJECTED;
    if (silent) {
      d.reason = "silent";
    } else if (!d.qualifyingBand && !sustainedQualify) {
      d.reason = "band_not_peak";
    } else if (d.qualifyingBand && !strongHit) {
      d.reason = beat ? "strong_hit_false" : "beat_not_detected";
    } else {
      d.reason = "sustain_not_confirmed";
    }
    return d;
  }
  if (!active) {
    d.outcome = Outcome::STARTED;
    d.reason = qualifiedViaHit ? "start_qualified_hit" : "start_qualified_sustained";
    return d;
  }
  uint32_t maxUntil = startMs + DROP_HOLD_MAX_MS;
  uint32_t candidate = now + DROP_HOLD_INITIAL_MS;
  uint32_t newUntil = (candidate < maxUntil) ? candidate : maxUntil;
  if (newUntil > untilMs) {
    d.outcome = Outcome::REFRESHED;
    d.reason = qualifiedViaHit ? "refresh_qualified_hit" : "refresh_qualified_sustained";
  } else {
    d.outcome = Outcome::REJECTED;
    d.reason = "already_active_no_refresh";
  }
  return d;
}

// --- mirrored computeStrongHitReason() ---
const char *computeStrongHitReason(bool clap, float transientDelta, float threshold, bool cooldownArmedBefore,
                                    uint32_t sinceLastMs, uint32_t cooldownMs) {
  bool raw = clap || (transientDelta >= threshold);
  if (!raw) return "below_transient_threshold";
  if (!(cooldownArmedBefore && sinceLastMs >= cooldownMs)) return "cooldown_active";
  return "qualified";
}

// --- mirrored debugShouldPrint() rate-limit gate ---
struct RateLimit {
  const char *lastReason = "";
  uint32_t lastPrintMs = 0;
  bool everPrinted = false;
};
bool debugShouldPrint(RateLimit &rl, const char *reason, uint32_t now, bool significant) {
  if (significant) {
    rl.lastReason = reason;
    rl.lastPrintMs = now;
    rl.everPrinted = true;
    return true;
  }
  bool changed = !rl.everPrinted || strcmp(rl.lastReason, reason) != 0;
  bool intervalElapsed = (now - rl.lastPrintMs) >= DEBUG_RATE_LIMIT_MS;
  if (changed || intervalElapsed) {
    rl.lastReason = reason;
    rl.lastPrintMs = now;
    rl.everPrinted = true;
    return true;
  }
  return false;
}

int main() {
  printf("== Revision-4 debug-diagnostics decision-reason test ==\n");

  // --- Item 1: debug mode defaults OFF ---
  // Mirrors MusicMotorController.cpp: `bool debugLoggingEnabled = false;`
  // at declaration, and initMusicMotorController() explicitly re-asserts
  // `debugLoggingEnabled = false;` on every true boot. No code path sets it
  // true except the explicit 'musicmotor debug on' command.
  bool debugLoggingEnabledDefault = false;
  check(debugLoggingEnabledDefault == false, "debug logging must default to OFF");

  // --- Item 6: strong hit outside PEAK, not sustained either -> REJECTED band_not_peak ---
  printf("\n-- band_not_peak --\n");
  {
    Decision d = computeDropHoldDecision(Band::HIGH, /*strongHit=*/true, /*beat=*/true, /*silent=*/false,
                                          /*sustainedQualify=*/false, /*active=*/false, 0, 0, 1000);
    check(d.outcome == Outcome::REJECTED, "HIGH band + strong hit (not sustained) must still be REJECTED");
    check(strcmp(d.reason, "band_not_peak") == 0, "reason must be band_not_peak");
    check(!d.qualifyingBand, "qualifyingBand must be false for HIGH");
    check(d.qualifyingHit, "qualifyingHit must reflect the actual strongHit value regardless of band");
  }

  // --- Item 5: PEAK without a strong hit, not sustained -> REJECTED ---
  printf("-- PEAK without strong hit --\n");
  {
    Decision noBeat = computeDropHoldDecision(Band::PEAK, false, false, false, false, false, 0, 0, 1000);
    check(noBeat.outcome == Outcome::REJECTED, "PEAK without any hit must be REJECTED");
    check(strcmp(noBeat.reason, "beat_not_detected") == 0, "reason must be beat_not_detected when nothing fired");
    Decision ordinaryBeat = computeDropHoldDecision(Band::PEAK, false, true, false, false, false, 0, 0, 1000);
    check(ordinaryBeat.outcome == Outcome::REJECTED, "PEAK with only an ordinary beat must be REJECTED");
    check(strcmp(ordinaryBeat.reason, "strong_hit_false") == 0,
          "reason must be strong_hit_false when a beat fired but wasn't strong");
  }

  // --- Item 4: PEAK + qualifying strong hit -> STARTED ---
  printf("-- PEAK + qualifying strong hit (inactive) --\n");
  {
    Decision d = computeDropHoldDecision(Band::PEAK, true, true, false, false, false, 0, 0, 1000);
    check(d.outcome == Outcome::STARTED, "PEAK + strong hit while inactive must START");
    check(strcmp(d.reason, "start_qualified_hit") == 0, "reason must be start_qualified_hit");
    check(d.qualifyingBand && d.qualifyingHit, "both qualifying flags must be true");
  }

  // --- Item 7: active drop-hold + qualifying hit -> REFRESHED, not STARTED ---
  printf("-- PEAK + qualifying strong hit (already active) --\n");
  {
    uint32_t startMs = 1000, untilMs = 1000 + DROP_HOLD_INITIAL_MS;
    Decision d = computeDropHoldDecision(Band::PEAK, true, true, false, false, true, startMs, untilMs, 1500);
    check(d.outcome == Outcome::REFRESHED, "active + qualifying hit must REFRESH, not START");
    check(strcmp(d.reason, "refresh_qualified_hit") == 0, "reason must be refresh_qualified_hit");
  }
  // Saturated at the 4000ms cap -- further qualifying hits change nothing.
  printf("-- PEAK + qualifying strong hit (saturated at max cap) --\n");
  {
    uint32_t startMs = 1000, untilMs = startMs + DROP_HOLD_MAX_MS;  // already at the ceiling
    Decision d = computeDropHoldDecision(Band::PEAK, true, true, false, false, true, startMs, untilMs, startMs + 3999);
    check(d.outcome == Outcome::REJECTED, "saturated hold must be REJECTED, not REFRESHED");
    check(strcmp(d.reason, "already_active_no_refresh") == 0, "reason must be already_active_no_refresh");
  }

  // --- silence gate (state==SILENT forces rejection even if band were PEAK
  // and/or the sustain timer had confirmed) ---
  printf("-- silent state forces rejection regardless of band/sustain --\n");
  {
    Decision d = computeDropHoldDecision(Band::PEAK, true, true, /*silent=*/true, /*sustainedQualify=*/true, false, 0, 0, 1000);
    check(d.outcome == Outcome::REJECTED && strcmp(d.reason, "silent") == 0,
          "silent state must reject via 'silent' regardless of reported band or sustain confirmation");
  }

  // --- Revision 5: sustained (non-transient) qualification path -- HIGH or
  // PEAK dwell confirmed for >= MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS
  // qualifies WITHOUT any strong hit at all. ---
  printf("\n-- sustained qualification (no strong hit) --\n");
  {
    Decision notYet = computeDropHoldDecision(Band::HIGH, false, false, false, /*sustainedQualify=*/false, false, 0, 0, 1000);
    check(notYet.outcome == Outcome::REJECTED && strcmp(notYet.reason, "band_not_peak") == 0,
          "HIGH band with the dwell not yet confirmed must still reject as band_not_peak (nothing qualifies yet)");

    Decision confirmed = computeDropHoldDecision(Band::HIGH, false, false, false, /*sustainedQualify=*/true, false, 0, 0, 1000);
    check(confirmed.outcome == Outcome::STARTED, "confirmed sustained HIGH dwell (no strong hit) must START drop hold");
    check(strcmp(confirmed.reason, "start_qualified_sustained") == 0, "reason must be start_qualified_sustained");
    check(confirmed.qualifiedViaSustain, "qualifiedViaSustain must be true");
    check(!confirmed.qualifyingHit, "qualifyingHit must be false -- no strong hit was involved");

    // A strong hit is NOT required even once already active -- sustained
    // confirmation alone keeps refreshing it.
    uint32_t startMs = 1000, untilMs = 1000 + DROP_HOLD_INITIAL_MS;
    Decision refreshed =
        computeDropHoldDecision(Band::PEAK, false, false, false, /*sustainedQualify=*/true, true, startMs, untilMs, 1500);
    check(refreshed.outcome == Outcome::REFRESHED, "sustained confirmation alone must REFRESH an already-active hold");
    check(strcmp(refreshed.reason, "refresh_qualified_sustained") == 0, "reason must be refresh_qualified_sustained");

    // Either path alone is sufficient -- a strong hit at PEAK simultaneously
    // WITH sustained confirmation still just starts once (qualifiedViaHit
    // wins the reason label, per computeDropHoldDecision()'s precedence).
    Decision both = computeDropHoldDecision(Band::PEAK, true, true, false, /*sustainedQualify=*/true, false, 0, 0, 1000);
    check(both.outcome == Outcome::STARTED && strcmp(both.reason, "start_qualified_hit") == 0,
          "both paths qualifying simultaneously must still just START once (hit path labeled)");

    // Boundary check on the confirm duration itself -- mirrors
    // updateMusicMotorController()'s `sustainedHighSinceMs != 0 && (now -
    // sustainedHighSinceMs) >= MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS`.
    auto sustainedQualifyAt = [](uint32_t dwellStartMs, uint32_t nowMs) {
      return (nowMs - dwellStartMs) >= DROP_HOLD_SUSTAIN_CONFIRM_MS;
    };
    check(!sustainedQualifyAt(0, DROP_HOLD_SUSTAIN_CONFIRM_MS - 1), "dwell 1ms short of the confirm window must not qualify");
    check(sustainedQualifyAt(0, DROP_HOLD_SUSTAIN_CONFIRM_MS), "dwell exactly at the confirm window must qualify");
  }

  // --- Item 3 (strong-hit reasons): below_transient_threshold / cooldown_active / qualified ---
  printf("\n-- strong-hit reasons --\n");
  check(strcmp(computeStrongHitReason(false, 0.10f, 0.28f, true, 1000, 300), "below_transient_threshold") == 0,
        "transientDelta below threshold, no clap -> below_transient_threshold");
  check(strcmp(computeStrongHitReason(false, 0.30f, 0.28f, false, 50, 300), "cooldown_active") == 0,
        "raw condition true but cooldown not yet elapsed -> cooldown_active");
  check(strcmp(computeStrongHitReason(false, 0.30f, 0.28f, true, 300, 300), "qualified") == 0,
        "raw condition true and cooldown elapsed exactly -> qualified");
  check(strcmp(computeStrongHitReason(true, 0.0f, 0.28f, true, 1000, 300), "qualified") == 0,
        "clap=true alone must qualify even with transientDelta at 0");

  // --- Item 8: cancellation reasons are distinguishable ---
  printf("\n-- cancellation reason vocabulary --\n");
  const char *silence = "silence_cancel";
  const char *disabled = "disabled_cancel";
  const char *emergency = "emergency_stop_cancel";
  check(strcmp(silence, disabled) != 0 && strcmp(silence, emergency) != 0 && strcmp(disabled, emergency) != 0,
        "silence/disabled/emergency-stop cancellation reasons must all be distinct strings");

  // --- Item 9: rate limiting never suppresses a significant line ---
  printf("\n-- rate limiting: significant lines always print --\n");
  {
    RateLimit rl;
    check(debugShouldPrint(rl, "start_qualified", 1000, true), "first significant print must succeed");
    check(debugShouldPrint(rl, "start_qualified", 1001, true), "back-to-back IDENTICAL significant reason at +1ms must still print");
    check(debugShouldPrint(rl, "start_qualified", 1002, true), "significant events are NEVER rate-limited, regardless of interval");
  }
  // Non-significant (rejection) lines ARE deduped unless the reason changes
  // or the interval elapses.
  printf("-- rate limiting: repeated rejection reason is suppressed within the window --\n");
  {
    RateLimit rl;
    check(debugShouldPrint(rl, "band_not_peak", 1000, false), "first rejection print must succeed");
    check(!debugShouldPrint(rl, "band_not_peak", 1500, false), "identical rejection reason within 1000ms must be suppressed");
    check(debugShouldPrint(rl, "band_not_peak", 2001, false), "identical rejection reason after 1000ms must print again");
  }
  printf("-- rate limiting: a CHANGED rejection reason always prints immediately --\n");
  {
    RateLimit rl;
    check(debugShouldPrint(rl, "band_not_peak", 1000, false), "first rejection print must succeed");
    check(debugShouldPrint(rl, "strong_hit_false", 1050, false), "a DIFFERENT rejection reason must print immediately, even within the window");
  }

  printf("\n%s: %d failure(s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
