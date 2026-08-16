#pragma once

// Stateful touch-calibration screen driver -- owns the calibration
// screen's LVGL widgets (title, instruction/progress labels, moving
// target dot) and the per-tick sampling state machine that walks through
// CALIBRATION_TARGET_COUNT targets, collects raw touch samples while
// each is held, and derives proposed transform constants once all
// targets are done. Uses only Calibration.h's pure math (host-tested)
// and TouchManager's readRawTouchPointForCalibration() -- never touches
// DisplayManager/LGFX directly, matching this project's layering.
//
// Sits in the UI SCREENS layer alongside Screens.h/.cpp, but broken into
// its own file/module (like DisplayManager/TouchManager) because its
// per-tick sampling logic is more than a simple widget+callback screen.
//
// This module does NOT self-apply the derived constants -- it only
// prints them to serial in a copy-pasteable form and shows a summary on
// screen. Per V1.2.2's explicit instruction, proposed constants are
// reviewed by a human before being pasted into TouchManager.cpp's real
// CAL_* constants -- this build never overwrites them on its own.

// Builds the calibration screen's static chrome and shows the first
// target. Call once, after initDisplayManager()/initTouchManager() have
// both succeeded.
void showCalibrationScreen();

// Call every loop() tick while the calibration screen is active. Polls
// raw touch state, accumulates samples for the current target while
// pressed, and finalizes/advances on release. No-op once calibration is
// complete.
void updateCalibrationScreen();

bool isCalibrationComplete();
