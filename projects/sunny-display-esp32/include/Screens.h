#pragma once

#include "UIState.h"

// UI SCREENS layer -- widget construction/navigation/visual presentation.
// Reads/writes UIState.h's data model; never touches DisplayManager/
// TouchManager hardware directly (it only uses LVGL's own widget API,
// which is already hardware-abstracted by the flush/indev callbacks
// those two modules registered).

// Builds and shows Phase 1's bring-up/tap-test screen (see
// docs/DISPLAY_HARDWARE.md's "First bring-up target" section for the
// exact requirements this satisfies). Kept for reference/regression --
// not the default boot screen as of V1.2.2. Call once, after
// initDisplayManager() and initTouchManager() have both succeeded.
void showBringUpTapTestScreen();

// Default boot screen as of V1.2.2 -- 5 static, tappable validation
// targets (reusing Calibration.h's target geometry) at the four corners
// and center, plus the retained TAP TEST button. Exercises the applied
// calibration constants through the normal LVGL indev path; does not
// sample/derive calibration data itself (see CalibrationManager.h for
// that -- CALIBRATION remains available as a diagnostic/manual mode for
// future recalibration, just not the default boot screen anymore).
void showTouchValidationScreen();

// Navigation dispatcher -- shows the given screen. BRING_UP_TAP_TEST and
// CALIBRATION have real implementations; every other SunnyUIScreen value
// (see UIState.h's isScreenImplemented()) routes to a generic
// placeholder ("<NAME> -- not yet implemented"). This is minimal
// navigation infrastructure for the next V1.2.3+ UI-design sprint to
// build on -- it deliberately does not implement HOME/AUDIO/MOTION/
// LEDS/DIAGNOSTICS/SETTINGS yet.
void showScreen(SunnyUIScreen screen);

// Call every loop() tick. Routes to whichever active screen has per-tick
// work (currently only CALIBRATION's sampling state machine; every other
// screen is a no-op here).
void updateActiveScreen();

SunnyUIScreen getCurrentScreen();
