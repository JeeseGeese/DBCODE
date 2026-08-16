#pragma once
#include <stdint.h>

// Sole owner of the ILI9341 LCD hardware (SPI bus, panel, backlight) and
// the LVGL display driver registration/flush callback. Mirrors this
// repository's established single-owner-resource convention (see
// projects/sunflower-esp32-s3/AGENTS.md) -- nothing outside this module
// touches the LCD SPI bus or the backlight pin directly.
//
// Does NOT own touch input (see TouchManager.h) or UI screen content (see
// Screens.h) -- this module is hardware init + the LVGL flush bridge only,
// per the DISPLAY HARDWARE / TOUCH HARDWARE / UI STATE / UI SCREENS
// separation this project uses.

// Initializes the LGFX device (SPI bus, ILI9341 panel, backlight PWM),
// then registers an LVGL display driver backed by a partial draw buffer
// (no PSRAM on this module -- see docs/DISPLAY_HARDWARE.md's memory plan
// for the exact buffer size and why). Must be called once, before any
// lv_* screen/widget code runs. Returns false if LGFX panel init fails.
bool initDisplayManager();

// Must be called every loop() iteration. Feeds LVGL's tick counter from
// millis() and calls lv_timer_handler() to service pending draw/animation
// work. Non-blocking -- LVGL's own timer handler returns promptly even
// when there's nothing to redraw.
void updateDisplayManager(unsigned long nowMs);

// Backlight control -- PWM-capable (see Config.h's LCD_PIN_BACKLIGHT).
// 0 = off, 255 = full brightness. Off by default at boot (matches the
// hardware's own power-on default -- see Config.h's comment).
void setBacklightBrightness(uint8_t brightness0to255);

bool isDisplayManagerReady();

// For a future diagnostics screen -- reports whether the panel
// initialized successfully and the configured resolution.
void printDisplayManagerStatus();

// --- Narrow raw-touch accessor for TouchManager.cpp only ---
// LovyanGFX ties the touch controller to the same hardware device object
// as the panel (see LGFXDevice.h's comment) -- this is the ONE function
// TouchManager.cpp calls to read raw, uncalibrated touch ADC counts and
// contact state. TouchManager owns all calibration/transform/LVGL-input-
// callback logic on top of this; DisplayManager does not interpret touch
// data itself. Returns false if no touch is currently detected.
bool readRawTouchPoint(int32_t *outRawX, int32_t *outRawY);
