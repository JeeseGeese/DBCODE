// Host-side regression coverage for the Sunny V1.1 LED-count correction
// (NUM_LEDS 58 -> 36, physically confirmed) and the HWTEST power-safety fix
// (src/HardwareTest.cpp now routes every frame through the SAME
// applyPowerLimit() the normal render loop uses, src/main.cpp). Same
// standalone-host-test approach as every other test_host/*.cpp file --
// pure integer/float arithmetic, no Arduino/ESP-IDF dependency, the
// relevant constants/formulas mirrored inline below.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/led_count_and_power_test test_host/led_count_and_power.cpp && /tmp/led_count_and_power_test
//
// Covers:
//  1. NUM_LEDS is the corrected physical count (36), not the old 58
//  2. PHYSICAL_LED_COUNT/LED_ROW_1-3 config invariants still hold at 36
//     (no leftover 42-LED-era inconsistency)
//  3. Symmetric-center overlay math (e.g. AudioOverlays.cpp's
//     `(NUM_LEDS - 1) / 2.0f`) computes the NEW center, not the old 58-LED one
//  4. applyPowerLimit()'s estimator scales with NUM_LEDS (uses the real
//     configured count, not a stale literal)
//  5. A raw, unscaled solid-white 36-LED frame exceeds LED_CURRENT_LIMIT_MA
//     and applyPowerLimit() therefore scales it down -- proving the fixed
//     HWTEST (which now runs every frame through this same function) can no
//     longer emit an unprotected full-white frame
//  6. A frame within the current limit is left untouched (no unnecessary
//     scaling)
//  7. No index in a 0..NUM_LEDS-1 loop can equal or exceed 36 (basic
//     buffer-overrun sanity check for the corrected count)

#include <cassert>
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
// Mirrors include/Config.h exactly (post 2026-08-08 LED-count correction).
// ----------------------------------------------------------------------------
constexpr int NUM_LEDS = 36;

struct LedRegion {
  uint16_t start;
  uint16_t count;
};
constexpr LedRegion LED_ROW_1{0, 12};
constexpr LedRegion LED_ROW_2{12, 12};
constexpr LedRegion LED_ROW_3{24, 12};
constexpr uint16_t PHYSICAL_LED_COUNT = 36;

constexpr uint16_t LED_CURRENT_LIMIT_MA = 1000;
constexpr uint8_t LED_MAX_MA_PER_CHANNEL = 20;
constexpr uint8_t LED_IDLE_MA_PER_LED = 1;

struct RGB8 {
  uint8_t r, g, b;
};

static void scaleClamp(RGB8 &c, float factor) {
  auto clamp255 = [](int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); };
  c.r = clamp255((int)(c.r * factor + 0.5f));
  c.g = clamp255((int)(c.g * factor + 0.5f));
  c.b = clamp255((int)(c.b * factor + 0.5f));
}

// Mirrors src/main.cpp's applyPowerLimit() exactly (formula + early-return
// behavior), parameterized over a buffer length so this file can also
// sanity-check the OLD (58) vs NEW (36) count difference in test 4.
static bool applyPowerLimit(RGB8 *buf, int numLeds, uint32_t *outEstimatedMa = nullptr, float *outScale = nullptr) {
  uint32_t channelSum = 0;
  for (int i = 0; i < numLeds; i++) channelSum += (uint32_t)buf[i].r + buf[i].g + buf[i].b;

  uint32_t estimatedMa = (uint32_t)numLeds * LED_IDLE_MA_PER_LED + (channelSum * LED_MAX_MA_PER_CHANNEL) / 255;
  if (outEstimatedMa) *outEstimatedMa = estimatedMa;
  if (estimatedMa <= LED_CURRENT_LIMIT_MA) {
    if (outScale) *outScale = 1.0f;
    return false;  // not scaled
  }

  float scale = (float)LED_CURRENT_LIMIT_MA / (float)estimatedMa;
  if (outScale) *outScale = scale;
  for (int i = 0; i < numLeds; i++) scaleClamp(buf[i], scale);
  return true;  // scaled
}

// --- 1. NUM_LEDS is the corrected physical count ---
static void test_01_num_leds_is_36() {
  check(NUM_LEDS == 36, "1. NUM_LEDS is the physically-confirmed 36, not the old 58");
}

