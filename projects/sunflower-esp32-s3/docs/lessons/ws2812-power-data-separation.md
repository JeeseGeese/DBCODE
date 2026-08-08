---
name: ws2812-power-data-separation
description: Never power a WS2812 strip from an MCU's own logic-rail pin
metadata:
  type: lesson
---

# WS2812 power/data separation

## Problem

58 WS2812B LEDs can draw far more current than an ESP32's onboard 3.3V
regulator is rated for at anything beyond very low brightness.

## Root cause / discovery

WS2812-class LEDs draw current per-channel-per-LED at full drive
(Sunny's estimate: `LED_MAX_MA_PER_CHANNEL=20mA`, `LED_IDLE_MA_PER_LED
=1mA` — see `Config.h`). At 58 LEDs, worst-case full-white draw is on
the order of amps, far beyond what a 3.3V logic regulator provides.

## How it was verified

Not an incident report — a documented safety rule from the start,
reinforced by a software power estimator/limiter (`applyPowerLimit()`
in `main.cpp`) as defense-in-depth, not the primary control.

## Correct approach

GPIO provides a **3.3V logic-level data signal only**. Power the strip
from a separate, adequately-rated supply (Sunny uses the shared 5V
rail — see `docs/current/POWER.md`), with a **common ground** between that
supply, the strip, and the MCU GND. Additionally implement a software
current estimate/cap as a bring-up safety aid (not a substitute for
correct electrical design).

## Common failure modes

- Powering a strip from a dev board's 3V3/5V pin "because it's
  convenient for a quick test," then leaving that wiring in place.
- Floating grounds between the LED supply and the MCU — causes data-
  signal integrity problems even if power is otherwise fine.
- Trusting a software power limiter as sufficient electrical
  protection — it cannot protect against a supply undersized for the
  LEDs' physical maximum draw.

## Applies to future projects?

Yes — universal WS2812/addressable-LED wiring rule, not Sunny-specific.

## Related Sunny files

`docs/current/POWER.md`, `README.md`'s "Safety warnings" section,
`main.cpp`'s `applyPowerLimit()`, `docs/playbooks/WS2812_BRINGUP.md`.
