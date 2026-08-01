// Host-side regression coverage for the unified "Audio Mode" architecture
// (Button4 long-hold -> setUserAudioModeEnabled(), src/Controls.cpp) added
// when MusicMotorController Revision 10.1 became Sunny's sole production
// music-driven dancing engine and DanceEngine was gated off by default
// (ENABLE_LEGACY_DANCE_ENGINE=0, include/DanceEngine.h). Same
// standalone-host-test approach as every other test_host/*.cpp file: no
// PlatformIO "test" env exists in this project, no Arduino dependency --
// the Button4 debounce/long-press state machine and the Audio Mode
// coordination logic are mirrored inline below as faithful, simplified
// reproductions of src/Controls.cpp's handleButton4()/
// handleButton4LongPress()/setUserAudioModeEnabled()/
// isUserAudioModeEnabled(). Debounce noise-filtering itself is unchanged
// by this task and not re-tested here -- tests drive already-debounced
// press()/release() edges, matching how handleButton4() consumes its own
// debounced stableState.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/audio_mode_test test_host/audio_mode_button4_integration.cpp && /tmp/audio_mode_test
//
// Covers the 22-item list from the Audio Mode integration task:
//  1. Audio Mode defaults OFF
//  2. MusicMotor defaults OFF
//  3. DanceEngine unavailable in default production build
//  4. Button4 short press triggers its bound action
//  5. Button4 long hold does not trigger short press on release
//  6. First valid long hold enables Audio Mode
//  7. Audio Mode ON enables LED overlay
//  8. Audio Mode ON enables MusicMotorController
//  9. Audio Mode ON does not enable DanceEngine
//  10. Continuous holding toggles only once
//  11. Second valid long hold disables Audio Mode
//  12. Audio Mode OFF disables LED overlay
//  13. Audio Mode OFF disables MusicMotorController
//  14. Audio Mode OFF invokes safe motor shutdown
//  15. MusicMotor shutdown clears pending movement state
//  16. Conflicting motor ownership rejects Audio Mode enable
//  17. Rejected enable leaves no partial state
//  18. Serial disable updates unified state accurately
//  19. Standalone diagnostic states reported as partial, not full Audio Mode
//  20/21. Existing MusicMotorController Rev 10.1 / target-speed invariant
//         tests still pass -- NOT re-tested here; validated by re-running
//         the full test_host/*.cpp suite (see AGENTS.md section 9). This
//         file adds no changes to MusicMotorController's own internals.
//  22. Boot initialization leaves motor output stopped
// Plus one bonus case found while implementing the ownership check:
//  23. Audio Mode enable completes (does not reject) when
//      MusicMotorController is already the sole active owner (e.g. started
//      standalone via 'musicmotor on') -- only a DIFFERENT owner rejects.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// ----------------------------------------------------------------------------
// Mirrors src/Controls.cpp's handleButton4() debounce-consuming edge/
// long-press logic and include/Config.h's BUTTON4_LONG_PRESS_MS.
// ----------------------------------------------------------------------------
constexpr uint32_t BUTTON4_LONG_PRESS_MS = 900;

struct Button4Sim {
  bool stableState = true;  // true == HIGH/released, mirrors digitalRead() idle-high with INPUT_PULLUP
  uint32_t pressStartMs = 0;
  bool longPressFired = false;
  int shortPressActionCount = 0;
  int longPressActionCount = 0;

  // Mirrors the "if (pressedEdge) { pressStartMs = now; longPressFired = false; }" block.
  void press(uint32_t now) {
    stableState = false;
    pressStartMs = now;
    longPressFired = false;
  }

  // Mirrors the long-press-threshold-crossing check, called every loop() tick.
  void tick(uint32_t now) {
    if (!stableState && !longPressFired && (now - pressStartMs) >= BUTTON4_LONG_PRESS_MS) {
      longPressFired = true;
      longPressActionCount++;
    }
  }

  // Mirrors the releasedEdge block: short-press action only fires if no
  // long press completed during this hold.
  void release(uint32_t now) {
    (void)now;
    stableState = true;
    if (longPressFired) return;
    shortPressActionCount++;
  }
};

