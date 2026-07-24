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
- **Microphone:** 1x INMP441 I2S MEMS microphone
- **Power:** ESP32 5V pin and LED strip 5V share a common ground with the
  ESP32 GND pin; INMP441 VDD runs from the ESP32 3V3 pin

## GPIO assignments

| Signal | GPIO | Notes |
|---|---|---|
| LED data (DIN) | 4 | 3.3V logic-level data signal to LED strip |
| Mode button | 10 | `INPUT_PULLUP`, wired to GND — cycles LED mode |
| Mute button | 11 | `INPUT_PULLUP`, wired to GND — toggles LEDs off/on |
| Brightness button | 17 | `INPUT_PULLUP`, wired to GND — cycles brightness |
| Button 4 | 5 | `INPUT_PULLUP`, wired to GND — toggles mic diagnostic mode |
| INMP441 SCK/BCLK | 6 | I2S bit clock, driven by the ESP32 (I2S master) |
| INMP441 WS/LRCLK | 7 | I2S word select, driven by the ESP32 (I2S master) |
| INMP441 SD/DATA | 15 | I2S data input to the ESP32 |

INMP441 `L/R` is tied to GND (selects the LEFT I2S channel) and `GND` is
tied to a common ground shared with the ESP32.

GPIO45 was tried as a candidate spare pin during wiring diagnostics and
failed to respond even to a direct short to GND (likely not broken out to a
usable header pin on this board, or a bad physical connection) — it is not
used by this firmware. GPIO8 was the originally planned mic data pin before
the board's actual wiring was traced to GPIO15 instead.

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

**Button 4 (GPIO5)** — toggles the microphone diagnostic mode described
below. Does not affect LED mode, mute, or brightness.

All brightness levels stay well below full brightness (255) — even the
brightest setting (20/255) keeps LED output conservative.

## Microphone (INMP441) diagnostic

Button 4 toggles a live microphone diagnostic on and off:

```
[MIC] Diagnostic enabled
[MIC] bytes=8192 rawMin=-35207 rawMax=32493 peak=33953 rms=11739
...
[MIC] Diagnostic disabled
```

While enabled, it reads I2S audio continuously (non-blocking relative to
the button/animation loop — see I2S config below) and prints roughly 8
readings/second: bytes read that window, raw sample min/max, and a
DC-corrected peak and RMS. Buttons and LED animations keep working
normally while the diagnostic runs.

**I2S configuration** (`I2S_NUM_0`, master receive mode):

- Sample rate: 16000 Hz
- 32-bit I2S words; the INMP441 left-justifies a 24-bit sample in each
  word, so firmware recovers it with `sample >> 8`
- LEFT channel selected (`I2S_CHANNEL_FMT_ONLY_LEFT`) — correct because
  the INMP441's `L/R` pin is tied to GND; it would need RIGHT instead if
  `L/R` were tied to VDD
- Standard I2S format (`I2S_COMM_FORMAT_STAND_I2S`)
- 4 DMA buffers × 256 frames
- 20ms read timeout — a pure 0-tick non-blocking poll was tried first but
  raced the DMA's buffer-ready signal and produced false "0 bytes read"
  errors even though the peripheral was working correctly; 20ms resolved
  this while staying short enough to keep buttons/animations responsive
- Streaming DC correction (slow exponential offset tracker) applied to
  every sample before peak/RMS accumulation, so a constant bias can't
  mask or exaggerate real audio content

**Fault detection:** the diagnostic reports (once each, until the
condition clears) if I2S initialization fails, if reads return 0 bytes
repeatedly, if raw samples stay exactly zero for several seconds, if raw
samples stay constant/stuck at any value for several seconds, or if
samples stay saturated near the 24-bit signed range for several seconds.

**Expected response** (hardware-verified):

| Condition | Typical RMS |
|---|---|
| Quiet room | ~8,000–20,000 (ambient + mic self-noise floor) |
| Normal speech near mic | ~40,000–360,000, rising/falling with speech |
| Light tap near the mic board | ~19,000–23,000, brief bump |
| Clap near mic | ~100,000–900,000, sharp spike then fast decay |

Very close or very loud claps may briefly push raw samples to the 24-bit
saturation limit (~±8,388,607) — this is normal clipping from an
excessively loud/close transient, not a fault.

## Safety warnings

- **Do not power the 58-LED strip from the ESP32's 3V3 pin.** At even modest
  brightness, 58 WS2812B LEDs can draw more current than the ESP32's onboard
  3.3V regulator is rated for. GPIO4 provides a **3.3V data signal only** —
  it does not and must not supply LED power.
- **Use a suitable external power supply for the LED strip**, sized for the
  strip's actual current draw at the brightness/color patterns you intend to
  run, with a **common ground** between that supply, the LED strip, and the
  ESP32 GND pin. Do not float the grounds.
