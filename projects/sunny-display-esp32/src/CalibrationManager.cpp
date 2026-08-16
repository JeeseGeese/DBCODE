#include "CalibrationManager.h"

#include <Arduino.h>
#include <lvgl.h>

#include "Calibration.h"
#include "Config.h"
#include "TouchManager.h"

namespace {
// Logical screen size post-rotation(3) -- same swap convention already
// used by DisplayManager.cpp (lv_display_create(LCD_HEIGHT, LCD_WIDTH))
// and TouchManager.cpp (readTouchScreenPoint()).
constexpr int32_t SCREEN_W = LCD_HEIGHT;
constexpr int32_t SCREEN_H = LCD_WIDTH;

constexpr int SAMPLES_PER_TARGET_MAX = 12;
constexpr int SAMPLES_PER_TARGET_MIN = 4;  // below this on release, treat as an accidental tap and retry
constexpr float TRIM_FRACTION = 0.2f;
constexpr int32_t DOT_RADIUS_PX = 8;

int currentTargetIndex = 0;  // 0..CALIBRATION_TARGET_COUNT-1
int32_t sampleBufX[SAMPLES_PER_TARGET_MAX];
int32_t sampleBufY[SAMPLES_PER_TARGET_MAX];
int sampleCount = 0;
bool wasPressed = false;
bool complete = false;

// Averaged raw sample per target, indexed the same as CalibrationTargetId.
CalibrationRawSample targetAverages[CALIBRATION_TARGET_COUNT];

lv_obj_t *targetDot = nullptr;
lv_obj_t *instructionLabel = nullptr;
lv_obj_t *progressLabel = nullptr;
lv_obj_t *resultLabel = nullptr;

void moveDotToCurrentTarget() {
  int32_t x = 0, y = 0;
  calibrationTargetScreenPoint(static_cast<CalibrationTargetId>(currentTargetIndex), SCREEN_W, SCREEN_H, &x, &y);
  lv_obj_set_pos(targetDot, x - DOT_RADIUS_PX, y - DOT_RADIUS_PX);
  lv_label_set_text_fmt(progressLabel, "Target %d of %d: %s", currentTargetIndex + 1, CALIBRATION_TARGET_COUNT,
                         calibrationTargetName(static_cast<CalibrationTargetId>(currentTargetIndex)));
}

void printProposedConstants(const DerivedCalibration &d) {
  Serial.println(F("[CAL] ================= PROPOSED CALIBRATION ================="));
  Serial.printf("[CAL] swapAxes=%d scaleX=%.6f offsetX=%.4f scaleY=%.6f offsetY=%.4f\n", d.swapAxes ? 1 : 0,
                (double)d.scaleX, (double)d.offsetX, (double)d.scaleY, (double)d.offsetY);
  Serial.println(F("[CAL] Not applied automatically -- paste into TouchManager.cpp's calibration"));
  Serial.println(F("[CAL] constants after review:"));
  Serial.printf("[CAL]   constexpr bool CAL_SWAP_AXES = %s;\n", d.swapAxes ? "true" : "false");
  Serial.printf("[CAL]   constexpr float CAL_SCALE_X = %.8ff;\n", (double)d.scaleX);
  Serial.printf("[CAL]   constexpr float CAL_OFFSET_X = %.5ff;\n", (double)d.offsetX);
  Serial.printf("[CAL]   constexpr float CAL_SCALE_Y = %.8ff;\n", (double)d.scaleY);
  Serial.printf("[CAL]   constexpr float CAL_OFFSET_Y = %.5ff;\n", (double)d.offsetY);

  // Per-target accuracy self-check: run the just-derived transform (in
  // memory only, not applied to the real transform) against EVERY
  // target's own averaged raw sample and compare to its known true
  // screen position -- not just the center. The original min/max model's
  // bug hid behind a near-perfect center result while corners were off
  // by 30-42px, so checking only the center is not sufficient evidence.
  for (int i = 0; i < CALIBRATION_TARGET_COUNT; i++) {
    auto id = static_cast<CalibrationTargetId>(i);
    int32_t trueX = 0, trueY = 0;
    calibrationTargetScreenPoint(id, SCREEN_W, SCREEN_H, &trueX, &trueY);
    const CalibrationRawSample &raw = targetAverages[i];
    int32_t predictedX = 0, predictedY = 0;
    transformRawTouchToScreen(raw.rawX, raw.rawY, SCREEN_W, SCREEN_H, d.swapAxes, d.scaleX, d.offsetX, d.scaleY,
                               d.offsetY, &predictedX, &predictedY);
    Serial.printf("[CAL] %-13s raw=(%ld,%ld) predicted=(%ld,%ld) true=(%ld,%ld) delta=(%ld,%ld)\n",
                  calibrationTargetName(id), (long)raw.rawX, (long)raw.rawY, (long)predictedX, (long)predictedY,
                  (long)trueX, (long)trueY, (long)(predictedX - trueX), (long)(predictedY - trueY));
  }
  Serial.println(F("[CAL] ==========================================================="));
}

void finishCalibration() {
  int32_t targetScreenX[CALIBRATION_TARGET_COUNT];
  int32_t targetScreenY[CALIBRATION_TARGET_COUNT];
  for (int i = 0; i < CALIBRATION_TARGET_COUNT; i++) {
    calibrationTargetScreenPoint(static_cast<CalibrationTargetId>(i), SCREEN_W, SCREEN_H, &targetScreenX[i],
                                  &targetScreenY[i]);
  }
  // Fits from ALL 5 measured targets (4 corners + center), not just the
  // corners -- see Calibration.h's deriveCalibrationFromSamples().
  DerivedCalibration derived =
      deriveCalibrationFromSamples(targetAverages, targetScreenX, targetScreenY, CALIBRATION_TARGET_COUNT);

  printProposedConstants(derived);

  complete = true;
  lv_obj_add_flag(targetDot, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(instructionLabel, "CALIBRATION COMPLETE");
  lv_label_set_text(progressLabel, "Proposed constants printed to serial");
  lv_label_set_text(resultLabel, "See [CAL] lines -- not applied yet, pending review");
}

void finalizeCurrentTargetAndAdvance() {
  int32_t avgX = calibrationTrimmedMean(sampleBufX, sampleCount, TRIM_FRACTION);
  int32_t avgY = calibrationTrimmedMean(sampleBufY, sampleCount, TRIM_FRACTION);
  Serial.printf("[CAL] target=%d (%s) AVERAGE raw=(%ld,%ld) from %d samples\n", currentTargetIndex,
                calibrationTargetName(static_cast<CalibrationTargetId>(currentTargetIndex)), (long)avgX, (long)avgY,
                sampleCount);
  targetAverages[currentTargetIndex] = {avgX, avgY};
  sampleCount = 0;
  currentTargetIndex++;

  if (currentTargetIndex >= CALIBRATION_TARGET_COUNT) {
    finishCalibration();
  } else {
    moveDotToCurrentTarget();
  }
}
}  // namespace

void showCalibrationScreen() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "TOUCH CALIBRATION");
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  instructionLabel = lv_label_create(screen);
  lv_label_set_text(instructionLabel, "Tap and HOLD each red target");
  lv_obj_set_style_text_color(instructionLabel, lv_palette_main(LV_PALETTE_YELLOW), 0);
  lv_obj_align(instructionLabel, LV_ALIGN_TOP_MID, 0, 28);

  progressLabel = lv_label_create(screen);
  lv_obj_set_style_text_color(progressLabel, lv_color_white(), 0);
  lv_obj_align(progressLabel, LV_ALIGN_TOP_MID, 0, 46);

  resultLabel = lv_label_create(screen);
  lv_label_set_text(resultLabel, "");
  lv_obj_set_style_text_color(resultLabel, lv_palette_main(LV_PALETTE_GREEN), 0);
  lv_obj_align(resultLabel, LV_ALIGN_BOTTOM_MID, 0, -8);

  targetDot = lv_obj_create(screen);
  lv_obj_remove_style_all(targetDot);
  lv_obj_set_size(targetDot, DOT_RADIUS_PX * 2, DOT_RADIUS_PX * 2);
  lv_obj_set_style_radius(targetDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(targetDot, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_set_style_bg_opa(targetDot, LV_OPA_COVER, 0);

  currentTargetIndex = 0;
  sampleCount = 0;
  wasPressed = false;
  complete = false;
  moveDotToCurrentTarget();

  Serial.println(F("[SCREEN] Calibration screen shown"));
  Serial.printf("[CAL] Starting calibration -- %d targets, hold each until it advances\n", CALIBRATION_TARGET_COUNT);
}

void updateCalibrationScreen() {
  if (complete) return;

  int32_t rawX = 0, rawY = 0;
  bool pressed = readRawTouchPointForCalibration(&rawX, &rawY);

  if (pressed) {
    if (sampleCount < SAMPLES_PER_TARGET_MAX) {
      sampleBufX[sampleCount] = rawX;
      sampleBufY[sampleCount] = rawY;
      sampleCount++;
      Serial.printf("[CAL] target=%d sample=%d raw=(%ld,%ld)\n", currentTargetIndex, sampleCount, (long)rawX,
                    (long)rawY);
    }
    wasPressed = true;
  } else if (wasPressed) {
    if (sampleCount >= SAMPLES_PER_TARGET_MIN) {
      finalizeCurrentTargetAndAdvance();
    } else if (sampleCount > 0) {
      Serial.printf("[CAL] target=%d tap too short (%d samples, need >= %d) -- hold longer and try again\n",
                    currentTargetIndex, sampleCount, SAMPLES_PER_TARGET_MIN);
      sampleCount = 0;
    }
    wasPressed = false;
  }
}

bool isCalibrationComplete() { return complete; }
