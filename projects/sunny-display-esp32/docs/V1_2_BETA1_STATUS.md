# Sunny V1.2 — First Beta Test Checkpoint

**Living document** — this file describes the Beta 1 checkpoint at the
moment it was cut. Update it (or its successor) as V1.2 continues; do
not silently rewrite this record to describe later work under the same
heading.

## Purpose

This is the first formal beta checkpoint for Sunny's touchscreen/UI
milestone (V1.2). It preserves the current working state of **both**
Sunny ESP32 controllers — the body controller (unchanged since the
physically-validated `sunny-v1.1` milestone) and the UI/display
controller (through V1.2.2, touch calibration) — before UI development
continues deeper into V1.2.3+.

**This is Beta 1, not V1.2 final.** V1.2 is not complete: five of its
eight internal sub-phases (V1.2.3–V1.2.8) have no implementation yet.

## Body Controller

Project: `projects/sunflower-esp32-s3/` (ESP32-S3, N16R8).

**Identical to `sunny-v1.1`.** `git diff` against the `sunny-v1.1` tag
shows zero changes to `include/`, `src/`, `test_host/`, or
`platformio.ini` — the body controller has not been touched by any
V1.2 work. Its physically-validated status, exit criteria, and accepted
limitations are recorded in full at
[`../sunflower-esp32-s3/docs/current/V1_1_STATUS.md`](../../sunflower-esp32-s3/docs/current/V1_1_STATUS.md)
and are not repeated here beyond the summary below.

Re-verified fresh for this checkpoint (not assumed from the tag alone):
20/20 host tests pass with 0 warnings, and a clean `pio run` build
succeeds with 0 project-source warnings — see "Software Verified"
below.

## Display Controller

Project: `projects/sunny-display-esp32/` (ESP32-WROOM-32E, ELEGOO 2.8"
touch display, LCDWIKI model E32R28T).

| Fact | Value |
|---|---|
| Board | ELEGOO E32R28T / CYD-family 2.8" touch display |
| MCU | ESP32-WROOM-32E |
| LCD controller | ILI9341 |
| Touch controller | XPT2046, resistive |
| Resolution / orientation | 320×240 landscape, `rotation=3` (physically validated — `rotation=1` was tried first and found upside down) |
| Touch calibration model | Per-axis linear fit (scale+offset), `swapAxes=true`, `scaleX=0.09019494 offsetX=-16.02758`, `scaleY=0.06781033 offsetY=-18.23129` |
| UI framework | LVGL 9.5.0 |
| Graphics/touch driver | LovyanGFX (native `Touch_XPT2046` support) |
| PSRAM | None (confirmed via official schematic) |
| Flash | 4MB QSPI |

Full hardware identity, pin map, driver-stack rationale, and bring-up
history: [`docs/DISPLAY_HARDWARE.md`](DISPLAY_HARDWARE.md).

**Status: V1.2.1 complete, V1.2.2 complete.** V1.2.1 (standalone
display/touch bring-up) was physically validated 2026-08-08. V1.2.2
(touch calibration) had a real ~30-42px corner-mapping bug (an inset
calibration target's raw reading was wrongly treated as measured at
the true screen edge), found and fixed 2026-08-09 with a per-axis
linear fit (<2px error on the measured dataset, host-test-verified).
The corrected model's physical spatial-accuracy retest — tapping all 5
targets + TAP TEST on the touch-validation screen and confirming the
crosshair/highlight lands accurately — was **pending as of the
2026-08-09 session** and has since been **confirmed passing by the
user during this checkpoint session**. See "Physically Verified" below
for exactly what that covers.

## Physically Verified

Only items with direct human observation on real hardware are listed
here — see `/AGENTS.md` section 6's physical-validation policy.

**Body controller** (from `sunny-v1.1`, 2026-08-08, battery power —
unchanged since):
- Body LED count is 36 (`[HWTEST] LED count configured: 36` on boot).
- HWTEST LED power limiter was physically verified (throttled a would-be
  2196mA startup frame down to the 1000mA budget on real hardware).
- Battery-powered operation avoided the previously-observed
  computer-USB brownout behavior under combined LED+MusicMotor load.
- Buttons, LED effects, AudioOverlay, microphone-reactive overlay,
  MusicMotor, and the speaker diagnostic suite all confirmed functional
  on battery power, combined, with no reset/brownout.

**Display controller:**
- Display initializes; ILI9341 renders correctly (2026-08-08).
- 320×240 landscape orientation is correct at `rotation=3` (2026-08-08).
- Colors and text are correct (2026-08-08).
- Resistive touch (XPT2046) responds — touch initializes (2026-08-08).
- TAP TEST button reacts, count increments, and touch position
  corresponds correctly to the visible button (2026-08-08).