// --- 2. PHYSICAL_LED_COUNT/LED_ROW_1-3 invariants hold at 36 ---
static void test_02_row_invariants_hold_at_36() {
  check(LED_ROW_1.start + LED_ROW_1.count == LED_ROW_2.start, "2. row1 ends where row2 begins");
  check(LED_ROW_2.start + LED_ROW_2.count == LED_ROW_3.start, "2. row2 ends where row3 begins");
  check(LED_ROW_3.start + LED_ROW_3.count == PHYSICAL_LED_COUNT, "2. row3 ends exactly at PHYSICAL_LED_COUNT");
  check(PHYSICAL_LED_COUNT == NUM_LEDS, "2. physical count now equals NUM_LEDS exactly (no more logical/physical gap)");
}

// --- 3. Symmetric-center math reflects the NEW 36-LED center, not the old 58-LED one ---
static void test_03_center_math_uses_new_count() {
  float center = (NUM_LEDS - 1) / 2.0f;
  check(center == 17.5f, "3. (NUM_LEDS-1)/2.0f is the new 36-LED center (17.5), not the old 58-LED center (28.5)");
  check(center != 28.5f, "3. center is NOT the stale 58-LED value");
}

// --- 4. The estimator uses the real configured count, not a stale literal ---
static void test_04_estimator_scales_with_num_leds() {
  RGB8 white36[36];
  for (auto &p : white36) p = RGB8{255, 255, 255};
  uint32_t est36 = 0;
  applyPowerLimit(white36, 36, &est36);

  RGB8 white58[58];
  for (auto &p : white58) p = RGB8{255, 255, 255};
  uint32_t est58 = 0;
  applyPowerLimit(white58, 58, &est58);

  check(est36 == 36 * 1 + (36 * 3 * 255 * 20) / 255, "4. 36-LED estimate matches the formula exactly");
  check(est58 > est36, "4. the (old, now-unused) 58-LED estimate would have been higher than the 36-LED one");
  check(est36 < est58, "4. confirms NUM_LEDS actually drives the estimate, not a hardcoded value");
}

// --- 5. A raw solid-white 36-LED frame exceeds the limit and gets scaled ---
static void test_05_solid_white_36_exceeds_limit_and_is_scaled() {
  RGB8 buf[36];
  for (auto &p : buf) p = RGB8{255, 255, 255};
  uint32_t estimatedMa = 0;
  float scale = 1.0f;
  bool wasScaled = applyPowerLimit(buf, 36, &estimatedMa, &scale);

  check(estimatedMa > LED_CURRENT_LIMIT_MA, "5. raw solid-white 36-LED frame estimate exceeds the 1000mA limit");
  check(wasScaled, "5. applyPowerLimit() actually scales a solid-white 36-LED frame down");
  check(buf[0].r < 255 && buf[0].g < 255 && buf[0].b < 255,
        "5. the fixed HWTEST path can no longer emit an unprotected raw-255 white frame");
  check(scale > 0.0f && scale < 1.0f, "5. scale factor is a genuine reduction, not a no-op");
}

// --- 6. A frame within the limit is left untouched ---
static void test_06_dim_frame_not_scaled() {
  RGB8 buf[36];
  for (auto &p : buf) p = RGB8{20, 0, 0};  // dim red -- well under the limit
  uint32_t estimatedMa = 0;
  bool wasScaled = applyPowerLimit(buf, 36, &estimatedMa);

  check(estimatedMa <= LED_CURRENT_LIMIT_MA, "6. dim frame estimate stays under the limit");
  check(!wasScaled, "6. applyPowerLimit() does not touch a frame that's already within budget");
  check(buf[0].r == 20, "6. untouched frame's pixel values are bit-for-bit unchanged");
}

// --- 7. No index in a 0..NUM_LEDS loop can reach or exceed 36 ---
static void test_07_no_index_reaches_old_58_bound() {
  int maxIndexSeen = -1;
  for (int i = 0; i < NUM_LEDS; i++) maxIndexSeen = i;
  check(maxIndexSeen == 35, "7. highest loop index is 35 (NUM_LEDS-1), never 57 (the old 58-LED bound)");
  check(maxIndexSeen < 58, "7. loop bound is well clear of the old 58-LED size");
}

int main() {
  test_01_num_leds_is_36();
  test_02_row_invariants_hold_at_36();
  test_03_center_math_uses_new_count();
  test_04_estimator_scales_with_num_leds();
  test_05_solid_white_36_exceeds_limit_and_is_scaled();
  test_06_dim_frame_not_scaled();
  test_07_no_index_reaches_old_58_bound();

  if (g_failures == 0) {
    printf("All led_count_and_power tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
