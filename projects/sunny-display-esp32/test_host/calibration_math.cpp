// Host-side regression coverage for Calibration.h/.cpp's pure touch-
// calibration math (target geometry, trimmed-mean noise filtering, and
// fitting a per-axis linear scale+offset transform from measured
// (raw, known-target-screen-position) sample pairs). Same standalone-
// host-test approach as every other test_host/*.cpp file in this repo --
// no Arduino/LVGL/hardware dependency, pure logic mirrored inline below.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/calibration_math_test test_host/calibration_math.cpp && /tmp/calibration_math_test
//
// V1.2.2 BUG FIX (2026-08-09): this file's model changed from
// deriveCalibrationFromCorners() (raw min/max mapped directly to screen
// 0/width-1) to deriveCalibrationFromSamples() (fits scale+offset against
// each target's ACTUAL known screen position). The old model treated an
// inset calibration target's raw reading as if measured at the true
// screen edge, producing ~30-42px corner error on real hardware despite
// a near-perfect center. See test_host/final_touch_calibration.cpp for
// the real measured dataset regression proving the fix.
//
// Covers:
//  1. Target screen points land at the expected inset corners/center
//  2. Trimmed mean rejects a single outlier spike
//  3. Trimmed mean falls back to a plain mean when trimming would empty the set
//  4. deriveCalibrationFromSamples: no-swap, positive-scale synthetic case fits exactly
//  5. deriveCalibrationFromSamples: swap detected when rawX correlates with screenY, not screenX
//  6. deriveCalibrationFromSamples: negative scale (inverted axis) fit correctly, not forced positive
//  7. deriveCalibrationFromSamples: an inset target's raw reading does NOT get treated as the screen edge
//     (the specific bug this model replaces) -- fitted screenX at the true inset position, not 0

#include <algorithm>
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
static void checkNear(double actual, double expected, double tol, const char *what) {
  check(std::fabs(actual - expected) <= tol, what);
}

// ----------------------------------------------------------------------------
// Mirrors include/Calibration.h / src/Calibration.cpp exactly.
// ----------------------------------------------------------------------------
enum class CalibrationTargetId : uint8_t {
  TOP_LEFT = 0,
  TOP_RIGHT = 1,
  BOTTOM_RIGHT = 2,
  BOTTOM_LEFT = 3,
  CENTER = 4,
};

constexpr int32_t CALIBRATION_TARGET_INSET_PX = 30;

static void calibrationTargetScreenPoint(CalibrationTargetId target, int32_t screenWidth, int32_t screenHeight,
                                          int32_t *outX, int32_t *outY) {
  int32_t inset = CALIBRATION_TARGET_INSET_PX;
  switch (target) {
    case CalibrationTargetId::TOP_LEFT:
      *outX = inset;
      *outY = inset;
      break;
    case CalibrationTargetId::TOP_RIGHT:
      *outX = screenWidth - 1 - inset;
      *outY = inset;
      break;
    case CalibrationTargetId::BOTTOM_RIGHT:
      *outX = screenWidth - 1 - inset;
      *outY = screenHeight - 1 - inset;
      break;
    case CalibrationTargetId::BOTTOM_LEFT:
      *outX = inset;
      *outY = screenHeight - 1 - inset;
      break;
    case CalibrationTargetId::CENTER:
      *outX = screenWidth / 2;
      *outY = screenHeight / 2;
      break;
  }
}

static int32_t calibrationTrimmedMean(const int32_t *samples, int count, float trimFraction) {
  if (count <= 0) return 0;
  constexpr int MAX_SAMPLES = 64;
  int n = count > MAX_SAMPLES ? MAX_SAMPLES : count;

  int32_t sorted[MAX_SAMPLES];
  for (int i = 0; i < n; i++) sorted[i] = samples[i];
  std::sort(sorted, sorted + n);

  int trim = (int)(n * trimFraction);
  int lo = trim;
  int hi = n - trim;
  if (hi <= lo) {
    lo = 0;
    hi = n;
  }

  int64_t sum = 0;
  int cnt = 0;
  for (int i = lo; i < hi; i++) {
    sum += sorted[i];
    cnt++;
  }
  if (cnt == 0) return 0;
  return (int32_t)(sum / cnt);
}

struct CalibrationRawSample {
  int32_t rawX;
  int32_t rawY;
};

struct DerivedCalibration {
  bool swapAxes;
  float scaleX;
  float offsetX;
  float scaleY;
  float offsetY;
};

static float meanOfInt(const int32_t *v, int n) {
  int64_t sum = 0;
  for (int i = 0; i < n; i++) sum += v[i];
  return n > 0 ? (float)sum / (float)n : 0.0f;
}

