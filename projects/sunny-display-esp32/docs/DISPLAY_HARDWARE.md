# Sunny UI Controller — Display Hardware, Architecture & Bring-Up

**Living document.** Covers the ELEGOO ESP32 2.8" Touch Display (Sunny's
V1.2 UI controller) — hardware identity, pin map, driver stack, LVGL
configuration, memory plan, and bring-up status. Updated as V1.2
progresses; not a duplicate of `projects/sunflower-esp32-s3/docs/` (the
Sunny body controller's own docs), which this project deliberately does
not touch.

## Two-controller architecture

Sunny V1.2 uses **two independent ESP32 controllers**, not one:

| | Body Controller | UI Controller (this project) |
|---|---|---|
| Chip | ESP32-S3 (N16R8) | ESP32-WROOM-32E |
| Project | `projects/sunflower-esp32-s3/` | `projects/sunny-display-esp32/` |
| Owns | 36 WS2812 LEDs, LED effects, AudioOverlay, INMP441 mic, MAX98357A speaker, shared I2S, DRV8833 motor, MusicMotor, physical buttons, personality/behavior coordination | ILI9341 LCD, XPT2046 touch, LVGL, UI rendering/navigation/state, Sunny face/personality *display* |
| Status | Validated at `sunny-v1.1` | V1.2.1 **COMPLETE** (physically validated); V1.2.2 touch calibration **mapping bug found and fixed** (2026-08-09) — a real ~30-42px corner mapping error was found and replaced with a fitted linear model (<2px error on the measured dataset); physical retest pending before V1.2.2 is marked complete — see "Touch calibration procedure" below |

These are **separately buildable, separately uploadable PlatformIO
projects**. Nothing in this project modifies the body controller's
source, and nothing in the body controller depends on this project
existing. See "Future ESP32-to-ESP32 communication" below for how they
will eventually talk to each other — not implemented yet.

## Hardware identity

**Brand/product:** ELEGOO ESP32 2.8-inch Touch Display (marketed by
ELEGOO itself as "Elegoo CYD" — part of the widely-documented "Cheap
Yellow Display" board family, not a bespoke ELEGOO-only PCB design).

**Confirmed physical markings:** "2.8\" LCD Display", "ESP32-32E",
"240x320", "Resistance Touch".

**Exact manufacturer model (from official schematic, see "Evidence"
below):** LCDWIKI **E32R28T** (the resistive-touch variant of the
E32R28T/E32N28T 2.8" ESP32-32E display family).

| Component | Part |
|---|---|
| MCU module | ESP32-WROOM-32E (ESP32-D0WD-V3 chip) |
| LCD controller | ILI9341, 240x320, 4-wire SPI |
| Touch controller | XPT2046, resistive |
| USB-UART bridge | CH340C (with one-click auto-download circuit) |
| Audio amplifier | FM8002E (mono, drives an external SP+/SP- speaker terminal) |
| Battery charge IC | TP4054 |
| 5V→3.3V regulator | ME6217C33M5G LDO, 800mA max |

**Memory (confirmed, no PSRAM):** 448KB ROM + 520KB SRAM + 16KB RTC SRAM
+ 4MB QSPI flash. 26 external GPIOs. 2.4GHz WiFi + Bluetooth v4.2 + BLE
(unused by this project so far).

## Evidence trail (per the "do not guess pins" requirement)

Priority order actually achieved, highest to lowest:

1. **Official manufacturer schematic — ACHIEVED.** LCDWIKI's
   "2.8inch ESP32-32E E32R28T&E32N28T User Manual" (PDF,
   `https://www.lcdwiki.com/res/E32R28T/2.8inch_ESP32-32E_E32R28T_E32N28T_User_Manual.pdf`)
   contains the full schematic (ESP32-WROOM-32E main control circuit,
   resistive touch circuit, backlight circuit, RGB LED circuit, SD card
   circuit, expansion header circuit, battery circuits) with every net
   name tied to an explicit ESP32 IO number. This is the primary source
   for every pin below.
2. **Exact-model-match summary page** — `https://www.lcdwiki.com/2.8inch_ESP32-32E_Display`
   — independently corroborates the same pin numbers (used as a
   cross-check, not the primary source).
3. No dedicated ELEGOO-branded schematic/GitHub repo was found for this
   exact SKU — ELEGOO's own listing explicitly brands the product
   "Elegoo CYD," confirming it's a rebrand of this exact
   LCDWIKI/CYD-family board design rather than a different PCB.

**Every pin below is CONFIRMED against source #1** unless explicitly
marked otherwise.

## Complete pin map

### ILI9341 LCD (dedicated hardware SPI bus)

| Signal | GPIO | Confidence |
|---|---|---|
| CS | 15 | CONFIRMED |
| DC/RS | 2 | CONFIRMED |
| SCLK | 14 | CONFIRMED |
| MOSI | 13 | CONFIRMED |
| MISO | 12 | CONFIRMED |
| RESET | — (tied to module EN/system reset, not a separate GPIO) | CONFIRMED |
| Backlight | 21 (active-HIGH via BSS138 FET, PWM-capable, **off by default at power-on**) | CONFIRMED |

### XPT2046 touch (fully independent SPI pins — no bus sharing with the LCD)

| Signal | GPIO | Confidence |
|---|---|---|
| CS | 33 | CONFIRMED |
| SCLK | 25 | CONFIRMED |
| MOSI (DIN) | 32 | CONFIRMED |
| MISO (DOUT) | 39 (input-only) | CONFIRMED |
| IRQ/PEN | 36 (input-only, active-LOW on contact) | CONFIRMED |

### MicroSD (shares its SPI bus with the expansion header — not used by Phase 1)

| Signal | GPIO | Confidence |
|---|---|---|
| CS | 5 | CONFIRMED |
| MOSI | 23 | CONFIRMED |
| SCLK | 18 | CONFIRMED |
| MISO | 19 | CONFIRMED |

### RGB status LED (common anode, active-LOW)

| Signal | GPIO | Confidence |
|---|---|---|
| Pin set {R, G, B} | 17, 22, 16 | CONFIRMED (pin set) |
| Exact color-to-pin assignment | R=17, G=22, B=16 | HIGH-CONFIDENCE (schematic doesn't spell out the color labels in prose; not independently re-verified against a physically-lit LED) |

### Audio (not used by Phase 1)

| Signal | GPIO | Confidence |
|---|---|---|
| Amplifier enable | 4 | CONFIRMED |
| DAC audio output | 26 | CONFIRMED |

### Battery

| Signal | GPIO | Confidence |
|---|---|---|
| Voltage sense (ADC, input-only) | 34 — reads through a 100K/100K divider; multiply by 2 for actual voltage | CONFIRMED |

### Fixed-function pins (not general-purpose)

| Signal | GPIO | Note |
|---|---|---|
| BOOT | 0 | Standard ESP32 boot-strap pin — never drive as an ordinary output |
| RESET | — | Hardware EN line via a discrete button/RC circuit, not a GPIO |

### UART0 (console/programming)

| Signal | GPIO | Note |
|---|---|---|
| TX0 | 1 | **CONFIRMED shared** between the onboard CH340C/USB-C circuit AND the external 4-pin "Serial Port" header (P2) — not independent |
| RX0 | 3 | Same sharing |

### Expansion header — the ONLY confirmed-free pins

| Signal | GPIO | Note |
|---|---|---|
| Spare CS (P3 header) | 27 | Genuinely unused by any onboard peripheral, bidirectional-capable |
| Spare input (P4 header) | 35 | **Input-only** (confirmed in the manual) |

Everything else exposed on P3/P4 duplicates a pin already listed above
(the SD/expansion SPI bus). **This board has almost no free GPIOs** —
only 27 and 35 (input-only) — which directly shapes the future
ESP32-to-ESP32 communication recommendation below.

### Unknown / not yet needed

Nothing required for Phase 1 bring-up is unknown. The 18-pin LCD FPC
welding interface and detailed SD-card pull-up values are documented in
the schematic but not reproduced here since they're assembly-level
detail, not firmware-relevant.

## GPIO/resource audit summary

- **Free, usable GPIOs on this board: effectively just IO27** (bidirectional)
  and IO35 (input-only). Every other pin is already claimed by an onboard
  peripheral (LCD, touch, SD/expansion-shared, RGB LED, audio, battery
  sense, UART0, boot-strap).
- **UART0 is not free for inter-controller use without contention** — it's
  the same lines as the USB programming/monitor console (see "Serial
  console" section below).
- **No dedicated second hardware UART is available on an exposed pin
  pair** given only one bidirectional spare pin (27) exists. This is the
  central constraint for the future communication design below.

## Driver stack selection

**Chosen: LovyanGFX (panel + native `Touch_XPT2046` support) + LVGL 9.5.0.**

Evaluated against TFT_eSPI (the other leading candidate) on this
project's stated priorities:

| Priority | LovyanGFX | TFT_eSPI |
|---|---|---|
| Reliability | Actively maintained, native ILI9341 + XPT2046 panel classes | Also mature/reliable, but touch requires a *separate* library (`XPT2046_Touchscreen`) |
| Compatibility with this exact board | Confirmed pin config maps cleanly (LCD and touch on fully independent SPI pins — no bus-sharing workaround needed) | Same pins work, but touch config lives in a second library |
| Maintainability | Config expressed as an explicit C++ class (`LGFXDevice.h`) — no vendored-header patching | Requires editing/overriding a library-global `User_Setup.h`, a known source of confusion across projects |
| Clean LVGL integration | Straightforward flush-callback bridge (implemented in `DisplayManager.cpp`) | Equally straightforward |
| RAM usage | Comparable; neither dominates | Comparable |
| Touch support | **Native** `Touch_XPT2046` class, one dependency total | Needs a second library (`XPT2046_Touchscreen`) |
| Minimal complexity | **One graphics dependency covers both panel and touch** | Two dependencies (TFT_eSPI + a touch library) |

The explicit C++ config class (matching this repo's stated preference
for explicit, commented tunables over implicit/magic config — see the
body project's `Config.h` convention) and the single-dependency
panel+touch story were the deciding factors. `witnessmenow/ESP32-Cheap-
Yellow-Display` (the most-specific community reference for this exact
board family) also currently favors LovyanGFX for new work.

## LVGL version and configuration

**Kept LVGL 9.5.0** — already staged in `platformio.ini` before this
board's exact identity was known; independently re-evaluated (not just
inherited) against this specific hardware/driver-stack combination and
found appropriate:

- LVGL 9.x is the current major version, actively maintained.
- Documented, working combinations of LVGL 9.2.x with TFT_eSPI/XPT2046
  exist in the community; nothing about ILI9341/XPT2046/ESP32-WROOM-32E
  requires the older LVGL 8.x line.
- No board-specific reason found to prefer 8.x over 9.x for this
  hardware.

**`lv_conf.h` strategy:** hand-written, NOT the generated template
(`include/lv_conf.h`, found via `-DLV_CONF_INCLUDE_SIMPLE -Iinclude`).
Explicitly configures color depth (RGB565, matches the ILI9341 natively),
memory (plain `malloc`/`free`, no static pool — no PSRAM to spare a large
one), tick source (fed manually from `millis()`), logging (WARN level,
routed to Serial), and the default theme/font.

**Widget configuration — a real lesson from this bring-up:** the first
attempt explicitly force-disabled every widget this project doesn't use
(scale, table, switch, roller, line, bar, etc.). This **broke the
build** — `lv_scale.c` (left at its library-default "on" state, since it
wasn't explicitly mentioned) internally depends on `lv_line_class`,
which had been explicitly forced off, producing an undefined-symbol
error. LVGL 9's widgets have an internal dependency graph this project
has no reason to hand-verify. **Fix:** only `LV_USE_LABEL` and
`LV_USE_BUTTON` are explicitly turned on (the two Screen 1 needs);
everything else is left unmentioned, falling back to LVGL's own
internally-consistent defaults. This trades some flash for build
reliability — see the memory plan below for why that trade was safe on
this board's actual measured footprint.

## Memory / performance plan

**No PSRAM on this module** (confirmed via the official schematic — the
manual's memory description never mentions it, and ESP32-WROOM-32E is
not a PSRAM-variant part).

**Draw buffer strategy:** partial rendering, NOT a full framebuffer. A
full 240x320 RGB565 framebuffer would be 153,600 bytes — too large a
static allocation relative to this chip's usable DRAM segment (which is
substantially smaller than the full 520KB SRAM figure once the
Arduino/ESP-IDF runtime's own static reservations are accounted for).
Instead, two rotating strip buffers of `240 x 15` pixels each (14,400
bytes total) are used; LVGL redraws in horizontal bands, calling the
flush callback once per band.

**A real DRAM overflow was hit and fixed during this bring-up**: an
initial 30-line buffer size (28,800 bytes) plus LVGL's full default
widget set overflowed the DRAM segment by 8,248 bytes at link time.
Halving the buffer to 15 lines resolved it. This is recorded here so a
future session doesn't have to rediscover it — if buffer size is ever
increased again (e.g. for smoother animation), re-verify against a
clean build rather than assuming headroom exists.

**Measured, current build (Phase 1 screen, LVGL defaults except
LABEL/BUTTON):**

- RAM (DRAM): 111,224 / 327,680 bytes (33.9%)
- Flash: 661,477 / 1,310,720 bytes (50.5% of the *app partition* — note
  this is the default `esp32dev` board's partition scheme, not
  necessarily using the full confirmed 4MB flash; revisit the partition
  table if a future feature needs more app space, e.g. SD-card asset
  loading)

**UI tick/refresh rate:** LVGL's own defaults — 30ms draw period, 30ms
input-read period (~33Hz), unchanged from the library default since
nothing about this screen needs faster response.

## Software architecture

```
main.cpp            setup()/loop() wiring only -- init + service, no UI logic
Config.h             every pin/tunable, grouped, CONFIRMED/HIGH-CONFIDENCE noted

DISPLAY HARDWARE:
  LGFXDevice.h        the LovyanGFX hardware device definition (panel+touch+backlight config)
  DisplayManager.h/.cpp  sole owner of the LGFX object; LVGL display driver + flush callback;
                          backlight control; exposes ONE narrow raw-touch accessor for TouchManager

TOUCH HARDWARE:
  TouchManager.h/.cpp  calibration constants, raw->screen coordinate transform (host-testable,
                        see test_host/touch_calibration.cpp), LVGL indev registration/read callback,
                        readRawTouchPointForCalibration() -- the one accessor CalibrationManager uses

UI STATE:
  UIState.h/.cpp       hardware-free data model (host-testable, see test_host/ui_state_model.cpp);
                        SunnyUIScreen enum + isScreenImplemented() -- the single source of truth for
                        which screens are real vs. navigation-infrastructure-only placeholders

UI SCREENS:
  Screens.h/.cpp        widget construction; showScreen()/updateActiveScreen() navigation dispatcher
                         (V1.2.2) routing to a real screen or a generic placeholder. Real screens:
                         BRING_UP_TAP_TEST, CALIBRATION (diagnostic/manual mode), TOUCH_VALIDATION
                         (default boot screen as of V1.2.2 -- static targets + TAP TEST, exercises
                         the applied calibration through the normal LVGL indev path)
  Calibration.h/.cpp     pure calibration math -- target geometry, sample filtering, transform
                         derivation (host-testable, see test_host/calibration_math.cpp and
                         test_host/final_touch_calibration.cpp, which locks in the actual applied
                         physical dataset)
  CalibrationManager.h/.cpp  stateful calibration screen + touch-sampling driver (V1.2.2), the
                         one consumer of Calibration.h's math and TouchManager's raw accessor

COMMUNICATION:
  Not implemented yet -- see "Future ESP32-to-ESP32 communication" below.
```

Single-owner conventions mirror the body project's own architecture
(`projects/sunflower-esp32-s3/docs/current/SOFTWARE_ARCHITECTURE.md`):
`DisplayManager` is the sole owner of the LGFX hardware object;
`TouchManager` is the sole owner of calibration/transform/indev logic
and never touches the LGFX object directly (see `DisplayManager.h`'s
`readRawTouchPoint()` — the one narrow accessor between them).

## First bring-up target — status

**Screen:** "SUNNY V1.2 / DISPLAY ONLINE / Resolution: 320x240 / Touch:
ONLINE / [ TAP TEST ]" — implemented in `Screens.cpp`. **V1.2.1 physically
validated 2026-08-08** (battery/USB power not relevant here — this board
runs standalone on its own USB-C).

**Requirements and status:**

| Requirement | Status |
|---|---|
| Display initializes | ✅ **Physically confirmed** — ILI9341 renders correctly |
| Correct 240x320 resolution | ✅ **Physically confirmed** — 320x240 landscape |
| Intentional orientation | ✅ **Physically confirmed right-side-up at `rotation=3`** — `rotation=1` was tried first and found upside down on real hardware 2026-08-08; `rotation=3` (180° from `rotation=1`, still landscape) fixed it |
| Colors correct | ✅ **Physically confirmed** |
| Text readable | ✅ **Physically confirmed** |
| LVGL genuinely rendering | ✅ **Physically confirmed** |
| No tearing/corruption | ✅ **Physically confirmed** (partial-buffer flush via `startWrite()`/`setAddrWindow()`/`writePixels()`/`endWrite()`) |
| Touch initializes | ✅ **Physically confirmed** — XPT2046 responds |
| Touch coordinates map correctly | ⚠️ **Fitted-model fix applied 2026-08-09, physical retest pending.** The original raw-min/max calibration model had a real, confirmed bug (systematic ~30-42px corner error) — see "Touch calibration procedure" and "Mapping-model bug and fix" below for the full record, including a correction to an earlier premature "resolved" conclusion in this document. Replaced with a per-axis linear fit (host-test-verified <2px error on the measured dataset); awaiting the user's physical confirmation before this row is marked physically confirmed. |
| TAP TEST button reacts + serial log | ✅ **Physically confirmed** — button visually reacts, count increments, touch position corresponds correctly to the visible button |

## Touch calibration procedure (V1.2.2 — mapping bug found and fixed, 2026-08-09)

**Correction to an earlier conclusion in this document:** an earlier
version of this section concluded the corner-target failures seen during
edge validation were a hitbox-size issue only, "resolved" by enlarging
the tap hitbox. That conclusion was **premature** — the enlarged hitbox
did make the corners *activate* (LVGL saw the press), but a further,
more careful physical retest showed the touch location still had to be
noticeably away from the visible dot to trigger it. That's not a hitbox
problem; it's evidence the raw→screen mapping itself was wrong. The
enlarged hitbox had been masking a real coordinate bug, not fixing one.
The actual bug and fix are below.

### The bug

The original calibration model (`deriveCalibrationFromCorners()` +
`transformRawTouchToScreen()`'s raw-min/max mapping) treated each
corner target's own averaged raw reading as if it had been measured **at
the true screen edge** (raw min → screen 0, raw max → screen width-1).
But the calibration targets are drawn **inset ~30px from the edge**
(exact-edge taps are unreliable on a resistive panel) — so a target's
raw reading actually corresponds to screen position 30 (or 289, etc.),
not 0 (or 319). This produced a systematic ~30-42px error at every
corner. The **center** target stayed accidentally accurate throughout,
because its true position IS the middle of the raw range, where the
edge-assumption error happens to cancel out — which is exactly why a
"center looks great, corners are off" symptom didn't initially read as
a mapping bug.

Confirmed by direct calculation against the measured dataset (see
`test_host/final_touch_calibration.cpp`'s regression guard, which
reproduces this exact failure as a permanent negative-control test):

| Target | True screen | OLD model predicted | OLD error |
|---|---|---|---|
| TOP-LEFT | (30,30) | (1,1) | 41.3px |
| TOP-RIGHT | (289,30) | (319,0) | 42.4px |
| BOTTOM-RIGHT | (289,209) | (319,239) | 42.4px |
| BOTTOM-LEFT | (30,209) | (0,239) | 42.2px |
| CENTER | (160,120) | (159,121) | 1.1px |

### The fix

Replaced the raw-min/max model with a **per-axis linear fit** (scale +
offset) directly against each target's actual known screen position,
using all 5 measured targets (not just the 4 corners) via least squares
— `deriveCalibrationFromSamples()` in `Calibration.cpp`. Three models
were compared on the real dataset:

| Model | Description | Mean error (5 pts) |
|---|---|---|
| A | Old raw-min/max | 33.9px |
| B | **Decoupled linear (scale+offset per axis)** | **0.93px** |
| C | Full 2D affine (cross-axis terms) | 0.92px |

Model C's cross-axis coefficients were ~0.0001 — two orders of magnitude
below the primary coefficients, i.e. statistically negligible. Model B
(the simpler model) is equally accurate, so it was chosen per "simplest
model that works": no cross terms, no `invertX`/`invertY` booleans
(sign is now just the fitted scale's sign), no separate raw-range
concept.

Which raw ADC channel drives which screen axis (`swapAxes`) is now
determined by **|Pearson correlation|** of each raw channel against each
screen axis across all samples — more robust than the old corner-index
spread heuristic, and confirmed on the real dataset:
`corr(rawX,screenY)=0.99996` vs. `corr(rawX,screenX)=0.0017` → swap
confirmed.

### Applied constants (fitted from the 5-point measured dataset)

```
swapAxes = true
scaleX  = 0.09019494   offsetX = -16.02758   (screenX = scaleX*rawY + offsetX)
scaleY  = 0.06781033   offsetY = -18.23129   (screenY = scaleY*rawX + offsetY)
```

Predicted vs. true for all 5 points under the NEW model (float32
precision, matching the firmware exactly):

| Target | Raw (x,y) | True screen | Predicted | Error |
|---|---|---|---|---|
| TOP-LEFT | (721,519) | (30,30) | (31,31) | 1.4px |
| TOP-RIGHT | (702,3382) | (289,30) | (289,29) | 1.0px |
| BOTTOM-RIGHT | (3365,3387) | (289,209) | (289,210) | 1.0px |
| BOTTOM-LEFT | (3337,507) | (30,209) | (30,208) | 1.0px |
| CENTER | (2038,1941) | (160,120) | (159,120) | 1.0px |

Well within the acceptance target (center ≤5px, corners ≤10px).
`test_host/final_touch_calibration.cpp` locks in both halves of this
record: the NEW model meeting the acceptance target, and the OLD model
failing it at every corner (a permanent regression guard against
reintroducing the bug).

### Calibration tool

`Calibration.cpp`'s `deriveCalibrationFromSamples()` now outputs exactly
the representation the runtime uses (swapAxes + scale/offset per axis),
not a misleading raw-min/max. `CalibrationManager.cpp`'s
`printProposedConstants()` prints per-target predicted-vs-true deltas
for **all 5 targets**, not just a center check — so a corner-specific
regression like this one can't hide behind a good center result again.

**V1.2.2's on-device calibration screen** (`CalibrationManager.h`/`.cpp`,
host-tested in `test_host/calibration_math.cpp`) is reachable as a
diagnostic/manual mode (`showScreen(SunnyUIScreen::CALIBRATION)`) but is
no longer the default boot screen (see "Post-calibration validation
screen" below). Flow: shows 5 targets in sequence (four corners inset
30px + center); tap-and-HOLD collects up to 12 raw samples per target,
trimmed-mean-averaged (`calibrationTrimmedMean()`, drops the extreme
20%); after all 5, fits and prints the constants above in copy-pasteable
form. **Not applied automatically** — a human reviews and pastes them
into `TouchManager.cpp`, same as this session's fix.

### Superseded first calibration pass

Before the dataset above, an earlier calibration pass was performed and
explicitly superseded by the (also now-superseded-model, but
raw-data-correct) second pass. The first pass's raw values were never
recorded in this document and are intentionally not reproduced from
memory.

### Recalibration

If the panel is ever recalibrated (e.g. after hardware rework), re-run
the on-device calibration screen, then update `TouchManager.cpp`'s
`CAL_*` constants and this section together — never one without the
other. This remains a measured, human-reviewed procedure; the firmware
never self-writes its own constants.

## Post-calibration validation screen (V1.2.2)

With real calibration applied, `main.cpp` now boots into
`showScreen(SunnyUIScreen::TOUCH_VALIDATION)` instead of the calibration
flow — `showTouchValidationScreen()` in `Screens.cpp`. This is a normal
LVGL screen (not a raw-sampling state machine like calibration): 5
static, tappable targets at the same 4-corner+center positions used for
calibration, plus the retained TAP TEST button (offset below the CENTER
target to avoid overlap). Each target is a real clickable LVGL object;
on press it highlights green, reads the point back through the normal
(now-calibrated) LVGL indev path, and prints
`[VALIDATE] target=<NAME> screen=(x,y) raw=(rx,ry)` to serial — this
exercises the *applied* `CAL_*` constants end-to-end rather than
re-deriving anything. `showScreen(SunnyUIScreen::CALIBRATION)` remains
available as a diagnostic/manual mode for future recalibration.

## Standalone-first safety rule (current phase)

**Power:** this board's own USB-C only.
**Programming:** this board's own USB-C only.
**Do NOT connect yet:** Sunny body ESP32-S3 UART, Sunny 5V rail, Sunny
3.3V rail, Sunny ground, or any Sunny GPIO to this board, in any
combination.

Rationale: Sunny V1.1's power/brownout investigation (see the body
project's `docs/current/POWER.md`) is recent and not fully closed;
introducing a second board electrically before this one's own
standalone operation is proven adds a new variable to an
already-open investigation. Electrical integration is a later V1.2
phase, not part of Phase 1.

## Future ESP32-to-ESP32 communication (design only — not implemented)

**Constraint that shapes this:** per the GPIO audit above, this board
has effectively **one** free bidirectional pin (IO27) and one free
input-only pin (IO35). The exposed "Serial Port" header shares UART0
with the USB programming/debug console — using it for body-controller
communication would contend with the normal upload/monitor workflow and
risk interacting with the CH340C's auto-reset circuit in surprising
ways.

**Recommendation:** do NOT use the exposed UART0-based "Serial Port"
header for inter-controller communication. If UART is still the chosen
transport once this phase is reached, it would need to be a
software-configured *second* UART instance remapped via the ESP32
classic's GPIO matrix onto IO27 (TX) — but IO35 being input-only means a
true bidirectional second UART isn't available without giving up one of
the currently-claimed pins (e.g. repurposing the RGB LED or SD/expansion
header), which would be a real, currently-undesirable tradeoff. This
needs a deliberate decision, not an assumption, when V1.2.4 is actually
reached — flagging it now so it isn't rediscovered late.

**Conceptual protocol (Phase 1 of communication, itself a LATER V1.2
sub-phase — see "V1.2 internal roadmap" in `ROADMAP.md`):**

```
DISPLAY -> BODY:  PING
BODY -> DISPLAY:  PONG
```

Then eventually:

```
BODY -> DISPLAY (state):    LED effect, overlay, brightness, mute,
                             Audio Mode, MusicMotor state, speaker
                             volume, microphone activity,
                             behavior/personality, diagnostics
DISPLAY -> BODY (command):  effect changes, overlay changes,
                             brightness, mute, Audio Mode, motion
                             controls, speaker volume
```

Protocol framing (message format, error handling, timeout/heartbeat
behavior) is intentionally undesigned until the transport question above
is actually resolved.

## Serial console clarity (two independently programmable ESP32s)

To avoid ever confusing which console belongs to which board:

- **Sunny body ESP32-S3** boot banner: `[SYSTEM] Sunflower LED
  controller starting` (see `projects/sunflower-esp32-s3/src/main.cpp`).
- **Sunny UI controller (this board)** boot banner: `[SYSTEM] Sunny UI
  controller starting (Phase 1 -- display/touch bring-up)`, immediately
  followed by `[SYSTEM] STANDALONE MODE -- no connection to the Sunny
  body ESP32-S3 controller`.

Log line prefixes are also disjoint by convention: this project uses
`[DISPLAY]`/`[TOUCH]`/`[SCREEN]`/`[LVGL]`; the body project uses
`[SPEAKER]`/`[POWER]`/`[HWTEST]`/`[MIC]`/etc. — no prefix collision.

Both boards' serial monitors must be opened against their own USB port —
**do not assume a `/dev/ttyACM0`-style port number is stable or
consistent between the two boards or across sessions**; always confirm
via `pio device list` before connecting to either.

## Power-integration considerations (not yet connected — informational only)

To be determined before any future electrical integration (do not guess
these now):

- This board's own regulator (ME6217C33M5G) accepts 2V-6.5V input and
  outputs regulated 3.3V at up to 800mA — meaning the board's own 5V
  USB-C/battery input could plausibly tolerate being sourced from
  Sunny's shared 5V rail *in principle*, but this has NOT been evaluated
  against Sunny's actual combined-load current budget (see the body
  project's `docs/current/POWER.md` — the V1.1 brownout investigation
  found the existing 5V rail's margin under combined load was already a
  live question before adding a third board to it).
- Typical/peak current draw for this display board (backlight +
  ILI9341 + WiFi/BT radio if ever used + speaker amplifier if ever used)
  has not been measured.
- UART logic level: both boards are 3.3V logic — no known level-shifting
  concern, but not yet verified against actual measured signal levels.
- Common-ground requirement: standard requirement for any future UART
  link between the two boards (same as every other cross-device
  connection in this repository's conventions) — not yet wired.
- Do not disturb the validated `sunny-v1.1` battery-powered
  configuration while doing standalone display bring-up — they remain
  electrically unconnected for now specifically to avoid this risk.

## V1.2 internal roadmap

```
V1.2.1  Display hardware identification + standalone firmware bring-up   <- COMPLETE, physically validated 2026-08-08
V1.2.2  LVGL + resistive-touch validation/calibration                    <- mapping bug fixed 2026-08-09
                                                                              (fitted linear model); physical
                                                                              retest pending
V1.2.3  Sunny UI foundation
V1.2.4  ESP32-S3 <-> Display ESP32 communication
V1.2.5  Live Sunny status screens
V1.2.6  Touchscreen controls
V1.2.7  Animations/personality UI
V1.2.8  Integrated validation + V1.2 checkpoint
```

V1.3 (Raspberry Pi) does not begin until this entire sequence completes
and its own checkpoint is reviewed/approved.

## Later Sunny UI architecture (design only — not implemented)

Planned screens, kept flexible for V1.3 Raspberry Pi integration later
(a future Pi could become another state source feeding the same UI
screens without redesigning them):

- **HOME** — Sunny face/personality animation, basic system state, quick status.
- **AUDIO** — mic activity, speaker volume, AudioOverlay, Audio Mode.
- **MOTION** — MusicMotor state, current intensity, motion mode.
- **LEDS** — current base effect, current overlay, brightness, mute.
- **DIAGNOSTICS** — body connection state, reset reason, mic level, speaker status, motor state, LED power estimate.
- **SETTINGS** — UI brightness, speaker volume, diagnostic logging controls, other safe user-facing controls.

**Navigation infrastructure only, added V1.2.2:** `UIState.h`'s
`SunnyUIScreen` enum now has entries for all six of these plus
`BRING_UP_TAP_TEST`/`CALIBRATION`, and `Screens.cpp`'s `showScreen()`
dispatcher routes any of the six to a generic placeholder screen
("`<NAME>` — Not yet implemented (V1.2.3+)"). `isScreenImplemented()` is
the single source of truth for which screens are real
(`BRING_UP_TAP_TEST`, `CALIBRATION`) vs. placeholder — update it
alongside adding each screen's real builder. **None of the six screens
above have real content yet** — only the enum values and the routing
exist, so V1.2.3 can build each screen against an already-working
dispatcher instead of inventing navigation from scratch.
