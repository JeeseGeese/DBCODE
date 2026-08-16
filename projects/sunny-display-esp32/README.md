# sunny-display-esp32

Sunny's V1.2 **UI controller** firmware — a separate PlatformIO project
from `projects/sunflower-esp32-s3/` (Sunny's V1.1 body controller). See
[`docs/DISPLAY_HARDWARE.md`](docs/DISPLAY_HARDWARE.md) for the full
hardware identity, pin map, driver stack, LVGL configuration, memory
plan, architecture, and bring-up status — this file is just a map.

## Hardware

ELEGOO ESP32 2.8" Touch Display (ESP32-WROOM-32E, ILI9341, XPT2046
resistive touch, LCDWIKI model E32R28T). No PSRAM, 4MB flash.

## Status

**V1.2.1 (display/touch standalone bring-up) — PHYSICALLY VALIDATED**
(2026-08-08): display initializes, renders correctly, right-side-up in
landscape at `rotation=3` (rotation=1 was tried first and found upside
down), colors/text correct, touch initializes and TAP TEST responds
correctly. See `docs/DISPLAY_HARDWARE.md`'s "First bring-up target"
table for the full itemized record.

**V1.2.2 (touch calibration) — mapping bug found and fixed
(2026-08-09), physical retest pending.** Physical validation found the
original calibration model had a real ~30-42px corner mapping error
(it treated an inset target's raw reading as if measured at the true
screen edge — the center stayed accidentally accurate, which is why
this wasn't obvious at first). An earlier "enlarging the hitbox fixed
it" conclusion in this project's docs was premature and has been
corrected. The fix: a per-axis linear (scale+offset) fit against the
measured targets' actual screen positions, chosen after comparing three
models on the real dataset — see `docs/DISPLAY_HARDWARE.md`'s "Touch
calibration procedure" section for the full bug/fix record and model
comparison. `test_host/final_touch_calibration.cpp` locks in both the
new model's accuracy and the old model's failure as a permanent
regression guard. Minimal screen-navigation
infrastructure (`HOME`/`AUDIO`/`MOTION`/`LEDS`/`DIAGNOSTICS`/`SETTINGS`
enum values + a placeholder-routing dispatcher) was also added for the
next UI-design sprint to build on — none of those six screens have real
content yet. The calibration screen remains reachable as a
diagnostic/manual mode for future recalibration.

This board currently runs **completely standalone** — its own USB-C for
power and programming, no electrical connection to the Sunny body
controller yet. See `docs/DISPLAY_HARDWARE.md`'s "Standalone-first
safety rule".

## Build

```bash
cd projects/sunny-display-esp32
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run
```

## Host tests

```bash
cd projects/sunny-display-esp32/test_host
for f in *.cpp; do n="${f%.cpp}"; g++ -std=c++17 -Wall -Wextra -o "/tmp/$n" "$f" && "/tmp/$n"; done
```

Covers the UI state model (`ui_state_model.cpp`), the generic touch
calibration coordinate transform (`touch_calibration.cpp`), the
calibration target geometry/sample-filtering/derivation math
(`calibration_math.cpp`), and the FINAL applied physical calibration
dataset (`final_touch_calibration.cpp`, locks in the actual measured
corner/center values) — pure logic, no hardware/LVGL dependency. Driver
code itself is not host-tested (not meaningful off real hardware).

## Upload

Standalone uploads to this board (its own USB-C only) are cleared as of
V1.2.1's physical validation. Still **do not** connect Sunny's body
ESP32-S3 to this board — see `docs/DISPLAY_HARDWARE.md`'s
"Standalone-first safety rule".

```bash
pio device list   # confirm the port -- do not assume; this board and
                   # the Sunny body controller may both be connected
pio run -t upload --upload-port <confirmed-port>
pio device monitor -p <confirmed-port> -b 115200
```

## Project layout

```
include/    Config.h (pin map), LGFXDevice.h, DisplayManager.h,
            TouchManager.h, UIState.h, Screens.h, Calibration.h,
            CalibrationManager.h, lv_conf.h
src/        matching .cpp files + main.cpp (init + service only)
test_host/  host-testable pure logic (UI state, touch calibration,
            calibration math)
docs/       DISPLAY_HARDWARE.md (the full reference)
```
