---
name: max98357a-bringup
description: MAX98357A amplifier bring-up sequencing and the startup safety order
metadata:
  type: lesson
---

# MAX98357A amplifier bring-up

## Problem

Bring up a MAX98357A I2S amplifier without an audible pop/garbage burst
at power-up, and without risking driving an uninitialized/floating
input into an enabled amplifier.

## Root cause / discovery

The amplifier's `SD` (shutdown/enable) pin is manually moved by hand
between GND (disabled) and 3.3V (enabled) in this project — it is
**not** GPIO-driven by firmware. If `SD` is moved to enable the
amplifier before the I2S bus is confirmed transmitting real digital
silence, the amplifier could reproduce whatever garbage/floating state
was on its data line.

## How it was verified

The firmware primes the TX side with digital silence
(`i2s_zero_dma_buffer()`) immediately upon I2S init, before printing
`[SPEAKER] Digital silence active` — the documented startup sequence
requires waiting for that exact line before moving `SD` to 3.3V.
Followed consistently across all speaker bring-up sessions in this
project without a reported bad power-up event.

## Correct approach

Sequence: (1) initialize I2S with silence already primed, (2) confirm
via serial log that silence is actively transmitting, (3) only then
manually enable the amplifier. Keep `SD` at GND (or otherwise disabled)
for every step before that confirmation. See
`docs/playbooks/MAX98357A_BRINGUP.md` for the full step-by-step.

## Common failure modes

- Enabling the amplifier before firmware has initialized I2S at all —
  the data line may float or carry noise.
- Assuming "the code looks like it primes silence" is sufficient
  without confirming the actual boot-log line before touching `SD`.
- Connecting a speaker terminal to ground — MAX98357A output is
  bridge-tied (BTL); see `docs/lessons/speaker-bridge-tied-output.md`.

## Applies to future projects?

Yes — the general "confirm safe/silent state before enabling an
amplifier" sequencing is broadly reusable for any I2S Class-D amp
bring-up.

## Related Sunny files

`include/SharedI2S.h`/`.cpp`, `include/SpeakerTest.h`/`.cpp`,
`docs/current/SPEAKER.md`, `docs/playbooks/MAX98357A_BRINGUP.md`.