static float absCorrelation(const int32_t *a, const int32_t *b, int n) {
  if (n < 2) return 0.0f;
  float ma = meanOfInt(a, n), mb = meanOfInt(b, n);
  double num = 0, sumSqA = 0, sumSqB = 0;
  for (int i = 0; i < n; i++) {
    double da = a[i] - ma, db = b[i] - mb;
    num += da * db;
    sumSqA += da * da;
    sumSqB += db * db;
  }
  double denom = std::sqrt(sumSqA) * std::sqrt(sumSqB);
  if (denom == 0) return 0.0f;
  float c = (float)(num / denom);
  return c < 0 ? -c : c;
}

static void linearFit(const int32_t *channel, const int32_t *target, int n, float *outScale, float *outOffset) {
  float meanC = meanOfInt(channel, n), meanT = meanOfInt(target, n);
  double num = 0, den = 0;
  for (int i = 0; i < n; i++) {
    double dc = channel[i] - meanC;
    num += dc * (target[i] - meanT);
    den += dc * dc;
  }
  float scale = den != 0 ? (float)(num / den) : 0.0f;
  float offset = meanT - scale * meanC;
  *outScale = scale;
  *outOffset = offset;
}

static DerivedCalibration deriveCalibrationFromSamples(const CalibrationRawSample *rawSamples,
                                                         const int32_t *targetScreenX, const int32_t *targetScreenY,
                                                         int count) {
  constexpr int MAX_SAMPLES = 16;
  int n = count > MAX_SAMPLES ? MAX_SAMPLES : count;

  int32_t rawXArr[MAX_SAMPLES];
  int32_t rawYArr[MAX_SAMPLES];
  for (int i = 0; i < n; i++) {
    rawXArr[i] = rawSamples[i].rawX;
    rawYArr[i] = rawSamples[i].rawY;
  }

  float corrRawXvsScreenX = absCorrelation(rawXArr, targetScreenX, n);
  float corrRawXvsScreenY = absCorrelation(rawXArr, targetScreenY, n);

  DerivedCalibration result{};
  result.swapAxes = corrRawXvsScreenY > corrRawXvsScreenX;

  const int32_t *xChannel = result.swapAxes ? rawYArr : rawXArr;
  const int32_t *yChannel = result.swapAxes ? rawXArr : rawYArr;

  linearFit(xChannel, targetScreenX, n, &result.scaleX, &result.offsetX);
  linearFit(yChannel, targetScreenY, n, &result.scaleY, &result.offsetY);

  return result;
}

// --- 1. Target screen points land at expected inset corners/center ---
static void test_01_target_points_at_expected_insets() {
  constexpr int32_t W = 320, H = 240, INSET = CALIBRATION_TARGET_INSET_PX;
  int32_t x = -1, y = -1;

  calibrationTargetScreenPoint(CalibrationTargetId::TOP_LEFT, W, H, &x, &y);
  check(x == INSET && y == INSET, "1. TOP_LEFT at (inset, inset)");

  calibrationTargetScreenPoint(CalibrationTargetId::TOP_RIGHT, W, H, &x, &y);
  check(x == W - 1 - INSET && y == INSET, "1. TOP_RIGHT at (W-1-inset, inset)");

  calibrationTargetScreenPoint(CalibrationTargetId::BOTTOM_RIGHT, W, H, &x, &y);
  check(x == W - 1 - INSET && y == H - 1 - INSET, "1. BOTTOM_RIGHT at (W-1-inset, H-1-inset)");

  calibrationTargetScreenPoint(CalibrationTargetId::BOTTOM_LEFT, W, H, &x, &y);
  check(x == INSET && y == H - 1 - INSET, "1. BOTTOM_LEFT at (inset, H-1-inset)");

  calibrationTargetScreenPoint(CalibrationTargetId::CENTER, W, H, &x, &y);
  check(x == W / 2 && y == H / 2, "1. CENTER at (W/2, H/2)");
}

// --- 2. Trimmed mean rejects a single outlier spike ---
static void test_02_trimmed_mean_rejects_outlier() {
  int32_t samples[] = {1000, 1010, 1005, 995, 1002, 9999};  // one wild spike
  int32_t result = calibrationTrimmedMean(samples, 6, 0.2f);
  check(result > 990 && result < 1020, "2. trimmed mean stays near the tight cluster, ignoring the spike");
}

// --- 3. Trimmed mean falls back to plain mean when trimming would empty the set ---
static void test_03_trimmed_mean_falls_back_on_small_sample() {
  int32_t samples[] = {100, 200};
  int32_t result = calibrationTrimmedMean(samples, 2, 0.4f);
  check(result == 150, "3. small sample count falls back to a plain mean (100,200 -> 150)");
}

