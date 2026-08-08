---
name: max98357a-gain-configuration
description: MAX98357A GAIN pin strapping changes hardware gain independent of digital amplitude — verify against the physical board, don't assume
metadata:
  type: lesson
---

# MAX98357A GAIN configuration

## Problem

Distinguish hardware gain (a fixed analog multiplier set by the `GAIN`
pin's strapping) from digital amplitude (the software-controlled
sample scale) — conflating the two makes loudness problems hard to
diagnose.

## Root cause / discovery

`GAIN` was undocumented against the physical board for a long stretch
of this project's speaker work — genuinely unknown whether it was
floating (a specific default gain per the MAX98357A datasheet) or tied
to a resistor/pin. It is currently tied to GND as a deliberate test
configuration, physically observed to improve loudness somewhat versus
the prior (undocumented/likely-floating) state.

## How it was verified

Physical A/B: tying GAIN to GND and listening, compared against the
previously untested/floating state. Not a datasheet-only claim — an
actual physical comparison. The improvement is real but not rigorously
measured (no dB figures recorded).

## Correct approach

Never assume a gain-pin state without physically inspecting the board.
Document GAIN's strapping explicitly, separately from any digital
amplitude constants in firmware (see
`docs/lessons/digital-volume-vs-hardware-gain.md`) — the two multiply
together, and changing one without knowing the other's state makes
loudness changes hard to reason about or reproduce.

## Common failure modes

- Assuming a floating pin defaults to a specific gain without checking
  the exact part's datasheet behavior for that pin under float.
- Attributing a loudness change entirely to a firmware amplitude
  change when the hardware gain strapping also changed (or vice
  versa) — always change and test one variable at a time.

## Applies to future projects?

Yes — general lesson for any amplifier IC with pin-strapped gain
options, not Sunny-specific.

## Related Sunny files

`docs/current/ELECTRICAL.md`, `docs/current/SPEAKER.md`,
`docs/playbooks/MAX98357A_BRINGUP.md`,
`docs/playbooks/SPEAKER_AUDIO_VALIDATION.md`.