// ----------------------------------------------------------------------------
// Mirrors src/Controls.cpp's audioOverlayEnabled/isUserAudioModeEnabled()/
// setUserAudioModeEnabled() and main.cpp's isAnyMotorDiagnosticActive()/
// currentMotorOwnerName(). musicMotorActive/pendingMovement stand in for
// MusicMotorController's isMusicMotorControllerActive()/internal runtime
// state (see MusicMotorController.cpp's resetRuntimeState(), which the real
// musicMotorDisable() already calls via hardStop()).
// ----------------------------------------------------------------------------
struct AudioModeSim {
  bool overlayEnabled = false;
  bool musicMotorActive = false;
  bool danceEngineActive = false;  // ENABLE_LEGACY_DANCE_ENGINE=0 -> DanceEngine can never become active
  bool pendingMovement = false;    // stand-in for MusicMotorController's stale drop-phrase/ramp state
  bool otherDiagnosticActive = false;
  const char *otherDiagnosticName = nullptr;
  bool shutdownInvoked = false;

  bool isAnyMotorDiagnosticActive() const { return otherDiagnosticActive || danceEngineActive || musicMotorActive; }

  const char *currentMotorOwnerName() const {
    if (otherDiagnosticActive) return otherDiagnosticName ? otherDiagnosticName : "OtherDiagnostic";
    if (danceEngineActive) return "DanceEngine";
    if (musicMotorActive) return "MusicMotorController";
    return nullptr;
  }

  // Derived read -- never stored separately (matches Controls.cpp exactly).
  bool isUserAudioModeEnabled() const { return overlayEnabled && musicMotorActive; }

  // Mirrors Controls.cpp's setUserAudioModeEnabled() -- see that function
  // for the authoritative version and full rationale comments.
  bool setUserAudioModeEnabled(bool enabled) {
    if (!enabled) {
      overlayEnabled = false;
      musicMotorActive = false;
      pendingMovement = false;  // musicMotorDisable() -> hardStop() -> resetRuntimeState() clears this
      shutdownInvoked = true;
      return true;
    }

    if (isUserAudioModeEnabled()) return true;

    if (isAnyMotorDiagnosticActive() && !musicMotorActive) {
      return false;  // rejected -- caller (test) checks nothing changed
    }

    musicMotorActive = true;  // mirrors musicMotorEnable() succeeding
    overlayEnabled = true;
    return true;
  }

  // Mirrors 'status'/'audiomode status' PARTIAL/ON/OFF classification.
  std::string statusString() const {
    if (overlayEnabled && musicMotorActive) return "ON";
    if (!overlayEnabled && !musicMotorActive) return "OFF";
    return "PARTIAL";
  }
};

// --- 1. Audio Mode defaults OFF ---
static void test_01_audio_mode_defaults_off() {
  AudioModeSim sim;
  check(!sim.isUserAudioModeEnabled(), "fresh AudioModeSim defaults to Audio Mode OFF");
}

// --- 2. MusicMotor defaults OFF ---
static void test_02_musicmotor_defaults_off() {
  AudioModeSim sim;
  check(!sim.musicMotorActive, "fresh AudioModeSim defaults MusicMotor OFF");
}

// --- 3. DanceEngine unavailable in default production build ---
static void test_03_dance_engine_disabled_by_default() {
  AudioModeSim sim;
  // ENABLE_LEGACY_DANCE_ENGINE=0 -> nothing in the production path can ever
  // set danceEngineActive true; enabling Audio Mode must not change that.
  sim.setUserAudioModeEnabled(true);
  check(!sim.danceEngineActive, "DanceEngine stays inactive/unavailable through an Audio Mode enable");
}

// --- 4. Button4 short press triggers its bound action ---
static void test_04_short_press_triggers_bound_action() {
  Button4Sim btn;
  uint32_t now = 1000;
  btn.press(now);
  btn.tick(now);
  now += 150;  // well under BUTTON4_LONG_PRESS_MS
  btn.release(now);
  check(btn.shortPressActionCount == 1, "short press (release before threshold) fires the bound short-press action exactly once");
  check(btn.longPressActionCount == 0, "short press never fires the long-press action");
}

// --- 5. Button4 long hold does not trigger short press on release ---
static void test_05_long_hold_no_short_press_on_release() {
  Button4Sim btn;
  uint32_t now = 1000;
  btn.press(now);
  now += BUTTON4_LONG_PRESS_MS;
  btn.tick(now);
  now += 50;
  btn.release(now);
  check(btn.longPressActionCount == 1, "long hold fires the long-press action once");
  check(btn.shortPressActionCount == 0, "release after a completed long hold does not also fire the short-press action");
}

