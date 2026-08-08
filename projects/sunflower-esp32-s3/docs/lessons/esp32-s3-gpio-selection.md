---
name: esp32-s3-gpio-selection
description: How Sunny's GPIO assignments were chosen and validated on an ESP32-S3-WROOM N16R8
metadata:
  type: lesson
---

# ESP32-S3 GPIO selection

## Problem

Assigning GPIOs for LEDs, buttons, I2S (mic + amplifier), and a motor
driver on an ESP32-S3-WROOM N16R8 module without accidentally choosing
a flash/PSRAM-reserved, boot-strapping, or USB/JTAG-sensitive pin.

## Root cause / discovery

The N16R8 variant uses octal (not quad) PSRAM, which reserves
additional GPIOs beyond the standard ESP32-S3 quad-SPI flash pin set —
a generic "ESP32-S3 pinout" reference is not sufficient for this exact
module variant. Sunny's working pin set (4, 5, 6, 7, 8, 9, 10, 11, 15,
16, 17) was arrived at empirically over several bring-up sessions
rather than from a single upfront audit document.

## How it was verified

Each peripheral was bench-tested individually after wiring (LED color/
rainbow test, mic RX trace, motor forward/reverse, I2S write success
counters) — no single static audit of "all N16R8-reserved pins" exists
in this project's history; correctness was established by things
actually working, repeatedly, across many boot cycles.

## Correct approach

Before wiring a new peripheral to an unused-looking pin on an
N16R8 (or any octal-PSRAM ESP32-S3 module): check the exact module
variant's datasheet for its PSRAM/flash pin reservations (not a generic
ESP32-S3 diagram), check for boot-strapping pins, check for USB/JTAG
pins, and cross-reference against every pin already listed in
`docs/current/GPIO_MAP.md`. See `docs/playbooks/GPIO_VALIDATION.md` for the
step-by-step procedure this lesson feeds.

## Common failure modes

- Assuming a "generic ESP32-S3" pinout applies to the N16R8's octal-
  PSRAM variant without checking.
- Wiring a boot-strapping pin without verifying its required state at
  reset, causing intermittent boot/flash failures.
- Not maintaining one canonical GPIO table, causing two subsystems to
  silently claim the same pin (see `docs/lessons/common-ground-design.md`
  for the related grounding discipline, and
  `docs/current/SOFTWARE_ARCHITECTURE.md`'s "single-owner resource"
  convention for the software-side analog of this discipline).

## Applies to future projects?

Yes — broadly reusable for any ESP32-S3 (especially octal-PSRAM
variant) project. Not Sunny-specific.

## Related Sunny files

`include/Config.h` (GPIO `#define`s), `docs/current/GPIO_MAP.md`,
`docs/playbooks/GPIO_VALIDATION.md`.
