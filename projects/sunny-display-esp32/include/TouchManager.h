#pragma once
#include <stdint.h>
#include <stdbool.h>

// Sole owner of touch CALIBRATION, the raw-to-screen coordinate
// TRANSFORM, and the LVGL input-device (indev) registration/read
// callback. Reads raw ADC touch data ONLY through DisplayManager's
// narrow readRawTouchPoint() accessor (see DisplayManager.h's comment on
// why the raw XPT2046 SPI object itself lives in that module, not here).
//
// Never invents calibration constants from a datasheet's theoretical ADC
// range -- resistive touch panels vary panel-to-panel and MUST be
// calibrated against the actual physical unit. See
// docs/DISPLAY_HARDWARE.md's "Touch calibration procedure" for how the
// current constants in TouchManager.cpp were physically measured and
// fitted (V1.2.2, 2026-08-09).

// Registers an LVGL pointer input device backed by readTouchScreenPoint()
// below. Call once, after initDisplayManager() has already succeeded.
void initTouchManager();

// Reads the current touch state, applies the calibration transform
// (raw ADC -> screen pixel coordinates, accounting for axis swap/
// inversion and the display's rotation), and reports it in SCREEN
// coordinates (0..LCD_HEIGHT-1 x 0..LCD_WIDTH-1 post-rotation). Returns
// false if no touch is currently detected. This is also the function
// wired into LVGL's indev read callback -- exposed here directly so
// non-LVGL code (e.g. a future calibration routine or host-testable
// transform logic) can call it too.
bool readTouchScreenPoint(int32_t *outScreenX, int32_t *outScreenY);

// Pure coordinate-transform function, factored out specifically so it's
// host-testable without any hardware -- see test_host/touch_calibration.cpp
// and test_host/final_touch_calibration.cpp. Applies the fitted per-axis
// linear (scale + offset) calibration -- see Calibration.h's
// DerivedCalibration for why this replaced the old raw-min/max model
// (V1.2.2, 2026-08-09: the old model produced ~30-42px corner error by
// assuming an inset target's raw reading was measured at the true screen
// edge). Clamps to screen bounds. Does not touch hardware.
void transformRawTouchToScreen(int32_t rawX, int32_t rawY, int32_t screenWidth, int32_t screenHeight, bool swapAxes,
                                float scaleX, float offsetX, float scaleY, float offsetY, int32_t *outScreenX,
                                int32_t *outScreenY);

bool isTouchManagerReady();
void printTouchManagerStatus();

// Narrow raw-ADC accessor for CalibrationManager only -- calibration
// needs UN-transformed touch samples (the transform's own constants are
// what it's trying to derive), so it cannot go through
// readTouchScreenPoint() above. This keeps DisplayManager's underlying
// readRawTouchPoint() accessor single-consumer (TouchManager.cpp is
// still its only caller); CalibrationManager depends only on
// TouchManager, matching this project's layering.
bool readRawTouchPointForCalibration(int32_t *outRawX, int32_t *outRawY);
