#include <Arduino.h>

#include "DisplayManager.h"
#include "Screens.h"
#include "TouchManager.h"
#include "UIState.h"

// Sunny UI controller -- V1.2.2 (touch calibration + LVGL navigation
// foundation). See docs/DISPLAY_HARDWARE.md for the full architecture.
// This file initializes the subsystems and services them every loop()
// tick; it contains no screen-building or hardware-driver logic itself
// (see DisplayManager.cpp/TouchManager.cpp/Screens.cpp/CalibrationManager.cpp).
//
// STANDALONE-ONLY as of Phase 1: this board must run on its own USB-C
// power/programming connection. Do not wire any Sunny body-controller
// (ESP32-S3) UART/power/ground to this board yet -- see
// docs/DISPLAY_HARDWARE.md's "Standalone-first safety rule".

void setup() {
  Serial.begin(115200);
  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) delay(10);
  delay(300);

  Serial.println(F("[SYSTEM] Sunny UI controller starting (Phase 1 -- display/touch bring-up)"));
  Serial.println(F("[SYSTEM] STANDALONE MODE -- no connection to the Sunny body ESP32-S3 controller"));

  if (!initDisplayManager()) {
    Serial.println(F("[SYSTEM] FATAL: display init failed, halting"));
    while (true) delay(1000);
  }
  setBacklightBrightness(200);  // explicit -- backlight defaults OFF at power-on, see Config.h

  initTouchManager();

  // V1.2.2: touch is now calibrated with physically-measured constants
  // (see TouchManager.cpp) -- boot into the normal touch-validation
  // screen. showScreen(CALIBRATION) remains available as a diagnostic/
  // manual mode for future recalibration; showScreen(BRING_UP_TAP_TEST)
  // remains available for reference/regression (see Screens.h). Neither
  // is the default here.
  showScreen(SunnyUIScreen::TOUCH_VALIDATION);

  printDisplayManagerStatus();
  printTouchManagerStatus();
}

void loop() {
  unsigned long now = millis();
  updateDisplayManager(now);
  updateActiveScreen();
  delay(5);  // LVGL's own recommendation: a small yield between timer_handler() calls
}
