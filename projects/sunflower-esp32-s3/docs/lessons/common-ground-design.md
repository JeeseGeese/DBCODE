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
(`docs/current/ELECTRICAL.md`) and never identified as a source of a
problem in this project's bring-up history — the *absence* of ground-
related symptoms is itself indirect confirmation the discipline has
been followed correctly.

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
