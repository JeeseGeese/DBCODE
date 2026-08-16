// Host-side regression coverage for UIState.h/.cpp's bring-up tap-test
// state model. Same standalone-host-test approach as the Sunny body
// project's test_host/*.cpp files: no Arduino/LVGL dependency, pure logic
// mirrored inline below.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/ui_state_model_test test_host/ui_state_model.cpp && /tmp/ui_state_model_test
//
// Covers:
//  1. A press-start increments tapCount exactly once
//  2. A held press (no release in between) does not double-count
//  3. Press/release/press/release counts twice, not once
//  4. Press-lost (treated as release) does not itself count as a tap
//  5. buttonCurrentlyPressed reflects the current press state accurately
//  6. isScreenImplemented() is true only for BRING_UP_TAP_TEST/CALIBRATION,
//     false for every navigation-infrastructure-only placeholder screen

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
// Mirrors include/UIState.h / src/UIState.cpp exactly.
// ----------------------------------------------------------------------------
struct BringUpTapTestState {
  uint32_t tapCount = 0;
  bool buttonCurrentlyPressed = false;
};

static void bringUpTapTestOnPressStart(BringUpTapTestState *state) {
  if (state->buttonCurrentlyPressed) return;
  state->buttonCurrentlyPressed = true;
  state->tapCount++;
}

static void bringUpTapTestOnPressEnd(BringUpTapTestState *state) { state->buttonCurrentlyPressed = false; }

enum class SunnyUIScreen : uint8_t {
  BRING_UP_TAP_TEST,
  CALIBRATION,
  TOUCH_VALIDATION,
  HOME,
  AUDIO,
  MOTION,
  LEDS,
  DIAGNOSTICS,
  SETTINGS,
};

static bool isScreenImplemented(SunnyUIScreen screen) {
  switch (screen) {
    case SunnyUIScreen::BRING_UP_TAP_TEST:
    case SunnyUIScreen::CALIBRATION:
    case SunnyUIScreen::TOUCH_VALIDATION:
      return true;
    default:
      return false;
  }
}

// --- 1. A press-start increments tapCount exactly once ---
static void test_01_press_start_increments_once() {
  BringUpTapTestState s;
  bringUpTapTestOnPressStart(&s);
  check(s.tapCount == 1, "1. single press-start increments tapCount to 1");
  check(s.buttonCurrentlyPressed, "1. buttonCurrentlyPressed is true after press-start");
}

// --- 2. A held press does not double-count ---
static void test_02_held_press_does_not_double_count() {
  BringUpTapTestState s;
  bringUpTapTestOnPressStart(&s);
  bringUpTapTestOnPressStart(&s);  // simulates a repeated PRESSED event while still held
  bringUpTapTestOnPressStart(&s);
  check(s.tapCount == 1, "2. repeated press-start while already pressed does not increment further");
}

// --- 3. Press/release/press/release counts twice ---
static void test_03_two_full_taps_count_twice() {
  BringUpTapTestState s;
  bringUpTapTestOnPressStart(&s);
  bringUpTapTestOnPressEnd(&s);
  check(!s.buttonCurrentlyPressed, "3. released after first tap");
  bringUpTapTestOnPressStart(&s);
  bringUpTapTestOnPressEnd(&s);
  check(s.tapCount == 2, "3. two independent press/release cycles count as two taps");
}

// --- 4. Press-lost (release without a matching new press) doesn't itself count ---
static void test_04_release_alone_does_not_increment() {
  BringUpTapTestState s;
  bringUpTapTestOnPressEnd(&s);  // release with no prior press (e.g. PRESS_LOST at startup)
  check(s.tapCount == 0, "4. a release/press-lost event alone never increments tapCount");
  check(!s.buttonCurrentlyPressed, "4. buttonCurrentlyPressed stays false");
}

// --- 5. buttonCurrentlyPressed accurately tracks state across a sequence ---
static void test_05_pressed_state_tracks_accurately() {
  BringUpTapTestState s;
  check(!s.buttonCurrentlyPressed, "5. starts unpressed");
  bringUpTapTestOnPressStart(&s);
  check(s.buttonCurrentlyPressed, "5. pressed after press-start");
  bringUpTapTestOnPressEnd(&s);
  check(!s.buttonCurrentlyPressed, "5. unpressed after press-end");
}

// --- 6. isScreenImplemented() is true only for real screens ---
static void test_06_is_screen_implemented_matches_real_screens() {
  check(isScreenImplemented(SunnyUIScreen::BRING_UP_TAP_TEST), "6. BRING_UP_TAP_TEST is implemented");
  check(isScreenImplemented(SunnyUIScreen::CALIBRATION), "6. CALIBRATION is implemented");
  check(isScreenImplemented(SunnyUIScreen::TOUCH_VALIDATION), "6. TOUCH_VALIDATION is implemented");
  check(!isScreenImplemented(SunnyUIScreen::HOME), "6. HOME is not yet implemented");
  check(!isScreenImplemented(SunnyUIScreen::AUDIO), "6. AUDIO is not yet implemented");
  check(!isScreenImplemented(SunnyUIScreen::MOTION), "6. MOTION is not yet implemented");
  check(!isScreenImplemented(SunnyUIScreen::LEDS), "6. LEDS is not yet implemented");
  check(!isScreenImplemented(SunnyUIScreen::DIAGNOSTICS), "6. DIAGNOSTICS is not yet implemented");
  check(!isScreenImplemented(SunnyUIScreen::SETTINGS), "6. SETTINGS is not yet implemented");
}

int main() {
  test_01_press_start_increments_once();
  test_02_held_press_does_not_double_count();
  test_03_two_full_taps_count_twice();
  test_04_release_alone_does_not_increment();
  test_05_pressed_state_tracks_accurately();
  test_06_is_screen_implemented_matches_real_screens();

  if (g_failures == 0) {
    printf("All ui_state_model tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
