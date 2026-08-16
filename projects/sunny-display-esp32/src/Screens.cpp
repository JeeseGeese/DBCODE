#include "Screens.h"

#include <Arduino.h>
#include <lvgl.h>

#include "Calibration.h"
#include "CalibrationManager.h"
#include "Config.h"
#include "TouchManager.h"
#include "UIState.h"

namespace {
BringUpTapTestState tapState;
lv_obj_t *tapButtonLabel = nullptr;
SunnyUIScreen currentScreen = SunnyUIScreen::BRING_UP_TAP_TEST;

lv_obj_t *validationStatusLabel = nullptr;
constexpr int32_t VALIDATION_DOT_DIAMETER_PX = 18;   // visible target -- small on purpose
constexpr int32_t VALIDATION_HITBOX_SIZE_PX = 50;    // much larger invisible/transparent hit area around it
constexpr int32_t VALIDATION_CROSSHAIR_SIZE_PX = 10;

struct ValidationTargetCtx {
  CalibrationTargetId id;
  lv_obj_t *dot;
};
ValidationTargetCtx validationCtx[CALIBRATION_TARGET_COUNT];

lv_obj_t *validationCrosshair = nullptr;
bool touchDebugWasPressed = false;
unsigned long lastTouchDbgPrintMs = 0;
constexpr unsigned long TOUCH_DBG_PRINT_INTERVAL_MS = 200;  // rate-limit while held, ~5Hz

const char *screenName(SunnyUIScreen screen) {
  switch (screen) {
    case SunnyUIScreen::BRING_UP_TAP_TEST:
      return "BRING_UP_TAP_TEST";
    case SunnyUIScreen::CALIBRATION:
      return "CALIBRATION";
    case SunnyUIScreen::TOUCH_VALIDATION:
      return "TOUCH_VALIDATION";
    case SunnyUIScreen::HOME:
      return "HOME";
    case SunnyUIScreen::AUDIO:
      return "AUDIO";
    case SunnyUIScreen::MOTION:
      return "MOTION";
    case SunnyUIScreen::LEDS:
      return "LEDS";
    case SunnyUIScreen::DIAGNOSTICS:
      return "DIAGNOSTICS";
    case SunnyUIScreen::SETTINGS:
      return "SETTINGS";
  }
  return "UNKNOWN";
}

// Generic stand-in for every SunnyUIScreen value without a real
// implementation yet -- see UIState.h's isScreenImplemented(). Only
// enough to prove the navigation dispatcher routes correctly; a real
// screen replaces this when it's actually built (V1.2.3+).
void showPlaceholderScreen(SunnyUIScreen screen) {
  lv_obj_t *screenObj = lv_screen_active();
  lv_obj_set_style_bg_color(screenObj, lv_color_black(), 0);

  lv_obj_t *title = lv_label_create(screenObj);
  lv_label_set_text(title, screenName(screen));
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *notice = lv_label_create(screenObj);
  lv_label_set_text(notice, "Not yet implemented (V1.2.3+)");
  lv_obj_set_style_text_color(notice, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_align(notice, LV_ALIGN_CENTER, 0, 0);

  Serial.printf("[SCREEN] Placeholder screen shown: %s\n", screenName(screen));
}

void tapButtonEventCallback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_target(e));

  if (code == LV_EVENT_PRESSED) {
    bringUpTapTestOnPressStart(&tapState);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    Serial.printf("[TOUCH] Tap event #%lu (button pressed)\n", (unsigned long)tapState.tapCount);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    bringUpTapTestOnPressEnd(&tapState);
    lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
  }

  if (tapButtonLabel != nullptr) {
    lv_label_set_text_fmt(tapButtonLabel, "TAP TEST (%lu)", (unsigned long)tapState.tapCount);
  }
}

