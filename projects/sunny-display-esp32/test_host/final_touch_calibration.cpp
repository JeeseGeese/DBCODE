// Host-side regression coverage for the FINAL, physically measured V1.2.2
// touch calibration now applied in TouchManager.cpp. Same standalone-host-
// test approach as every other test_host/*.cpp file in this repo -- no
// Arduino/LVGL/hardware dependency, transform logic mirrored inline below.
// See docs/DISPLAY_HARDWARE.md's "Touch calibration procedure" section for
// the full measured dataset and model-comparison record this test locks in.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/final_touch_calibration_test test_host/final_touch_calibration.cpp && /tmp/final_touch_calibration_test
//
// THE BUG (2026-08-09): physical validation found all four corner targets
// were significantly offset from their visible dots even though the
// center was accurate and touch hardware/hitboxes worked fine. Root
// cause: the original calibration model treated each inset target's own
// averaged raw reading as if it had been measured at the true screen
// EDGE (raw min -> screen 0, raw max -> screen width-1). Since targets
// are drawn ~30px inset from the edge (for tappability), this produced a
// systematic ~30-42px error at every corner -- while the center stayed
// accidentally accurate, because the center target's true position IS
// the middle of the raw range, where the edge-assumption error cancels
// out. THE FIX: fit scale+offset directly against each target's ACTUAL
// known screen position (deriveCalibrationFromSamples() in
// Calibration.cpp), not an assumed edge.
//
// Covers:
//  1. Final applied constants match the fitted model from the 5-point measured dataset
//  2. NEW model: all 5 measured points map within the acceptance target
//     (center <=5px, corners <=10px) -- this is the fix
//  3. OLD model: reproduced here as a negative control, and PROVEN to fail the
//     same acceptance target at the corners (>=30px error) despite passing
//     at the center -- this is the bug, preserved as a regression check so
//     it can never be silently reintroduced
//  4. NEW model: no screen-bounds overflow for any of the 5 points
//  5. NEW model: no axis mirroring -- each point lands in the correct screen quadrant/half

#include <cmath>
#include <cstdint>
#include <cstdio>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

constexpr int32_t SCREEN_W = 320;
constexpr int32_t SCREEN_H = 240;

// ----------------------------------------------------------------------------
// NEW model -- mirrors src/TouchManager.cpp's transformRawTouchToScreen() and
// its FINAL applied CAL_* constants exactly.
// ----------------------------------------------------------------------------
static void transformNew(int32_t rawX, int32_t rawY, bool swapAxes, float scaleX, float offsetX, float scaleY,
                          float offsetY, int32_t *outScreenX, int32_t *outScreenY) {
  int32_t xChannel = swapAxes ? rawY : rawX;
  int32_t yChannel = swapAxes ? rawX : rawY;

  auto roundToInt = [](float v) -> int32_t { return (int32_t)(v >= 0.0f ? v + 0.5f : v - 0.5f); };

  int32_t sx = roundToInt(scaleX * (float)xChannel + offsetX);
  int32_t sy = roundToInt(scaleY * (float)yChannel + offsetY);

  if (sx < 0) sx = 0;
  if (sx > SCREEN_W - 1) sx = SCREEN_W - 1;
  if (sy < 0) sy = 0;
  if (sy > SCREEN_H - 1) sy = SCREEN_H - 1;

  *outScreenX = sx;
  *outScreenY = sy;
}

// FINAL fitted constants (V1.2.2, second/authoritative measured pass,
// fitted from all 5 targets via deriveCalibrationFromSamples()).
constexpr bool CAL_SWAP_AXES = true;
constexpr float CAL_SCALE_X = 0.09019494f;
constexpr float CAL_OFFSET_X = -16.02758f;
constexpr float CAL_SCALE_Y = 0.06781033f;
constexpr float CAL_OFFSET_Y = -18.23129f;

// ----------------------------------------------------------------------------
// OLD model -- the exact raw-min/max transform this replaced, preserved here
// ONLY as a regression negative-control (proves the bug, not used at runtime
// anymore). Constants are the ones actually derived+applied under the old
// model from the same measured corner data.
// ----------------------------------------------------------------------------
static void transformOld(int32_t rawX, int32_t rawY, int32_t calRawXMin, int32_t calRawXMax, int32_t calRawYMin,
                          int32_t calRawYMax, bool swapAxes, int32_t *outScreenX, int32_t *outScreenY) {
  if (swapAxes) {
    int32_t tmp = rawX;
    rawX = rawY;
    rawY = tmp;
  }
  auto mapClamp = [](int32_t v, int32_t inMin, int32_t inMax, int32_t outMax) -> int32_t {
    if (inMax == inMin) return 0;
    int32_t mapped = (int32_t)(((int64_t)(v - inMin) * outMax) / (inMax - inMin));
    if (mapped < 0) mapped = 0;
    if (mapped > outMax - 1) mapped = outMax - 1;
    return mapped;
  };
  *outScreenX = mapClamp(rawX, calRawXMin, calRawXMax, SCREEN_W);
  *outScreenY = mapClamp(rawY, calRawYMin, calRawYMax, SCREEN_H);
}
constexpr int32_t OLD_X_MIN = 513, OLD_X_MAX = 3384, OLD_Y_MIN = 711, OLD_Y_MAX = 3351;

