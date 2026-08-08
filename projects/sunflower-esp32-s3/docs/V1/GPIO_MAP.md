# Sunny V1 — GPIO Map

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/GPIO_MAP.md`.

Single source of truth for every GPIO assignment as of the V1 baseline.
Verified against `include/Config.h` and `include/MotorDriver.h` source
(not assumed from older docs — see the note at the bottom about a stale
comment found and fixed during the V1 audit).

| Signal | GPIO | Owner (sole) | Notes |
|---|---|---|---|
| WS2812 LED data (DIN) | 4 | `main.cpp`'s single `strip` object | 3.3V logic-level data only — never power the strip from this pin |
| Mode button | 10 | `Controls.cpp` | `INPUT_PULLUP`, wired to GND |
| Mute button | 11 | `Controls.cpp` | `INPUT_PULLUP`, wired to GND |
| Brightness button | 17 | `Controls.cpp` | `INPUT_PULLUP`, wired to GND |
| Button 4 (Audio) | 5 | `Controls.cpp` | `INPUT_PULLUP`, wired to GND; short press = LED overlay toggle, long hold = unified Audio Mode toggle |
| INMP441 BCLK / MAX98357A BCLK | 6 | `SharedI2S.cpp` (`I2S_NUM_0`) | shared full-duplex master port — see `I2S_ARCHITECTURE.md` |
| INMP441 WS / MAX98357A LRC | 7 | `SharedI2S.cpp` (`I2S_NUM_0`) | shared full-duplex master port |
| INMP441 SD (I2S data IN) | 15 | `SharedI2S.cpp` / `AudioAnalyzer.cpp` reads | mic → ESP32 |
| MAX98357A DIN (I2S data OUT) | 16 | `SharedI2S.cpp` / `SpeakerTest.cpp` writes | ESP32 → amplifier |
| DRV8833 IN1 (motor) | 8 | `MotorDriver.cpp` (sole owner of all GPIO8/9 access) | `digitalWrite` normally; briefly attached to an LEDC PWM channel only while `MotorPwmCalibration` owns it |
| DRV8833 IN2 (motor) | 9 | `MotorDriver.cpp` | same as IN1 |
| DRV8833 SLEEP/nSLEEP | — | not wired / not driven by firmware | |

**Unassigned — do not invent a pin number for these without a hardware
audit**: future Button 5/6, a conversation-enable switch, a
voice-prompt-enable switch. See "Reserved / excluded pins" below for
what any such audit must avoid.

## Reserved / excluded pins (ESP32-S3-WROOM N16R8)

Before assigning any new GPIO (Raspberry Pi comms, camera trigger,
future buttons/switches), exclude:

- Every pin already in the table above.
- Flash/PSRAM-dedicated pins on the N16R8 module (octal PSRAM uses
  additional GPIOs beyond the standard quad-SPI set — verify against
  the exact WROOM-N16R8 pinout before assuming a "free" pin is
  actually free on this module variant, not a generic ESP32-S3).
- USB/JTAG-sensitive pins (native USB-OTG D+/D-, if ever used — this
  board bridges UART0 via an external CH343 chip instead, so native USB
  pins are currently unused, but avoid repurposing them without
  checking `ARDUINO_USB_MODE` implications).
- Boot-strapping pins (pins sampled at reset to select boot mode —
  driving these incorrectly at boot can prevent flashing/booting).

See `docs/playbooks/GPIO_VALIDATION.md` for the general procedure to
audit a candidate pin before wiring it.

## Known documentation staleness fixed during the V1 audit

- `include/MotorDriver.h`'s header comment previously stated "ESP32 3.3V
  → DRV8833 VCC". This was correct for the ORIGINAL bring-up wiring but
  became stale after the 2026-08-01 power-architecture correction (the
  DRV8833, like the LEDs and amplifier, actually runs from the shared
  5V rail — see `POWER.md`). The comment has been corrected to match
  verified reality as part of this cleanup (a documentation-only fix,
  zero runtime behavior change).
- `docs/DRV8833_MOTOR_BRINGUP.md` section 11 and
  `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 24 both still describe
  the pre-correction 3.3V claim / a pre-`SharedI2S` GPIO table. These are
  historical documents describing what was true/believed *at the time
  they were written* — they have been archived as-is (not rewritten)
  with a pointer to this file for the current truth. See
  `archive/README.md`.