- **Touch mapping/calibration has been physically validated** — the
  corrected per-axis linear-fit model's spatial accuracy across all 5
  targets (4 corners + center) plus TAP TEST was confirmed by the user
  during this checkpoint session, following the 2026-08-09 fix.

## Software Verified

Performed fresh during this checkpoint session (not assumed from an
earlier session):

**Body controller** (`sunflower-esp32-s3`):
- Host tests: 20/20 files compiled, executed, and passed. 0 failures,
  0 compiler warnings.
- Build: `pio run` — SUCCESS. RAM 23,048/327,680 bytes (7.0%), Flash
  456,357/6,553,600 bytes (7.0%). 0 project-source warnings; 74
  pre-existing, documented `ARDUINO_USB_MODE redefined` framework
  notices (harmless, explained in `platformio.ini`'s own comment).
- No firmware upload performed — source is byte-identical to the
  physically-validated `sunny-v1.1` tag, so re-flashing was unnecessary.

**Display controller** (`sunny-display-esp32`):
- Host tests: 4/4 files compiled, executed, and passed (26 individual
  checks across `calibration_math`, `final_touch_calibration`,
  `touch_calibration`, `ui_state_model`). 0 failures, 0 compiler
  warnings.
- Build: `pio run` — SUCCESS. RAM 111,452/327,680 bytes (34.0%), Flash
  668,557/1,310,720 bytes (51.0%). 0 warnings of any kind. These
  figures exactly match the last uploaded firmware from the 2026-08-09
  session, confirming the currently-checked-out source matches what
  was on the board when the user physically retested it.
- No new firmware upload performed this session — the already-flashed
  firmware (source-identical to what's checked in) is the one the user
  just physically retested.

## Current Beta Capabilities

What a user can actually do with this checkpoint right now:

- Run Sunny's body controller exactly as at `sunny-v1.1`: four-button
  control, WS2812 base effects + audio-reactive overlays, microphone
  input, MAX98357A speaker with the full diagnostic/bench suite,
  DRV8833 motor with MusicMotor and idle/personality movement — all on
  battery power, previously validated brownout-free.
- Power on the display controller standalone (its own USB-C) and see a
  working, accurately-calibrated touchscreen: boots to a touch-
  validation screen with 5 tappable targets plus a TAP TEST button, all
  spatially accurate to the physically-confirmed calibration.
- Reach a manual/diagnostic calibration screen
  (`showScreen(SunnyUIScreen::CALIBRATION)`) for future recalibration
  if the panel is ever reworked.
- See routing/navigation infrastructure for six future screens
  (HOME/AUDIO/MOTION/LEDS/DIAGNOSTICS/SETTINGS) — each currently shows
  a "Not yet implemented" placeholder; none has real content yet.

What a user **cannot** yet do: see live body-controller state on the
display, control the body controller from the touchscreen, or use any
of the six planned UI screens for their intended purpose. The two
boards remain electrically unconnected by design (see
`DISPLAY_HARDWARE.md`'s "Standalone-first safety rule").

## Known Limitations

- The touchscreen and body controller are **not yet integrated** for
  live state exchange — no communication transport exists yet
  (`DISPLAY_HARDWARE.md`'s "Future ESP32-to-ESP32 communication"
  documents the constraint and open design question; the display board
  has effectively one free bidirectional GPIO).
- UI/animation work remains under active development — V1.2.3 through
  V1.2.8 (UI foundation, communication, live status screens,
  touchscreen controls, animations/personality UI, integrated
  validation) have not started.
- Raspberry Pi integration is future V1.3 work; camera and AI/voice are
  later milestones still (V1.4+).
- The body-controller's electrical brownout root cause was strongly
  associated with computer-USB source power (battery power eliminated
  it under the same load) but was **not formally isolated** to one
  exact physical component — this is an accepted, documented V1.1
  limitation, not a blocker.
- No permanent final PCB or production power distribution has been
  validated for either controller — both remain solderless
  breadboard/Dupont prototypes.
- The display board's power-integration questions (whether it could
  safely share Sunny's 5V rail, its own typical/peak current draw) are
  explicitly undetermined — see `DISPLAY_HARDWARE.md`'s
  "Power-integration considerations." The two boards are not
  electrically connected, by design, until this is resolved.
- The residual low-frequency speaker buzz on the body controller
  remains unresolved (accepted V1.1 limitation, unchanged by V1.2 work).

## Accepted Working Rule

> If a system is physically validated and meets the requirement,
> preserve that known-good state. Do not redesign, recalibrate,
> refactor, or optimize it without a specific problem or meaningful
> benefit.

## Restore Point

_Populated after commit/tag/push:_

- **Branch:** `feature/expressive-motion-v1`
- **Commit:** _pending_
- **Tag:** `sunny-v1.2-beta1` (pending)
- **Date:** 2026-08-16
- **Remote verification:** _pending_
