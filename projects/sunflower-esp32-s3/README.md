# sunflower-esp32-s3

ESP32-S3 firmware project driving a 58-pixel WS2812B-compatible addressable
LED strip, controlled by four physical pushbuttons.

## Hardware

- **MCU:** ESP32-S3-WROOM module, N16R8 variant (16 MB flash, 8 MB octal PSRAM)
- **Board:** generic ESP32-S3 devkit, USB-C, UART0 bridged to USB via an
  onboard WCH CH343 chip (not the ESP32-S3's native USB-OTG peripheral)
- **LEDs:** 58x WS2812B-compatible addressable LEDs, single data line
- **Buttons:** 4x momentary pushbuttons, each wired between its GPIO pin
  and GND, using the ESP32's internal pull-up (no external resistors)
- **Power:** ESP32 5V pin and LED strip 5V share a common ground with the
  ESP32 GND pin

## GPIO assignments

| Signal | GPIO | Notes |
|---|---|---|
| LED data (DIN) | 4 | 3.3V logic-level data signal to LED strip |
| Mode button | 10 | `INPUT_PULLUP`, wired to GND — cycles LED mode |
| Mute button | 11 | `INPUT_PULLUP`, wired to GND — toggles LEDs off/on |
| Brightness button | 17 | `INPUT_PULLUP`, wired to GND — cycles brightness |
| Button 4 (print-only) | 5 | `INPUT_PULLUP`, wired to GND — logs press only |

GPIO45 was tried as a candidate spare pin during wiring diagnostics and
failed to respond even to a direct short to GND (likely not broken out to a
usable header pin on this board, or a bad physical connection) — it is not
used by this firmware.

## Build instructions

```
cd ~/DOBETTERCODE/DBCODE/projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"   # if pio isn't already on PATH
pio run
```

## Upload instructions

Connect the board via USB-C, then:

```
pio device list                          # confirm the port, typically /dev/ttyACM0
pio run -t upload --upload-port /dev/ttyACM0
```

If your user account isn't in the `dialout` group, prefix the upload command
with `sg dialout -c "..."` or add yourself to the group and re-login:

```
sudo usermod -aG dialout $USER
```

## Serial monitor instructions

```
pio device monitor -p /dev/ttyACM0 -b 115200
```

(Ctrl+C to exit.)

## Current LED controller behavior

On boot the firmware initializes all four buttons and the LED strip, and
starts in **Mode 0 (Off)**. Each button is independently debounced (40ms,
edge-triggered on stable HIGH→LOW) so presses on different buttons never
interfere with each other, and each press is a single mode/setting change
with no repeat-while-held.

**Mode button (GPIO10)** — advances to the next mode on each press,
wrapping from the last mode back to the first. 12 modes:

`Off, Solid Red, Solid Green, Solid Blue, Solid White (dim), Forward
Walking Pixel, Reverse Walking Pixel, Rainbow, Theater Chase, Breathing,
Twinkle, Larson Scanner`

Animated modes run on their own `millis()`-based timing in the main loop
(no `delay()`), so buttons stay responsive while an animation is running.

**Mute button (GPIO11)** — toggles the LEDs off/on without losing the
currently selected mode; unmuting re-renders exactly where the mode was
left off.

**Brightness button (GPIO17)** — cycles global brightness through
`10 → 15 → 20 → 10 ...`. Never reaches full brightness at any level,
including in "white" modes.

**Button 4 (GPIO5)** — currently print-only; logs `[BUTTON] Button 4
pressed` to Serial with no other effect. Reserved for future use.

All brightness levels stay well below full brightness (255) — even the
brightest setting (20/255) keeps LED output conservative.

## Safety warnings

- **Do not power the 58-LED strip from the ESP32's 3V3 pin.** At even modest
  brightness, 58 WS2812B LEDs can draw more current than the ESP32's onboard
  3.3V regulator is rated for. GPIO4 provides a **3.3V data signal only** —
  it does not and must not supply LED power.
- **Use a suitable external power supply for the LED strip**, sized for the
  strip's actual current draw at the brightness/color patterns you intend to
  run, with a **common ground** between that supply, the LED strip, and the
  ESP32 GND pin. Do not float the grounds.
