---
name: led-power-limiting
description: A software current estimate/limiter is a bring-up safety aid, not a substitute for correct electrical power design
metadata:
  type: lesson
---

# LED power limiting (software estimate)

## Problem

Prevent an addressable LED strip from being commanded to draw more
current than its supply can provide, without requiring exact hardware
current measurement equipment during firmware development.

## Root cause / discovery

Not an incident — a proactive design. `main.cpp`'s `applyPowerLimit()`
estimates current per frame from the sum of every LED's R+G+B channel
values against assumed per-channel/idle current constants
(`LED_MAX_MA_PER_CHANNEL`, `LED_IDLE_MA_PER_LED`, both `Config.h`), and
scales the whole frame down proportionally if the estimate exceeds a
configured cap (`LED_CURRENT_LIMIT_MA`).

## How it was verified

The estimator is exercised continuously during normal operation
(printing a rate-limited `[POWER]` warning when it throttles); it has
not been cross-checked against a real current meter in this project.

## Correct approach

Treat a software current estimator as a **bring-up safety aid**
layered on top of correct electrical design (adequately-rated supply,
correct wire gauge, common ground) — never as a substitute for it. The
per-channel current assumption is a standard approximation for
WS2812-class LEDs, not a measurement of the specific LEDs installed.
Cross-check the estimate against a real current meter before relying
on it for anything beyond bring-up.

## Common failure modes

- Treating "the software limiter didn't trigger" as proof the supply
  is adequate.
- Never validating the per-channel current constants against the
  actual installed LEDs, which can vary between WS2812-compatible
  parts.

## Applies to future projects?

Yes — general technique for any addressable-LED project without
dedicated current-sense hardware.

## Related Sunny files

`src/main.cpp` (`applyPowerLimit()`), `include/Config.h`,
`docs/current/POWER.md`.