// --- 6/7/8/9. First valid long hold enables unified Audio Mode (LED overlay + MusicMotor, not DanceEngine) ---
static void test_06_07_08_09_first_long_hold_enables_audio_mode() {
  Button4Sim btn;
  AudioModeSim sim;
  uint32_t now = 1000;
  btn.press(now);
  now += BUTTON4_LONG_PRESS_MS;
  btn.tick(now);
  check(btn.longPressActionCount == 1, "long-press threshold crossed exactly once");
  // Mirrors handleButton4LongPress(): setUserAudioModeEnabled(!isUserAudioModeEnabled())
  bool result = sim.setUserAudioModeEnabled(!sim.isUserAudioModeEnabled());
  check(result, "first long hold's enable call succeeds");
  check(sim.isUserAudioModeEnabled(), "6. first valid long hold enables Audio Mode");
  check(sim.overlayEnabled, "7. Audio Mode ON enables the LED overlay");
  check(sim.musicMotorActive, "8. Audio Mode ON enables MusicMotorController");
  check(!sim.danceEngineActive, "9. Audio Mode ON does not enable DanceEngine");
}

// --- 10. Continuous holding toggles only once ---
static void test_10_continuous_hold_toggles_once() {
  Button4Sim btn;
  AudioModeSim sim;
  uint32_t now = 1000;
  btn.press(now);
  int toggleInvocations = 0;
  // Simulate many loop() ticks while the button remains held well past
  // the threshold -- longPressFired must latch so the toggle only ever
  // fires on the instant the threshold is first crossed.
  for (uint32_t t = now; t <= now + BUTTON4_LONG_PRESS_MS + 2000; t += 15) {
    bool firedBefore = btn.longPressFired;
    btn.tick(t);
    if (!firedBefore && btn.longPressFired) {
      sim.setUserAudioModeEnabled(!sim.isUserAudioModeEnabled());
      toggleInvocations++;
    }
  }
  check(toggleInvocations == 1, "10. continuous holding toggles Audio Mode exactly once, not repeatedly");
  check(sim.isUserAudioModeEnabled(), "Audio Mode ended up ON after the single toggle from a continuous hold");
}

// --- 11/12/13/14/15. Second valid long hold disables Audio Mode + safe shutdown ---
static void test_11_12_13_14_15_second_long_hold_disables_audio_mode() {
  Button4Sim btn;
  AudioModeSim sim;
  uint32_t now = 1000;

  // First hold: enable.
  btn.press(now);
  now += BUTTON4_LONG_PRESS_MS;
  btn.tick(now);
  sim.setUserAudioModeEnabled(!sim.isUserAudioModeEnabled());
  now += 50;
  btn.release(now);
  sim.pendingMovement = true;  // simulate a pending drop-phrase/ramp mid-flight

  // Second hold: disable.
  now += 500;
  btn.press(now);
  now += BUTTON4_LONG_PRESS_MS;
  btn.tick(now);
  bool result = sim.setUserAudioModeEnabled(!sim.isUserAudioModeEnabled());
  now += 50;
  btn.release(now);

  check(result, "second long hold's disable call succeeds");
  check(!sim.isUserAudioModeEnabled(), "11. second valid long hold disables Audio Mode");
  check(!sim.overlayEnabled, "12. Audio Mode OFF disables the LED overlay");
  check(!sim.musicMotorActive, "13. Audio Mode OFF disables MusicMotorController");
  check(sim.shutdownInvoked, "14. Audio Mode OFF invokes MusicMotor's safe shutdown path");
  check(!sim.pendingMovement, "15. MusicMotor shutdown clears pending movement state");
  check(btn.longPressActionCount == 2, "both long holds registered as exactly one long-press action each");
  check(btn.shortPressActionCount == 0, "neither long hold's release fired a short-press action");
}

