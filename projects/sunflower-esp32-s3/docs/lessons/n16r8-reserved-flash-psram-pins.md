---
name: n16r8-reserved-flash-psram-pins
description: The N16R8 ESP32-S3 variant's octal PSRAM reserves more GPIOs than a generic ESP32-S3 pinout shows
metadata:
  type: lesson
---

# Reserved N16R8 flash/PSRAM pins

## Problem

An ESP32-S3-WROOM N16R8 module (16MB flash, 8MB **octal** PSRAM) has a
different reserved-pin footprint than a generic ESP32-S3 or a
quad-PSRAM variant — using a generic pinout diagram risks wiring a pin
that's actually dedicated to PSRAM on this specific module.

## Root cause / discovery

Octal PSRAM uses additional GPIO lines beyond the standard quad-SPI
flash pin set that a generic ESP32-S3 reference typically shows. This
project's `platformio.ini` explicitly configures
`board_build.psram_type = opi` (octal) and
`board_build.arduino.memory_type = qio_opi`, confirming the octal
variant is in use.

## How it was verified

Not independently re-derived pin-by-pin in this project's history —
flagged here as a required check for any *future* GPIO assignment
(Raspberry Pi comms, camera trigger, Buttons 5/6), since Sunny's
current 11-pin assignment (see `docs/current/GPIO_MAP.md`) was arrived at
without incident, but a new pin choice has not yet been stress-tested
against this specific reservation.

## Correct approach

Before wiring any new peripheral, check the exact N16R8/octal-PSRAM
pin reservation list against the specific WROOM module datasheet (not
a generic ESP32-S3 diagram), in addition to the standard boot-strapping
and USB/JTAG exclusions. See `docs/playbooks/GPIO_VALIDATION.md`.

## Common failure modes

- Using a generic ESP32-S3 (or ESP32-S3 quad-PSRAM) pinout reference
  for an octal-PSRAM N16R8 board.
- Discovering the conflict only after intermittent flash/PSRAM
  corruption or boot failures, rather than before wiring.

## Applies to future projects?

Yes, for any project using an ESP32-S3 N16R8 (or other octal-PSRAM)
module specifically — not the standard quad-PSRAM variant.

## Related Sunny files

`platformio.ini`, `docs/current/GPIO_MAP.md`,
`docs/playbooks/GPIO_VALIDATION.md`.
