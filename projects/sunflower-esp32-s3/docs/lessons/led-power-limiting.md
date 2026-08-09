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
- **Diagnostic/startup/hardware-test code bypassing the same
  power-safety path production rendering uses, "because it's just a
  test."** Found in this project (2026-08-08, V1.1 power investigation):
  `HardwareTest.cpp`'s boot-time HWTEST sequence wrote raw, unscaled
  `255,255,255` SOLID WHITE frames directly to the strip, skipping both
  normal brightness scaling and `applyPowerLimit()` entirely — an
  estimated ~2196mA unprotected current command at boot (at the
  corrected 36-LED count), and a plausible contributor to a reset-loop/
  stuck-white-LED debugging incident during that investigation (see
  `docs/current/POWER.md`'s "HWTEST power-safety bug" section). Fixed by
  routing every HWTEST frame through the SAME `applyPowerLimit()`
  function normal rendering uses, and physically verified afterward
  (`[POWER] Throttling: estimated 2196mA exceeds 1000mA limit, scaling
  by 0.46`). **General rule: test/diagnostic/startup code paths must use
  the same production power-safety mechanisms as normal operation, not a
  separate unprotected path.**

## Applies to future projects?

Yes — general technique for any addressable-LED project without
dedicated current-sense hardware.

## Related Sunny files

`src/main.cpp` (`applyPowerLimit()`), `src/HardwareTest.cpp`,
`include/Config.h`, `docs/current/POWER.md`.