// --- 16/17. Conflicting motor ownership rejects Audio Mode enable, no partial state left ---
static void test_16_17_conflicting_ownership_rejects_enable() {
  AudioModeSim sim;
  sim.otherDiagnosticActive = true;
  sim.otherDiagnosticName = "MotorPwmCalibration";

  bool result = sim.setUserAudioModeEnabled(true);

  check(!result, "16. Audio Mode enable is rejected while another diagnostic owns the motor");
  check(strcmp(sim.currentMotorOwnerName(), "MotorPwmCalibration") == 0, "rejection correctly names the blocking owner");
  check(!sim.overlayEnabled, "17. rejected enable leaves the LED overlay OFF (no partial state)");
  check(!sim.musicMotorActive, "17. rejected enable leaves MusicMotorController OFF (no partial state)");
  check(!sim.isUserAudioModeEnabled(), "17. rejected enable leaves Audio Mode OFF overall");
}

// --- 18. Serial disable updates unified state accurately ---
static void test_18_serial_disable_updates_unified_state() {
  AudioModeSim sim;
  sim.setUserAudioModeEnabled(true);
  check(sim.isUserAudioModeEnabled(), "Audio Mode starts fully ON for this test");

  // Simulate 'musicmotor off' issued directly over serial -- bypasses
  // setUserAudioModeEnabled() entirely, exactly like the real
  // dispatchMusicMotorCommand("off") -> musicMotorDisable() path.
  sim.musicMotorActive = false;

  check(!sim.isUserAudioModeEnabled(), "18. disabling MusicMotor via serial immediately invalidates the unified Audio Mode state");
  check(sim.overlayEnabled, "the LED overlay half is untouched by a MusicMotor-only serial disable");
}

// --- 19. Standalone diagnostic states reported as partial, not full Audio Mode ---
static void test_19_standalone_diagnostic_reported_as_partial() {
  AudioModeSim sim;
  // Simulate 'musicmotor on' issued directly over serial, LED overlay
  // never touched -- exactly the "standalone diagnostic state" Phase 9
  // describes.
  sim.musicMotorActive = true;

  check(sim.statusString() == "PARTIAL", "19. MusicMotor-only standalone state reports PARTIAL, not full Audio Mode ON");
  check(!sim.isUserAudioModeEnabled(), "a partial state must never satisfy isUserAudioModeEnabled()");

  AudioModeSim sim2;
  sim2.overlayEnabled = true;  // simulate serial 'x' alone
  check(sim2.statusString() == "PARTIAL", "LED-overlay-only standalone state also reports PARTIAL");
}

// --- 22. Boot initialization leaves motor output stopped ---
static void test_22_boot_leaves_motor_output_stopped() {
  AudioModeSim sim;  // fresh construction mirrors initControls()/initMusicMotorController() at boot
  check(!sim.musicMotorActive, "22. boot initialization leaves MusicMotorController inactive (motor output stopped)");
  check(!sim.overlayEnabled, "22. boot initialization leaves the LED overlay OFF");
  check(!sim.isUserAudioModeEnabled(), "22. boot initialization leaves unified Audio Mode OFF");
}

// --- 23. Bonus: enabling completes rather than rejects when MusicMotorController is the only active owner ---
static void test_23_completes_partial_state_instead_of_rejecting() {
  AudioModeSim sim;
  sim.musicMotorActive = true;  // e.g. a prior standalone 'musicmotor on', overlay still off

  bool result = sim.setUserAudioModeEnabled(true);

  check(result, "23. enabling Audio Mode succeeds when MusicMotorController already solely owns the motor");
  check(sim.overlayEnabled, "23. the LED overlay half is completed rather than the request being rejected");
  check(sim.isUserAudioModeEnabled(), "23. Audio Mode ends up fully ON");
}

int main() {
  test_01_audio_mode_defaults_off();
  test_02_musicmotor_defaults_off();
  test_03_dance_engine_disabled_by_default();
  test_04_short_press_triggers_bound_action();
  test_05_long_hold_no_short_press_on_release();
  test_06_07_08_09_first_long_hold_enables_audio_mode();
  test_10_continuous_hold_toggles_once();
  test_11_12_13_14_15_second_long_hold_disables_audio_mode();
  test_16_17_conflicting_ownership_rejects_enable();
  test_18_serial_disable_updates_unified_state();
  test_19_standalone_diagnostic_reported_as_partial();
  test_22_boot_leaves_motor_output_stopped();
  test_23_completes_partial_state_instead_of_rejecting();

  if (g_failures == 0) {
    printf("All audio_mode_button4_integration tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
