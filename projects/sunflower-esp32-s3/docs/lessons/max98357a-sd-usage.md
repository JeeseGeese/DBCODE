---
name: max98357a-sd-usage
description: MAX98357A SD is a manual logic-level enable pin, not GPIO-controlled — sequence it after confirmed digital silence
metadata:
  type: lesson
---

# MAX98357A `SD` (shutdown/enable) usage

## Problem

Decide how and when to enable a MAX98357A amplifier relative to
firmware I2S initialization.

## Root cause / discovery

In this project, `SD` is a manually-moved logic pin (GND=disabled,
3.3V=enabled) — not driven by any GPIO or firmware logic. This was a
deliberate bring-up choice, not a limitation: it lets a human gate
enabling the amplifier on an explicit serial confirmation, independent
of firmware state, which is a safer bring-up posture than an
auto-enabling GPIO would be.

## How it was verified

Consistently followed across all speaker bring-up sessions: `SD`
stays at GND until `[SPEAKER] Digital silence active` prints, per the
documented startup sequence (see `max98357a-bringup.md`).

## Correct approach

During bring-up, keep `SD` manual and gate it on an explicit,
human-confirmed serial signal. If a future revision wants firmware
control of `SD` (to let software mute/unmute the amp instead of a
manual wire move), that requires a **new, currently-unassigned** GPIO
— see `docs/current/GPIO_MAP.md`'s "Reserved/excluded pins" before proposing
one; do not silently repurpose an existing pin.

## Common failure modes

- Wiring `SD` to a GPIO and having it default HIGH (enabled) at boot,
  before I2S/silence is confirmed — defeats the whole point of a manual
  gate.
- Losing track of `SD`'s physical position across sessions (it's not
  software-visible) — always re-confirm it's at GND before a fresh
  bring-up session, don't assume from a previous session's end state.

## Applies to future projects?

Partially — the general principle (gate amplifier enable on confirmed
safe digital state, whether manual or GPIO-driven) is reusable; the
specific "leave it manual during bring-up" choice is a bring-up-phase
decision, not necessarily right for a shipped product.

## Related Sunny files

`src/SharedI2S.cpp`, `src/SpeakerTest.cpp` (`initSpeakerTest()`),
`docs/current/SPEAKER.md`, `docs/playbooks/MAX98357A_BRINGUP.md`.
