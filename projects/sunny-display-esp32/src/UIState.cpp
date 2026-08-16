#include "UIState.h"

void bringUpTapTestOnPressStart(BringUpTapTestState *state) {
  if (state->buttonCurrentlyPressed) return;  // already pressed -- don't double-count a held touch
  state->buttonCurrentlyPressed = true;
  state->tapCount++;
}

void bringUpTapTestOnPressEnd(BringUpTapTestState *state) { state->buttonCurrentlyPressed = false; }

bool isScreenImplemented(SunnyUIScreen screen) {
  switch (screen) {
    case SunnyUIScreen::BRING_UP_TAP_TEST:
    case SunnyUIScreen::CALIBRATION:
    case SunnyUIScreen::TOUCH_VALIDATION:
      return true;
    default:
      return false;
  }
}