// Shared between showBringUpTapTestScreen() and showTouchValidationScreen()
// -- both screens retain the TAP TEST button, per V1.2.2's requirement.
void buildTapTestButton(lv_obj_t *parent, int32_t width, int32_t height, lv_align_t align, int32_t xOfs,
                         int32_t yOfs) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, width, height);
  lv_obj_align(btn, align, xOfs, yOfs);
  lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
  lv_obj_add_event_cb(btn, tapButtonEventCallback, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(btn, tapButtonEventCallback, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(btn, tapButtonEventCallback, LV_EVENT_PRESS_LOST, nullptr);

  tapButtonLabel = lv_label_create(btn);
  lv_label_set_text_fmt(tapButtonLabel, "TAP TEST (%lu)", (unsigned long)tapState.tapCount);
  lv_obj_center(tapButtonLabel);
}

// showTouchValidationScreen()'s per-target hitbox callback -- identifies
// which target was hit (via user_data -> ValidationTargetCtx), prints the
// LVGL-reported (already calibrated) screen coordinates plus the
// concurrent raw ADC reading, and highlights that target's visible dot
// (a separate, smaller child object -- the callback fires on the larger
// invisible hitbox, see showTouchValidationScreen()). This exercises the
// real, applied calibration constants (TouchManager.cpp's CAL_*) through
// the normal LVGL indev path -- it does NOT re-derive or sample
// calibration data itself (see CalibrationManager.cpp for that).
void validationTargetEventCallback(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  auto *ctx = static_cast<ValidationTargetCtx *>(lv_event_get_user_data(e));
  if (ctx == nullptr || ctx->dot == nullptr) return;

  if (code == LV_EVENT_PRESSED) {
    lv_obj_set_style_bg_color(ctx->dot, lv_palette_main(LV_PALETTE_GREEN), 0);

    lv_indev_t *indev = lv_indev_active();
    lv_point_t p{0, 0};
    if (indev != nullptr) lv_indev_get_point(indev, &p);

    int32_t rawX = 0, rawY = 0;
    bool haveRaw = readRawTouchPointForCalibration(&rawX, &rawY);
    if (haveRaw) {
      Serial.printf("[VALIDATE] target=%s screen=(%d,%d) raw=(%ld,%ld)\n", calibrationTargetName(ctx->id), (int)p.x,
                    (int)p.y, (long)rawX, (long)rawY);
    } else {
      Serial.printf("[VALIDATE] target=%s screen=(%d,%d)\n", calibrationTargetName(ctx->id), (int)p.x, (int)p.y);
    }
    if (validationStatusLabel != nullptr) {
      lv_label_set_text_fmt(validationStatusLabel, "HIT: %s screen=(%d,%d)", calibrationTargetName(ctx->id),
                             (int)p.x, (int)p.y);
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    lv_obj_set_style_bg_color(ctx->dot, lv_palette_main(LV_PALETTE_RED), 0);
  }
}

// Per-tick diagnostic driver for the touch-validation screen (V1.2.2 edge-
// validation investigation). Runs regardless of whether any LVGL target
// was hit -- reads the active indev's last point/state directly (the same
// data LVGL's own dispatch used), so it sees every touch, including ones
// that land outside every hitbox. Draws a temporary crosshair at the
// mapped screen point and rate-limit-prints raw+mapped coordinates. Purely
// diagnostic -- does not read, derive, or change any calibration constant.
void updateTouchValidationScreen() {
  lv_indev_t *indev = lv_indev_active();
  if (indev == nullptr || validationCrosshair == nullptr) return;

  lv_point_t p{0, 0};
  lv_indev_get_point(indev, &p);
  bool pressed = (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED);

  if (pressed) {
    lv_obj_clear_flag(validationCrosshair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(validationCrosshair, p.x - VALIDATION_CROSSHAIR_SIZE_PX / 2,
                    p.y - VALIDATION_CROSSHAIR_SIZE_PX / 2);

    unsigned long now = millis();
    if (!touchDebugWasPressed || (now - lastTouchDbgPrintMs >= TOUCH_DBG_PRINT_INTERVAL_MS)) {
      int32_t rawX = 0, rawY = 0;
      if (readRawTouchPointForCalibration(&rawX, &rawY)) {
        Serial.printf("[TOUCHDBG] raw=(%ld,%ld) mapped=(%d,%d)\n", (long)rawX, (long)rawY, (int)p.x, (int)p.y);
      }
      lastTouchDbgPrintMs = now;
    }
  } else {
    lv_obj_add_flag(validationCrosshair, LV_OBJ_FLAG_HIDDEN);
  }
  touchDebugWasPressed = pressed;
}
}  // namespace

void showBringUpTapTestScreen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "SUNNY V1.2");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  lv_obj_t *subtitle = lv_label_create(screen);
  lv_label_set_text(subtitle, "DISPLAY ONLINE");
  lv_obj_set_style_text_color(subtitle, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 34);

  lv_obj_t *resLabel = lv_label_create(screen);
  lv_label_set_text_fmt(resLabel, "Resolution: %dx%d", LCD_HEIGHT, LCD_WIDTH);
  lv_obj_set_style_text_color(resLabel, lv_color_white(), 0);
  lv_obj_align(resLabel, LV_ALIGN_TOP_MID, 0, 60);

  lv_obj_t *touchLabel = lv_label_create(screen);
  lv_label_set_text_fmt(touchLabel, "Touch: %s", isTouchManagerReady() ? "ONLINE" : "OFFLINE");
  lv_obj_set_style_text_color(touchLabel, isTouchManagerReady() ? lv_palette_main(LV_PALETTE_GREEN)
                                                                 : lv_palette_main(LV_PALETTE_RED),
                               0);
  lv_obj_align(touchLabel, LV_ALIGN_TOP_MID, 0, 82);

  buildTapTestButton(screen, 160, 60, LV_ALIGN_CENTER, 0, 30);

  Serial.println(F("[SCREEN] Bring-up/tap-test screen shown"));
}