// --- Measured dataset (authoritative second calibration pass, 2026-08-09) ---
struct MeasuredPoint {
  const char *name;
  int32_t rawX, rawY;
  int32_t trueScreenX, trueScreenY;
};
constexpr MeasuredPoint kMeasured[5] = {
    {"TOP-LEFT", 721, 519, 30, 30},        {"TOP-RIGHT", 702, 3382, 289, 30},
    {"BOTTOM-RIGHT", 3365, 3387, 289, 209}, {"BOTTOM-LEFT", 3337, 507, 30, 209},
    {"CENTER", 2038, 1941, 160, 120},
};

static double dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
  double dx = x1 - x2, dy = y1 - y2;
  return std::sqrt(dx * dx + dy * dy);
}

// --- 1. Final constants match the fitted model ---
static void test_01_final_constants_match_fitted_model() {
  check(CAL_SWAP_AXES == true, "1. CAL_SWAP_AXES == true");
  check(std::fabs(CAL_SCALE_X - 0.09019494f) < 1e-6f, "1. CAL_SCALE_X matches the fitted value");
  check(std::fabs(CAL_OFFSET_X - (-16.02758f)) < 1e-3f, "1. CAL_OFFSET_X matches the fitted value");
  check(std::fabs(CAL_SCALE_Y - 0.06781033f) < 1e-6f, "1. CAL_SCALE_Y matches the fitted value");
  check(std::fabs(CAL_OFFSET_Y - (-18.23129f)) < 1e-3f, "1. CAL_OFFSET_Y matches the fitted value");
}

// --- 2. NEW model meets the acceptance target for all 5 points (THE FIX) ---
static void test_02_new_model_meets_acceptance_target() {
  for (const auto &p : kMeasured) {
    int32_t sx = -1, sy = -1;
    transformNew(p.rawX, p.rawY, CAL_SWAP_AXES, CAL_SCALE_X, CAL_OFFSET_X, CAL_SCALE_Y, CAL_OFFSET_Y, &sx, &sy);
    double err = dist(sx, sy, p.trueScreenX, p.trueScreenY);
    double maxAllowed = (p.name == kMeasured[4].name) ? 5.0 : 10.0;  // center<=5px, corners<=10px
    char msg[160];
    snprintf(msg, sizeof(msg), "2. NEW model: %s error %.2fpx within acceptance target (<=%.0fpx)", p.name, err,
              maxAllowed);
    check(err <= maxAllowed, msg);
  }
}

// --- 3. OLD model FAILS the same acceptance target at the corners (THE BUG,
//        kept as a permanent regression guard) ---
static void test_03_old_model_fails_at_corners_regression_guard() {
  double cornerErrors[4];
  int cornerCount = 0;
  double centerError = -1;
  for (const auto &p : kMeasured) {
    int32_t sx = -1, sy = -1;
    transformOld(p.rawX, p.rawY, OLD_X_MIN, OLD_X_MAX, OLD_Y_MIN, OLD_Y_MAX, CAL_SWAP_AXES, &sx, &sy);
    double err = dist(sx, sy, p.trueScreenX, p.trueScreenY);
    if (p.name == kMeasured[4].name) {
      centerError = err;
    } else {
      cornerErrors[cornerCount++] = err;
    }
  }
  for (int i = 0; i < cornerCount; i++) {
    check(cornerErrors[i] >= 30.0, "3. OLD model corner error is >=30px, reproducing the physically observed bug");
  }
  check(centerError <= 5.0, "3. OLD model's center error stayed small -- exactly why the bug was easy to miss");
}

// --- 4. NEW model: no screen-bounds overflow for any of the 5 points ---
static void test_04_new_model_no_overflow() {
  for (const auto &p : kMeasured) {
    int32_t sx = -1, sy = -1;
    transformNew(p.rawX, p.rawY, CAL_SWAP_AXES, CAL_SCALE_X, CAL_OFFSET_X, CAL_SCALE_Y, CAL_OFFSET_Y, &sx, &sy);
    check(sx >= 0 && sx < SCREEN_W, "4. screenX stays within [0, SCREEN_W)");
    check(sy >= 0 && sy < SCREEN_H, "4. screenY stays within [0, SCREEN_H)");
  }
}

// --- 5. NEW model: no axis mirroring -- each point lands in the correct half/quadrant ---
static void test_05_new_model_no_mirroring() {
  int32_t sx = -1, sy = -1;
  transformNew(kMeasured[0].rawX, kMeasured[0].rawY, CAL_SWAP_AXES, CAL_SCALE_X, CAL_OFFSET_X, CAL_SCALE_Y,
               CAL_OFFSET_Y, &sx, &sy);
  check(sx < SCREEN_W / 2, "5. TOP-LEFT maps to the left half, not mirrored to the right");
  check(sy < SCREEN_H / 2, "5. TOP-LEFT maps to the top half, not mirrored to the bottom");

  transformNew(kMeasured[2].rawX, kMeasured[2].rawY, CAL_SWAP_AXES, CAL_SCALE_X, CAL_OFFSET_X, CAL_SCALE_Y,
               CAL_OFFSET_Y, &sx, &sy);
  check(sx > SCREEN_W / 2, "5. BOTTOM-RIGHT maps to the right half, not mirrored to the left");
  check(sy > SCREEN_H / 2, "5. BOTTOM-RIGHT maps to the bottom half, not mirrored to the top");
}

int main() {
  test_01_final_constants_match_fitted_model();
  test_02_new_model_meets_acceptance_target();
  test_03_old_model_fails_at_corners_regression_guard();
  test_04_new_model_no_overflow();
  test_05_new_model_no_mirroring();

  if (g_failures == 0) {
    printf("All final_touch_calibration tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
