---
name: non-blocking-firmware-architecture
description: No delay() in any behavior module — millis()-based state machines plus a defensive max-energized safety net
metadata:
  type: lesson
---

# Non-blocking firmware architecture

## Problem

A single-threaded `loop()` firmware needs to run LED rendering, audio
capture, motor behaviors, and serial dispatch all "simultaneously" —
any blocking call in one starves every other subsystem for its
duration.

## Root cause / discovery

Enforced as a hard rule across this project's behavior/controller
modules from early on: **no `delay()` anywhere** in
`MotorBehavior`/`MotorPowerGuard`/`MotorPriorityMode`/
`MotorPwmCalibration`/`DanceEngine`/`MusicMotorController`/
`ExpressiveMotion`/`BehaviorEngine`/`LedEffects`/`AudioOverlays`. Every
one of these is a `millis()`-based state machine, advanced once per
`loop()` call.

## How it was verified

Verified by direct source inspection (grep for `delay(`) at multiple
points in this project's history, and indirectly by the fact that
serial/button/LED/audio responsiveness never degrades while any of
these modules are active.

## Correct approach

Model every multi-step behavior as an explicit state enum plus stored
timestamps, advanced by comparing `now - startedAt` against a
threshold — never by sleeping. Add a defensive maximum-duration
backstop (this project's `MOTION_MAX_ENERGIZED_MS` ≈2000ms and
equivalents) that force-stops any state that somehow overran its own
intended timing, as a safety net, not the primary timing mechanism.

## Common failure modes

- A "just this once" `delay()` in a new module — silently stalls
  every other subsystem sharing the same `loop()`, including safety-
  critical ones like emergency stop.
- A state machine with no maximum-duration backstop — a logic bug that
  fails to transition out of an energized state has no safety net.
- Confusing "non-blocking" with "instant" — non-blocking state
  machines still take real wall-clock time to complete a multi-step
  sequence; the point is that `loop()` keeps returning control between
  steps, not that the sequence itself is free.

## Applies to future projects?

Yes — fundamental to any single-threaded embedded `loop()` design with
multiple concurrent responsibilities.

## Related Sunny files

Every module listed above, `docs/current/SOFTWARE_ARCHITECTURE.md`,
`docs/current/MOTOR.md` (the max-energized safety net specifically).
