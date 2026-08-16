#pragma once
#include <stdint.h>
#include <stdbool.h>

// UI STATE layer -- a small, deliberately hardware-free data model of
// what's currently true about the UI. Phase 1 only has one screen (the
// bring-up/tap-test screen), so this is intentionally minimal; it exists
// now so later phases (Phase 10.3 onward -- Sunny UI foundation) have an
// established, host-tested pattern to extend rather than inventing state
// management ad-hoc inside widget callbacks.
//
// Pure logic only -- no LVGL types, no hardware calls -- so this file and
// its .cpp are fully host-testable (see test_host/ui_state_model.cpp).

enum class SunnyUIScreen : uint8_t {
  BRING_UP_TAP_TEST,  // V1.2.1 standalone bring-up screen -- kept for reference/regression
  CALIBRATION,        // Touch calibration flow (see CalibrationManager.h) -- available as a
                      // diagnostic/manual mode for recalibration; NOT the default boot screen
                      // as of V1.2.2 (see TOUCH_VALIDATION below)
  TOUCH_VALIDATION,   // V1.2.2 default boot screen -- static targets + TAP TEST, validates the
                      // now-applied final measured calibration (see Screens.cpp)
  // The remaining values are NAVIGATION INFRASTRUCTURE ONLY (V1.2.2) --
  // Screens.cpp's dispatcher routes them to a generic placeholder screen.
  // Full implementations are a later V1.2.3+ sprint; see
  // docs/DISPLAY_HARDWARE.md's "Later Sunny UI architecture" section for
  // what each will eventually show. Do not add screen-specific logic
  // here speculatively -- isScreenImplemented() below is the single
  // source of truth for what's real vs. a placeholder.
  HOME,
  AUDIO,
  MOTION,
  LEDS,
  DIAGNOSTICS,
  SETTINGS,
};

// True only for screens with a real implementation (see Screens.cpp's
// showScreen() dispatcher, which routes everything else to a generic
// placeholder). Update this alongside adding a screen's real builder --
// never mark a screen implemented before it has one.
bool isScreenImplemented(SunnyUIScreen screen);

struct BringUpTapTestState {
  uint32_t tapCount = 0;
  bool buttonCurrentlyPressed = false;  // drives the button's visual "pressed" state
};

// Pure state-transition functions -- no side effects beyond the struct
// itself, so they're trivially host-testable and reusable from both the
// real touch callback and a host test.

// Call when a press begins (touch state transitions released -> pressed
// while over the button). Increments tapCount exactly once per press.
void bringUpTapTestOnPressStart(BringUpTapTestState *state);

// Call when a press ends (touch state transitions pressed -> released).
void bringUpTapTestOnPressEnd(BringUpTapTestState *state);
