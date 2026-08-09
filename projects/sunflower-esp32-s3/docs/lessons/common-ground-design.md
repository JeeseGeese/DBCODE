---
name: common-ground-design
description: Every peripheral on a different power rail than the MCU still needs a shared ground reference
metadata:
  type: lesson
---

# Common-ground design

## Problem

Sunny has three different power domains (3.3V for the mic, shared 5V
for LEDs/amplifier/motor, and the MCU's own logic) — without a
deliberate common ground, signal integrity and even measurement safety
degrade.

## Root cause / discovery

Not an incident — an enforced design rule throughout this project's
wiring: every peripheral's GND is tied to a common ground shared with
the ESP32, regardless of which rail powers its VCC/VIN.

## How it was verified

Consistently stated across every peripheral's wiring documentation
(`docs/current/ELECTRICAL.md`). Directly confirmed as load-bearing
during the V1.1 power/brownout investigation (2026-08-08): on the
current solderless breadboard/Dupont prototype, **changing the
MAX98357A's ground connection caused the WS2812 LEDs to display
incorrect/chaotic colors** — a disturbed ground reference visibly
corrupted the LED data signal (a single-wire, timing-sensitive
protocol), not just amplifier noise. This is evidence the prototype has
meaningful sensitivity to ground reference/return-path quality; it does
**not** by itself prove the MAX98357A caused any observed brownout —
see `docs/current/POWER.md`'s "Ground / breadboard / Dupont sensitivity"
section for the full, deliberately separated writeup.

## Correct approach

Tie every peripheral's ground to one common reference, regardless of
how many separate power rails exist. Never assume two rails "share
ground automatically" just because they come from the same physical
supply — verify with continuity if in doubt, especially before
oscilloscope probing (see `docs/lessons/speaker-bridge-tied-output.md`
for why floating/wrong ground reference is also a probing-safety
issue).

## Common failure modes

- Floating a peripheral's ground when it's on a different rail than
  the MCU, "since it has its own power anyway" — causes noise coupling
  and unreliable digital signals (I2S, GPIO) between domains.
- Discovering a missing ground connection only through intermittent,
  hard-to-reproduce signal glitches rather than a wiring check.

## Applies to future projects?

Yes — universal multi-rail embedded wiring discipline, not
Sunny-specific.

## Related Sunny files

`docs/current/ELECTRICAL.md`, `docs/current/POWER.md`,
`docs/playbooks/GPIO_VALIDATION.md`.
