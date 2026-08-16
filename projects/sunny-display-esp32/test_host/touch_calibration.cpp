// Host-side regression coverage for TouchManager.h/.cpp's pure raw-ADC ->
// screen-pixel transform (transformRawTouchToScreen()). Same standalone-
// host-test approach as every other test_host/*.cpp file in this repo --
// no Arduino/LVGL/hardware dependency, pure arithmetic mirrored inline.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/touch_calibration_test test_host/touch_calibration.cpp && /tmp/touch_calibration_test
//
// V1.2.2 BUG FIX (2026-08-09): this function's model changed from raw
// min/max mapped linearly to the full screen 0..width-1 range (which
// silently assumed the calibration reading was taken AT the screen edge)
// to a directly-fitted per-axis scale+offset (see Calibration.h's
// deriveCalibrationFromSamples() for how the fit is produced). This file
// now tests the generic scale/offset/swap/clamp behavior of the
// transform itself; test_host/final_touch_calibration.cpp tests it
// against the real measured V1.2.2 dataset specifically.
//
// Covers:
//  1. Basic scale+offset mapping is arithmetically correct
//  2. Rounds to the nearest pixel, not truncated
//  3. Clamps below 0 (mapped value would be negative)
//  4. Clamps above screenSize-1 (mapped value would overflow)
//  5. Axis swap routes rawY into the screenX calculation and vice versa
//  6. A negative scale correctly inverts direction (not forced positive)
//  7. Degenerate zero scale (a flat/constant target) doesn't crash, still clamps safely

#include <cstdint>
#include <cstdio>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// ----------------------------------------------------------------------------
// Mirrors src/TouchManager.cpp's transformRawTouchToScreen() exactly.
// ----------------------------------------------------------------------------
static void transformRawTouchToScreen(int32_t rawX, int32_t rawY, int32_t screenWidth, int32_t screenHeight,
                                       bool swapAxes, float scaleX, float offsetX, float scaleY, float offsetY,
                                       int32_t *outScreenX, int32_t *outScreenY) {
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

constexpr int32_t W = 320;
constexpr int32_t H = 240;

// --- 1. Basic scale+offset mapping is arithmetically correct ---
static void test_01_basic_scale_offset_mapping() {
  int32_t sx = -1, sy = -1;
  // screenX = 0.1*rawX - 10 ; screenY = 0.2*rawY - 20
  transformRawTouchToScreen(1000, 500, W, H, false, 0.1f, -10.0f, 0.2f, -20.0f, &sx, &sy);
  check(sx == 90, "1. screenX = 0.1*1000 - 10 = 90");
  check(sy == 80, "1. screenY = 0.2*500 - 20 = 80");
}

// --- 2. Rounds to the nearest pixel, not truncated ---
static void test_02_rounds_not_truncates() {
  int32_t sx = -1, sy = -1;
  // scale*channel+offset = 10.6 -- must round to 11, not truncate to 10.
  transformRawTouchToScreen(100, 100, W, H, false, 0.106f, 0.0f, 0.106f, 0.0f, &sx, &sy);
  check(sx == 11, "2. 10.6 rounds to 11, not truncated to 10");
  check(sy == 11, "2. same for screenY");
}

// --- 3. Clamps below 0 ---
static void test_03_clamps_below_zero() {
  int32_t sx = -1, sy = -1;
  transformRawTouchToScreen(0, 0, W, H, false, 1.0f, -500.0f, 1.0f, -500.0f, &sx, &sy);
  check(sx == 0 && sy == 0, "3. a mapped value that would go negative clamps to 0");
}

// --- 4. Clamps above screenSize-1 ---
static void test_04_clamps_above_max() {
  int32_t sx = -1, sy = -1;
  transformRawTouchToScreen(0, 0, W, H, false, 1.0f, 99999.0f, 1.0f, 99999.0f, &sx, &sy);
  check(sx == W - 1, "4. a mapped value that would overflow clamps to screenWidth-1, not screenWidth");
  check(sy == H - 1, "4. same for screenHeight-1");
}

// --- 5. Axis swap routes rawY into screenX's calc and rawX into screenY's ---
static void test_05_axis_swap_routes_channels() {
  int32_t sxNoSwap = -1, syNoSwap = -1, sxSwap = -1, sySwap = -1;
  int32_t rawX = 100, rawY = 200;
  // Same scale/offset (1x, 0) on both axes so the swap's effect is visible directly.
  transformRawTouchToScreen(rawX, rawY, W, H, false, 1.0f, 0.0f, 1.0f, 0.0f, &sxNoSwap, &syNoSwap);
  transformRawTouchToScreen(rawX, rawY, W, H, true, 1.0f, 0.0f, 1.0f, 0.0f, &sxSwap, &sySwap);
  check(sxNoSwap == rawX && syNoSwap == rawY, "5. without swap, screenX<-rawX, screenY<-rawY");
  check(sxSwap == rawY && sySwap == rawX, "5. with swap, screenX<-rawY, screenY<-rawX");
}

// --- 6. A negative scale correctly inverts direction ---
static void test_06_negative_scale_inverts() {
  int32_t sxLow = -1, syLow = -1, sxHigh = -1, syHigh = -1;
  // screenX = -0.1*rawX + 100 -- screenX should DECREASE as rawX increases.
  transformRawTouchToScreen(100, 0, W, H, false, -0.1f, 100.0f, 1.0f, 0.0f, &sxLow, &syLow);
  transformRawTouchToScreen(500, 0, W, H, false, -0.1f, 100.0f, 1.0f, 0.0f, &sxHigh, &syHigh);
  check(sxLow == 90, "6. screenX at rawX=100 is -0.1*100+100=90");
  check(sxHigh == 50, "6. screenX at rawX=500 is -0.1*500+100=50 (lower, not forced positive-direction)");
  check(sxHigh < sxLow, "6. increasing rawX decreases screenX under a negative scale");
}

// --- 7. Degenerate zero scale doesn't crash, still clamps safely ---
static void test_07_zero_scale_fails_safe() {
  int32_t sx = -1, sy = -1;
  // scale=0 means the mapped value is just the offset, regardless of raw input.
  transformRawTouchToScreen(12345, 6789, W, H, false, 0.0f, 42.0f, 0.0f, 42.0f, &sx, &sy);
  check(sx == 42 && sy == 42, "7. zero scale maps every input to the constant offset, no crash/divide-by-zero");
}

int main() {
  test_01_basic_scale_offset_mapping();
  test_02_rounds_not_truncates();
  test_03_clamps_below_zero();
  test_04_clamps_above_max();
  test_05_axis_swap_routes_channels();
  test_06_negative_scale_inverts();
  test_07_zero_scale_fails_safe();

  if (g_failures == 0) {
    printf("All touch_calibration tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