// --- 4. No swap, positive scale: synthetic data with an exact linear relationship ---
static void test_04_no_swap_exact_linear_fit() {
  // screenX = 0.1*rawX - 50 ; screenY = 0.2*rawY - 40, exactly, over a 2x2 factorial design.
  CalibrationRawSample samples[] = {{1000, 1000}, {3000, 1000}, {1000, 3000}, {3000, 3000}};
  int32_t sx[] = {50, 250, 50, 250};
  int32_t sy[] = {160, 160, 560, 560};
  DerivedCalibration d = deriveCalibrationFromSamples(samples, sx, sy, 4);
  check(!d.swapAxes, "4. rawX correlates with screenX, not screenY -> no swap");
  checkNear(d.scaleX, 0.1, 1e-4, "4. scaleX fits exactly to 0.1");
  checkNear(d.offsetX, -50, 1e-2, "4. offsetX fits exactly to -50");
  checkNear(d.scaleY, 0.2, 1e-4, "4. scaleY fits exactly to 0.2");
  checkNear(d.offsetY, -40, 1e-2, "4. offsetY fits exactly to -40");
}

// --- 5. Swap detected: rawX actually drives screenY, rawY drives screenX ---
static void test_05_swap_detected() {
  // screenX = 0.1*rawY - 50 ; screenY = 0.2*rawX - 40 -- axes crossed relative to test 4.
  CalibrationRawSample samples[] = {{1000, 1000}, {1000, 3000}, {3000, 1000}, {3000, 3000}};
  int32_t sx[] = {50, 250, 50, 250};
  int32_t sy[] = {160, 160, 560, 560};
  DerivedCalibration d = deriveCalibrationFromSamples(samples, sx, sy, 4);
  check(d.swapAxes, "5. rawX correlates with screenY, not screenX -> swap detected");
  checkNear(d.scaleX, 0.1, 1e-4, "5. scaleX (fit against rawY) still fits to 0.1");
  checkNear(d.scaleY, 0.2, 1e-4, "5. scaleY (fit against rawX) still fits to 0.2");
}

// --- 6. Negative scale (inverted axis) is fit correctly, not forced positive ---
static void test_06_negative_scale_fit_correctly() {
  // screenX decreases as rawX increases: screenX = -0.1*rawX + 350.
  CalibrationRawSample samples[] = {{1000, 500}, {3000, 500}, {1000, 1500}, {3000, 1500}};
  int32_t sx[] = {250, 50, 250, 50};
  int32_t sy[] = {100, 100, 100, 100};  // constant -- isolates the X-axis check
  DerivedCalibration d = deriveCalibrationFromSamples(samples, sx, sy, 4);
  check(d.scaleX < 0, "6. negative correlation fits a negative scale, not clamped/forced positive");
  checkNear(d.scaleX, -0.1, 1e-4, "6. scaleX fits exactly to -0.1");
  checkNear(d.offsetX, 350, 1e-2, "6. offsetX fits exactly to 350");
}

// --- 7. THE BUG THIS MODEL FIXES: an inset target's raw reading must fit to
//        its true (inset) screen position, not get treated as the screen edge ---
static void test_07_inset_target_does_not_collapse_to_screen_edge() {
  // Two points on the X axis: TOP_LEFT's raw reading at the true INSET
  // position (30, not 0) and TOP_RIGHT's at (289, not 319) -- exactly
  // the geometry a real calibration screen produces. A correctly fitted
  // model must predict screenX close to 30 and 289 for these same raw
  // values, NOT 0 and 319 (which is what the old raw-min/max model did).
  CalibrationRawSample samples[] = {{721, 519}, {702, 3382}};  // real TOP_LEFT / TOP_RIGHT raw values
  int32_t sx[] = {30, 289};                                    // their TRUE inset screen positions
  int32_t sy[] = {30, 30};
  DerivedCalibration d = deriveCalibrationFromSamples(samples, sx, sy, 2);
  // With exactly 2 points the fit is exact by construction -- confirm it
  // reproduces the true inset positions, not the screen edges.
  float predictedX1 = d.swapAxes ? d.scaleX * samples[0].rawY + d.offsetX : d.scaleX * samples[0].rawX + d.offsetX;
  float predictedX2 = d.swapAxes ? d.scaleX * samples[1].rawY + d.offsetX : d.scaleX * samples[1].rawX + d.offsetX;
  checkNear(predictedX1, 30, 1.0, "7. TOP_LEFT's raw fits back to screenX=30 (its true inset position)");
  checkNear(predictedX2, 289, 1.0, "7. TOP_RIGHT's raw fits back to screenX=289 (its true inset position)");
  check(std::fabs(predictedX1 - 0) > 5, "7. TOP_LEFT does NOT collapse to screen edge 0 (the old bug)");
  check(std::fabs(predictedX2 - 319) > 5, "7. TOP_RIGHT does NOT collapse to screen edge 319 (the old bug)");
}

int main() {
  test_01_target_points_at_expected_insets();
  test_02_trimmed_mean_rejects_outlier();
  test_03_trimmed_mean_falls_back_on_small_sample();
  test_04_no_swap_exact_linear_fit();
  test_05_swap_detected();
  test_06_negative_scale_fit_correctly();
  test_07_inset_target_does_not_collapse_to_screen_edge();

  if (g_failures == 0) {
    printf("All calibration_math tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
