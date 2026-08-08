# Sunny V1 — Hardware Architecture

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/HARDWARE_ARCHITECTURE.md`.

## MCU

**ESP32-S3-WROOM, N16R8 variant** — 16 MB flash, 8 MB octal PSRAM.
Generic ESP32-S3 devkit board, USB-C, with UART0 bridged to USB via an
onboard WCH CH343 chip (**not** the ESP32-S3's native USB-OTG
peripheral). This is why `platformio.ini` sets `ARDUINO_USB_MODE=0` —
it keeps `Serial` on UART0 so output reaches the CH343-bridged USB-C
port; the alternative (`USB_CDC_ON_BOOT`) would redirect `Serial` to
native USB pins (GPIO19/20) that aren't wired to a connector on this
board.

## Peripherals

| Peripheral | Summary | Detail doc |
|---|---|---|
| 58x WS2812B-compatible LEDs | single data line, GPIO4 | `LED_ENGINE.md`, `GPIO_MAP.md` |
| 4x momentary pushbuttons | `INPUT_PULLUP`, no external resistors | `BUTTONS.md` |
| INMP441 I2S MEMS microphone | 16kHz, LEFT channel | `MICROPHONE.md`, `I2S_ARCHITECTURE.md` |
| MAX98357A I2S amplifier + 40mm/4Ω/3W speaker | shares the mic's I2S bus | `SPEAKER.md`, `I2S_ARCHITECTURE.md` |
| DRV8833 H-bridge + brushed DC motor | digitalWrite + PWM control | `MOTOR.md` |

## System block diagram

```
                         ESP32-S3-WROOM (N16R8)
                                  |
      +--------------+--------------+--------------+--------------+
      |              |              |              |              |
   GPIO4          Buttons      I2S_NUM_0        GPIO8/9        (future)
  (LED data)   10/11/17/5    (full-duplex,     (DRV8833      Raspberry Pi
      |         INPUT_      shared master)      IN1/IN2)      companion
      v          PULLUP          |                 |
  58x WS2812        |     +------+------+           v
  (5V rail,         v     |             |      brushed DC
   common      Mode/Mute/ BCLK=6/WS=7   |        motor
   ground)     Bright/   |             |
               Button4    v             v
                       INMP441      MAX98357A
                    (SD=GPIO15,   (DIN=GPIO16,
                     3.3V rail,    5V rail,
                     RX only)      TX only)
                                       |
                                  40mm/4Ω/3W
                                    speaker
```

## Power rails (see `POWER.md` for the full detail and history)

```
Shared 5V rail:  WS2812 LEDs, MAX98357A amplifier, DRV8833 motor driver
3.3V rail:       INMP441 microphone only
Common ground:   across ESP32-S3 and all peripherals
```

ESP32 GPIO logic signals (I2S, buttons, LED data) are always 3.3V
regardless of a peripheral's own power rail.

## Verified vs. not-yet-verified

**Physically verified** (see `docs/V1/TESTING.md` for the source of
each claim):
- Four-button control, WS2812 base effects + audio-reactive overlay.
- Bidirectional DRV8833 motor control (forward/reverse/stop), the
  mechanical belt-preload fix, `MusicMotorController`'s M80 floor and
  timing table, Revision 10.1's deadlock fix.
- Shared I2S full-duplex bus initializes and runs (mic capture + write
  path both work); speaker output is audible with an identified usable
  amplitude range (see `SPEAKER.md`).

**Not yet physically verified**:
- `ExpressiveMotion` / `BehaviorEngine` movement tiers against the real
  mechanism (software-validated only — see `KNOWN_LIMITATIONS.md`).
- Combined LED + amplifier + motor peak current draw on the shared 5V
  rail under sustained/production conditions.
- The exact root cause of residual speaker buzz/static (see
  `SPEAKER.md`).
