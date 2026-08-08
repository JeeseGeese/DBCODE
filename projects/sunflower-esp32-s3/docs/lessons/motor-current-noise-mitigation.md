---
name: motor-current-noise-mitigation
description: A brushed DC motor sharing a supply rail with other loads causes measurable contention — the real fix is a dedicated supply, not software workarounds
metadata:
  type: lesson
---

# Motor current/noise mitigation

## Problem

A brushed DC motor sharing a 5V supply rail with WS2812 LEDs and an
I2S amplifier visibly disturbs the other loads when it engages.

## Root cause / discovery

Physically observed: motor engagement measurably disturbs LED output
while the two share the 5V supply (motor movement is reliable with
LEDs muted, inconsistent/weak while LEDs are actively rendering). No
exact voltage sag has been instrumented — the correlation with
shared-supply contention is observed, not measured in volts.

## How it was verified

Direct physical A/B: LED behavior differs measurably between
"LEDs muted during motor engagement" and "LEDs active during motor
engagement," repeatably.

## Correct approach

The only real fix is a dedicated external motor power supply,
common-grounded with the rest of the system, sized for actual current
draw including startup inrush — not yet done in this project (see
`docs/current/POWER.md`). In the meantime, a software workaround
(`MotorPowerGuard`: mute LEDs immediately before motor engagement,
restore shortly after) reduces the visible symptom for bench
development — explicitly documented as a **workaround, not a fix**,
and deliberately **not** applied to every motor-driving module (see
below).

## Common failure modes

- Treating a software mute-around-motor-engagement workaround as
  having "solved" the power problem — it hides one visible symptom
  (LED flicker) without addressing the underlying shared-rail
  contention, which may still affect other loads (e.g. the amplifier).
- Applying a workaround inconsistently and not documenting which
  modules use it (in this project, `DanceEngine`/`MusicMotorController`
  deliberately do not, by explicit design decision — see
  `docs/current/POWER.md`) — creates confusion about which behaviors are
  "protected" and which aren't.

## Applies to future projects?

Yes — general lesson for any small embedded system sharing a motor and
other current-sensitive loads on one supply rail.

## Related Sunny files

`include/MotorPowerGuard.h`/`.cpp`, `docs/current/POWER.md`,
`docs/current/MOTOR.md`, `docs/playbooks/POWER_BROWNOUT_DEBUGGING.md`.
