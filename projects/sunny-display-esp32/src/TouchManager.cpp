#include "TouchManager.h"

#include <Arduino.h>
#include <lvgl.h>

#include "Config.h"
#include "DisplayManager.h"

// ----------------------------------------------------------------------------
// FINAL MEASURED CALIBRATION -- V1.2.2, physically measured against the real
// XPT2046 panel at rotation=3 using the on-device calibration screen
// (CalibrationManager.cpp). These are the fitted per-axis linear
// (scale + offset) coefficients from deriveCalibrationFromSamples()
// (Calibration.cpp) applied to the 5 measured target points (4 corners +
// center) -- see docs/DISPLAY_HARDWARE.md's "Touch calibration procedure"
// for the full dataset, the model comparison that selected this decoupled-
// linear model over a full 2D affine fit, and the BUG FIX note explaining
// why the earlier raw-min/max model (which this replaced, 2026-08-09)
// produced ~30-42px corner error despite a near-perfect center. If
// recalibration is ever needed again, the calibration screen remains
// available as a diagnostic/manual mode -- see
// Screens.h's showScreen(SunnyUIScreen::CALIBRATION).
// ----------------------------------------------------------------------------
namespace {
constexpr bool CAL_SWAP_AXES = true;
constexpr float CAL_SCALE_X = 0.09019494f;
constexpr float CAL_OFFSET_X = -16.02758f;
constexpr float CAL_SCALE_Y = 0.06781033f;
constexpr float CAL_OFFSET_Y = -18.23129f;

bool touchReady = false;
lv_indev_t *lvIndev = nullptr;

void lvglTouchReadCallback(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  int32_t x = 0, y = 0;
  if (readTouchScreenPoint(&x, &y)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
}  // namespace

void transformRawTouchToScreen(int32_t rawX, int32_t rawY, int32_t screenWidth, int32_t screenHeight, bool swapAxes,
                                float scaleX, float offsetX, float scaleY, float offsetY, int32_t *outScreenX,
                                int32_t *outScreenY) {
  int32_t xChannel = swapAxes ? rawY : rawX;
  int32_t yChannel = swapAxes ? rawX : rawY;

  auto roundToInt = [](float v) -> int32_t { return (int32_t)(v >= 0.0f ? v + 0.5f : v - 0.5f); };

  int32_t sx = roundToInt(scaleX * (float)xChannel + offsetX);
  int32_t sy = roundToInt(scaleY * (float)yChannel + offsetY);

  if (sx < 0) sx = 0;
  if (sx > screenWidth - 1) sx = screenWidth - 1;
  if (sy < 0) sy = 0;
  if (sy > screenHeight - 1) sy = screenHeight - 1;

  *outScreenX = sx;
  *outScreenY = sy;
}

bool readTouchScreenPoint(int32_t *outScreenX, int32_t *outScreenY) {
  int32_t rawX = 0, rawY = 0;
  if (!readRawTouchPoint(&rawX, &rawY)) return false;

  // LCD_HEIGHT/LCD_WIDTH swapped here to match DisplayManager's
  // rotation(3) landscape logical resolution (see DisplayManager.cpp's
  // lv_display_create(LCD_HEIGHT, LCD_WIDTH) call).
  transformRawTouchToScreen(rawX, rawY, LCD_HEIGHT, LCD_WIDTH, CAL_SWAP_AXES, CAL_SCALE_X, CAL_OFFSET_X, CAL_SCALE_Y,
                             CAL_OFFSET_Y, outScreenX, outScreenY);
  return true;
}

void initTouchManager() {
  Serial.println(F("[TOUCH] Initializing XPT2046 input device (FINAL measured calibration -- see "
                    "docs/DISPLAY_HARDWARE.md)"));
  lvIndev = lv_indev_create();
  lv_indev_set_type(lvIndev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(lvIndev, lvglTouchReadCallback);
  touchReady = true;
}

bool isTouchManagerReady() { return touchReady; }

void printTouchManagerStatus() {
  Serial.printf("[TOUCH] ready=%d calibrated=1 swapAxes=%d scaleX=%.6f offsetX=%.4f scaleY=%.6f offsetY=%.4f\n",
                touchReady ? 1 : 0, CAL_SWAP_AXES ? 1 : 0, (double)CAL_SCALE_X, (double)CAL_OFFSET_X,
                (double)CAL_SCALE_Y, (double)CAL_OFFSET_Y);
}

bool readRawTouchPointForCalibration(int32_t *outRawX, int32_t *outRawY) { return readRawTouchPoint(outRawX, outRawY); }
