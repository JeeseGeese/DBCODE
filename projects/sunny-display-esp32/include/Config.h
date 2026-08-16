#pragma once

// Centralized pin/tunable definitions for the Sunny UI controller
// (ELEGOO ESP32 2.8" Touch Display, ESP32-WROOM-32E, ILI9341 + XPT2046
// resistive touch). Every pin below is CONFIRMED against LCDWIKI's
// official "2.8inch ESP32-32E E32R28T&E32N28T User Manual" schematic
// (exact model match to the physical board -- see docs/DISPLAY_HARDWARE.md
// for the full citation and evidence trail). None of these are guessed or
// inherited from a generic "Cheap Yellow Display" tutorial.

// ============================================================================
// ILI9341 LCD (hardware SPI)
// ============================================================================
constexpr int LCD_PIN_CS = 15;
constexpr int LCD_PIN_DC = 2;    // D/C ("RS") -- data/command select
constexpr int LCD_PIN_SCLK = 14;
constexpr int LCD_PIN_MOSI = 13;
constexpr int LCD_PIN_MISO = 12;
// No dedicated LCD reset GPIO -- the panel's reset line is tied to the
// ESP32 module's own EN/reset line (confirmed: schematic's "18P LCD panel
// wire welding interface" ties LCD RESET to the same net as the module
// RESET circuit, not a separate controllable pin).
constexpr int LCD_PIN_RESET = -1;  // -1 = "shared with system reset, not GPIO-controlled"
// Active-HIGH via an onboard BSS138 N-channel FET switching the backlight
// LED's cathode to ground. PWM-capable (confirmed in the manual: "the
// LCD_BL pin can input PWM signal to adjust the LCD backlight"). Default
// state after boot is OFF -- firmware must explicitly drive this pin.
constexpr int LCD_PIN_BACKLIGHT = 21;

constexpr int LCD_WIDTH = 240;
constexpr int LCD_HEIGHT = 320;

// ============================================================================
// XPT2046 resistive touch controller (hardware SPI, fully independent
// pins from the LCD's SPI bus -- not shared, no bus-contention concern)
// ============================================================================
constexpr int TOUCH_PIN_CS = 33;
constexpr int TOUCH_PIN_SCLK = 25;
constexpr int TOUCH_PIN_MOSI = 32;  // XPT2046 DIN (ESP32 -> touch controller)
constexpr int TOUCH_PIN_MISO = 39;  // XPT2046 DOUT (touch controller -> ESP32); input-only pin (SENSOR_VN/ADC1_CH3)
// PEN/IRQ -- active LOW while a touch is in contact (confirmed in the
// manual's resistive-touch-circuit description). Input-only pin
// (SENSOR_VP/ADC1_CH0).
constexpr int TOUCH_PIN_IRQ = 36;

// ============================================================================
// MicroSD card (SPI) -- shares its bus with the "SPI peripheral" expansion
// header (P3); NOT used by Phase 1 bring-up, listed here for completeness/
// future use only.
// ============================================================================
constexpr int SD_PIN_CS = 5;
constexpr int SD_PIN_MOSI = 23;
constexpr int SD_PIN_SCLK = 18;
constexpr int SD_PIN_MISO = 19;

// ============================================================================
// Onboard RGB status LED -- common anode, active-LOW (drive a channel LOW
// to light it, HIGH to turn it off). Pin SET confirmed via schematic
// (Figure 3.17); the exact per-channel color assignment below is
// HIGH-CONFIDENCE, not independently re-verified against a labeled
// silkscreen -- do not rely on exact color if it matters for a specific
// diagnostic (see docs/DISPLAY_HARDWARE.md).
// ============================================================================
constexpr int RGB_LED_PIN_R = 17;
constexpr int RGB_LED_PIN_G = 22;
constexpr int RGB_LED_PIN_B = 16;

// ============================================================================
// Onboard audio (FM8002E amplifier + mono speaker terminal) -- not used by
// Phase 1 bring-up, listed for completeness/future use only.
// ============================================================================
constexpr int AUDIO_PIN_ENABLE = 4;   // amplifier enable, active per FM8002E SHUTDOWN pin (manual: "low level enabled by circuit design; high by default")
constexpr int AUDIO_PIN_DAC_OUT = 26; // ESP32 DAC output feeding the amplifier's AUDIO_IN

// ============================================================================
// Battery voltage sense (ADC, input-only pin) -- reads BAT+ through a
// 100K/100K divider, so the raw ADC-derived voltage must be multiplied by
// 2 to get the actual battery voltage (confirmed in the manual).
// ============================================================================
constexpr int BATTERY_PIN_ADC = 34;

// ============================================================================
// Onboard buttons -- fixed-function, not general-purpose GPIOs to reuse:
// BOOT (IO0) is a standard ESP32 boot-strapping pin (pulled LOW at reset
// to enter the ROM download mode) -- never repurpose as an ordinary
// output. RESET is the module's hardware EN line via a discrete RC/button
// circuit, not a GPIO at all.
// ============================================================================
constexpr int BOOT_BUTTON_PIN = 0;  // read-only awareness; do not drive this pin as an output

// ============================================================================
// UART0 (console/programming) -- CONFIRMED shared between the onboard
// USB-C/CH340C circuit AND the external 4-pin "Serial Port" header (P2).
// Do NOT use the external serial connector for a future body-controller
// link without accepting that it contends with the USB programming/
// monitor console -- see docs/DISPLAY_HARDWARE.md's "Future ESP32-to-
// ESP32 communication" section.
// ============================================================================
constexpr int UART0_PIN_TX = 1;
constexpr int UART0_PIN_RX = 3;

// ============================================================================
// Expansion header pins -- the ONLY pins confirmed genuinely free/unused
// by any onboard peripheral (per the manual's "Expand IO and peripheral
// interface circuits" section). Everything else on this board's exposed
// headers duplicates a pin already listed above (SD/SPI-peripheral bus).
// ============================================================================
constexpr int EXPANSION_PIN_SPARE_CS = 27;    // P3 header, genuinely unused elsewhere, bidirectional-capable
constexpr int EXPANSION_PIN_SPARE_INPUT = 35; // P4 header, INPUT-ONLY (confirmed in manual: "IO35 can only be input")
