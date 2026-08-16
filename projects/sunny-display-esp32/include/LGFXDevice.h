#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "Config.h"

// The LovyanGFX hardware device definition -- ties together the SPI bus,
// ILI9341 panel, backlight PWM channel, and XPT2046 touch controller into
// one lgfx::LGFX_Device subclass, per LovyanGFX's own established
// architecture (panel and touch share one device/bus configuration
// object in this library, not two independent objects -- this is a
// library-imposed constraint, not a choice to merge DISPLAY HARDWARE and
// TOUCH HARDWARE responsibilities at the module level).
//
// Ownership: DisplayManager.cpp is the sole owner of the ONE instance of
// this class (see its .cpp file) and the sole caller of its panel/
// backlight methods. TouchManager.cpp reads touch state ONLY through
// DisplayManager's narrow raw-touch accessor (see DisplayManager.h) --
// it never touches this class directly. This preserves the DISPLAY
// HARDWARE / TOUCH HARDWARE separation at the responsibility level even
// though the underlying library ties the SPI objects together.
class SunnyLGFX : public lgfx::LGFX_Device {
 public:
  SunnyLGFX() {
    // --- SPI bus for the ILI9341 panel ---
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;  // 40MHz -- standard, well-proven rate for ILI9341 over short on-board traces
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = LCD_PIN_SCLK;
      cfg.pin_mosi = LCD_PIN_MOSI;
      cfg.pin_miso = LCD_PIN_MISO;
      cfg.pin_dc = LCD_PIN_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    // --- ILI9341 panel itself ---
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = LCD_PIN_CS;
      cfg.pin_rst = -1;  // no dedicated reset GPIO -- shared with system EN, see Config.h
      cfg.pin_busy = -1;
      cfg.panel_width = LCD_WIDTH;
      cfg.panel_height = LCD_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;  // this bus is dedicated to the panel only (touch is on separate pins entirely)
      _panel_instance.config(cfg);
    }

    // --- Backlight (PWM-capable, active-HIGH, off by default) ---
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = LCD_PIN_BACKLIGHT;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;  // pick a channel unlikely to collide with anything else this project adds later
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    // --- XPT2046 touch -- fully independent SPI pins from the panel bus
    // (see Config.h), so it gets its own bus config rather than sharing
    // the panel's, even though LovyanGFX supports bus-sharing for boards
    // where touch and panel DO share pins.
    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = 4095;  // raw XPT2046 12-bit ADC range -- see TouchManager for the real calibrated range
      cfg.y_min = 0;
      cfg.y_max = 4095;
      cfg.pin_int = TOUCH_PIN_IRQ;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      cfg.spi_host = HSPI_HOST;  // separate hardware SPI peripheral from the panel's VSPI
      cfg.freq = 1000000;        // XPT2046 is a slow ADC part -- 1MHz is the conventional safe rate
      cfg.pin_sclk = TOUCH_PIN_SCLK;
      cfg.pin_mosi = TOUCH_PIN_MOSI;
      cfg.pin_miso = TOUCH_PIN_MISO;
      cfg.pin_cs = TOUCH_PIN_CS;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }

 private:
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;
};
