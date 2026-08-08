---
name: drv8833-motor-control
description: DRV8833 direction control, safe reversal, and the M80 minimum-reliable-PWM finding
metadata:
  type: lesson
---

# DRV8833 motor control

## Problem

Drive a brushed DC motor bidirectionally through a DRV8833 H-bridge
safely (no instantaneous polarity reversal, no indefinite energization)
and at a speed that reliably moves it.

## Root cause / discovery

Two independent findings: (1) commanding an instantaneous reversal
(HIGH/LOW swapped directly) is electrically risky and was designed out
from the start — every reversal in this project's firmware inserts an
explicit coast/stop segment first; (2) below 80% PWM duty
("M80"), movement was **not reliably dependable** on the physically
installed motor/mechanism — established through direct physical PWM
characterization (`MotorPwmCalibration`'s manual/ramp/cycle tests).

## How it was verified

M80 as the minimum reliable floor was confirmed via direct physical
testing across multiple `MusicMotorController` revisions and is
enforced as a hard floor in the production dancing engine. No-
instantaneous-reversal is verified by code inspection at every
revision (a real `STOP`/coast segment between any two directional
commands in every pattern/state table).

## Correct approach

`IN1`/`IN2` = `HIGH`/`LOW` for forward, `LOW`/`HIGH` for reverse,
`LOW`/`LOW` to coast, `HIGH`/`HIGH` to brake. Never transition directly
between forward and reverse — always coast/stop first. Establish a
minimum reliable PWM duty empirically for the actual installed
mechanism (belt tension, gearing, load) rather than assuming a
percentage from a datasheet or a different build.

## Common failure modes

- Reversing direction with a single instantaneous GPIO state change —
  risks driver/motor stress and audible mechanical shock.
- Assuming a PWM duty that "should" move a motor (e.g. 30-50%) without
  physically confirming it's above the real static-friction/breakaway
  threshold for the specific installed mechanism.
- No safety backstop on maximum continuous energized time — see
  `docs/lessons/non-blocking-firmware-architecture.md` and
  `docs/current/MOTOR.md`'s `MOTION_MAX_ENERGIZED_MS` note.

## Applies to future projects?

Yes — general H-bridge brushed-DC-motor control discipline; the exact
M80 number is Sunny-specific (a physical property of this mechanism),
marked as an example, not a universal constant.

## Related Sunny files

`include/MotorDriver.h`/`.cpp`, `include/MusicMotorController.h`,
`docs/current/MOTOR.md`, `docs/playbooks/DRV8833_MOTOR_BRINGUP.md`.