void showTouchValidationScreen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "TOUCH VALIDATION");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  validationStatusLabel = lv_label_create(screen);
  lv_label_set_text(validationStatusLabel, "Tap each red target");
  lv_obj_set_style_text_color(validationStatusLabel, lv_palette_main(LV_PALETTE_YELLOW), 0);
  lv_obj_align(validationStatusLabel, LV_ALIGN_TOP_MID, 0, 22);

  for (int i = 0; i < CALIBRATION_TARGET_COUNT; i++) {
    auto id = static_cast<CalibrationTargetId>(i);
    int32_t x = 0, y = 0;
    calibrationTargetScreenPoint(id, LCD_HEIGHT, LCD_WIDTH, &x, &y);

    // Larger invisible/transparent hitbox -- the target position doesn't
    // move, but the tappable area around it is much bigger than the
    // visible dot, so a near-miss on a real resistive panel still
    // registers. Edge targets sitting close to the screen border simply
    // get clamped to stay fully on-screen (LVGL positions from the
    // object's top-left, so this only matters at the very edges).
    lv_obj_t *hitbox = lv_obj_create(screen);
    lv_obj_remove_style_all(hitbox);
    lv_obj_set_size(hitbox, VALIDATION_HITBOX_SIZE_PX, VALIDATION_HITBOX_SIZE_PX);
    lv_obj_set_style_bg_opa(hitbox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hitbox, 0, 0);
    lv_obj_add_flag(hitbox, LV_OBJ_FLAG_CLICKABLE);
    int32_t hitboxX = x - VALIDATION_HITBOX_SIZE_PX / 2;
    int32_t hitboxY = y - VALIDATION_HITBOX_SIZE_PX / 2;
    if (hitboxX < 0) hitboxX = 0;
    if (hitboxY < 0) hitboxY = 0;
    if (hitboxX + VALIDATION_HITBOX_SIZE_PX > LCD_HEIGHT) hitboxX = LCD_HEIGHT - VALIDATION_HITBOX_SIZE_PX;
    if (hitboxY + VALIDATION_HITBOX_SIZE_PX > LCD_WIDTH) hitboxY = LCD_WIDTH - VALIDATION_HITBOX_SIZE_PX;
    lv_obj_set_pos(hitbox, hitboxX, hitboxY);

    // Visible dot stays exactly at the true target coordinate (child of
    // the hitbox, centered within it only if the hitbox wasn't clamped --
    // position it independently at (x,y) so the target itself never
    // moves, only the tap tolerance around it grows).
    lv_obj_t *dot = lv_obj_create(screen);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, VALIDATION_DOT_DIAMETER_PX, VALIDATION_DOT_DIAMETER_PX);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);  // hitbox above receives events, not this visual dot
    lv_obj_set_pos(dot, x - VALIDATION_DOT_DIAMETER_PX / 2, y - VALIDATION_DOT_DIAMETER_PX / 2);

    validationCtx[i] = {id, dot};
    void *userData = &validationCtx[i];
    lv_obj_add_event_cb(hitbox, validationTargetEventCallback, LV_EVENT_PRESSED, userData);
    lv_obj_add_event_cb(hitbox, validationTargetEventCallback, LV_EVENT_RELEASED, userData);
    lv_obj_add_event_cb(hitbox, validationTargetEventCallback, LV_EVENT_PRESS_LOST, userData);
  }

  // Temporary diagnostic crosshair -- shows exactly where the firmware
  // currently thinks a touch is mapped to, independent of any target
  // hitbox. Created last so it renders on top of everything else. Hidden
  // until the first touch; see updateTouchValidationScreen().
  validationCrosshair = lv_obj_create(screen);
  lv_obj_remove_style_all(validationCrosshair);
  lv_obj_set_size(validationCrosshair, VALIDATION_CROSSHAIR_SIZE_PX, VALIDATION_CROSSHAIR_SIZE_PX);
  lv_obj_set_style_radius(validationCrosshair, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(validationCrosshair, lv_palette_main(LV_PALETTE_CYAN), 0);
  lv_obj_set_style_bg_opa(validationCrosshair, LV_OPA_COVER, 0);
  lv_obj_remove_flag(validationCrosshair, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(validationCrosshair, LV_OBJ_FLAG_HIDDEN);
  touchDebugWasPressed = false;

  // TAP TEST retained (required), placed below the CENTER target so the two
  // don't visually/hit-test overlap (CENTER dot sits at true screen center;
  // this button is offset further down).
  buildTapTestButton(screen, 120, 36, LV_ALIGN_CENTER, 0, 60);

  Serial.println(F("[SCREEN] Touch validation screen shown (edge-validation diagnostics active: "
                    "[TOUCHDBG] raw+mapped, enlarged hitboxes, on-screen crosshair)"));
}

void showScreen(SunnyUIScreen screen) {
  currentScreen = screen;
  lv_obj_clean(lv_screen_active());  // drop the previous screen's widgets before building the new one

  switch (screen) {
    case SunnyUIScreen::BRING_UP_TAP_TEST:
      showBringUpTapTestScreen();
      break;
    case SunnyUIScreen::CALIBRATION:
      showCalibrationScreen();
      break;
    case SunnyUIScreen::TOUCH_VALIDATION:
      showTouchValidationScreen();
      break;
    default:
      showPlaceholderScreen(screen);
      break;
  }
}

void updateActiveScreen() {
  if (currentScreen == SunnyUIScreen::CALIBRATION) {
    updateCalibrationScreen();
  } else if (currentScreen == SunnyUIScreen::TOUCH_VALIDATION) {
    updateTouchValidationScreen();
  }
}

SunnyUIScreen getCurrentScreen() { return currentScreen; }
