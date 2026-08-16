#include "DisplayManager.h"

#include <Arduino.h>
#include <lvgl.h>

#include "Config.h"
#include "LGFXDevice.h"

// ----------------------------------------------------------------------------
// Sole owner of the SunnyLGFX hardware device object (see LGFXDevice.h) --
// no other file in this project touches _lgfx directly.
//
// Draw buffer: partial-render, NOT a full framebuffer. This module has no
// PSRAM (confirmed -- see docs/DISPLAY_HARDWARE.md), so a full 240x320x2
// (RGB565) framebuffer (153,600 bytes) is avoided in favor of two small
// rotating strip buffers covering LCD_WIDTH x DRAW_BUFFER_LINES pixels
// each -- LVGL redraws in bands, calling flush_cb once per band. 30 lines
// (28,800 bytes total) overflowed this chip's DRAM segment by 8,248 bytes
// once LVGL's other default-enabled widgets were linked in (see
// docs/DISPLAY_HARDWARE.md's "LVGL configuration" section for why widgets
// were left at library defaults rather than hand-pruned) -- 15 lines
// (14,400 bytes total) was the fix; see the memory-plan section of
// docs/DISPLAY_HARDWARE.md for the full measured RAM/Flash numbers.
// ----------------------------------------------------------------------------

namespace {
SunnyLGFX sunnyLgfx;

constexpr int DRAW_BUFFER_LINES = 15;  // ~1/16th of the 240px logical width per buffer -- see comment above
lv_color_t drawBuf1[LCD_WIDTH * DRAW_BUFFER_LINES];
lv_color_t drawBuf2[LCD_WIDTH * DRAW_BUFFER_LINES];

lv_display_t *lvDisplay = nullptr;
bool displayReady = false;

void lvglFlushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  sunnyLgfx.startWrite();
  sunnyLgfx.setAddrWindow(area->x1, area->y1, w, h);
  sunnyLgfx.writePixels(reinterpret_cast<lgfx::rgb565_t *>(px_map), w * h);
  sunnyLgfx.endWrite();
  lv_display_flush_ready(disp);
}

// Routes LVGL's own internal log lines to Serial, prefixed for easy
// filtering against the rest of this project's [DISPLAY]/[TOUCH] boot log.
void lvglLogCallback(lv_log_level_t level, const char *buf) {
  (void)level;
  Serial.printf("[LVGL] %s", buf);
}
}  // namespace

bool initDisplayManager() {
  Serial.println(F("[DISPLAY] Initializing ILI9341 (LovyanGFX)..."));

  if (!sunnyLgfx.init()) {
    Serial.println(F("[DISPLAY] ERROR: panel init failed"));
    displayReady = false;
    return false;
  }
  sunnyLgfx.setRotation(3);  // landscape, 320x240 logical, 180 degrees from rotation 1 -- physical bring-up on
                              // 2026-08-08 found rotation 1 upside down; see docs/DISPLAY_HARDWARE.md
  sunnyLgfx.setBrightness(0);  // stay dark until setBacklightBrightness() is explicitly called

  lv_init();
  lv_log_register_print_cb(lvglLogCallback);

  lvDisplay = lv_display_create(LCD_HEIGHT, LCD_WIDTH);  // width/height swapped to match rotation(3) landscape
  lv_display_set_flush_cb(lvDisplay, lvglFlushCallback);
  lv_display_set_buffers(lvDisplay, drawBuf1, drawBuf2, sizeof(drawBuf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);

  Serial.printf("[DISPLAY] Panel ready: %dx%d (logical, post-rotation)\n", LCD_HEIGHT, LCD_WIDTH);
  displayReady = true;
  return true;
}

void updateDisplayManager(unsigned long nowMs) {
  if (!displayReady) return;
  static unsigned long lastTickMs = 0;
  lv_tick_inc(nowMs - lastTickMs);
  lastTickMs = nowMs;
  lv_timer_handler();
}

void setBacklightBrightness(uint8_t brightness0to255) {
  if (!displayReady) return;
  sunnyLgfx.setBrightness(brightness0to255);
}

bool isDisplayManagerReady() { return displayReady; }

void printDisplayManagerStatus() {
  Serial.printf("[DISPLAY] ready=%d resolution=%dx%d rotation=3\n", displayReady ? 1 : 0, LCD_HEIGHT, LCD_WIDTH);
}

bool readRawTouchPoint(int32_t *outRawX, int32_t *outRawY) {
  if (!displayReady) return false;
  lgfx::touch_point_t tp;
  int32_t n = sunnyLgfx.getTouchRaw(&tp, 1);
  if (n == 0) return false;
  *outRawX = tp.x;
  *outRawY = tp.y;
  return true;
}
