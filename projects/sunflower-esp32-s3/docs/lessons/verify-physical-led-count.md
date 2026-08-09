---
name: verify-physical-led-count
description: Verify physical LED count independently of firmware configuration before using firmware current estimates for power diagnosis
metadata:
  type: lesson
---

# Verify physical LED count independently of firmware configuration

## Problem

Sunny's firmware carried `NUM_LEDS=58` from early bring-up through the
`sunny-v1-baseline` tag. The strip physically connected to Sunny has 36
LEDs. `NUM_LEDS` was never re-derived from a physical count after the
initial (apparently mistaken, or later-changed) value was set — it was
simply carried forward, including through a documented-but-unverified
intermediate theory ("a 42-LED assembly... has been physically
connected," see `Config.h`'s git history) that was itself never
independently confirmed either.

## Root cause / discovery

Found during the Sunny V1.1 LED-count audit (2026-08-08), triggered by
the user directly reporting the physical count (36) rather than by any
in-firmware detection — **this firmware has no way to detect the real
LED count**; WS2812 strips have no MCU-readable length. Every `NUM_LEDS`
value in this project's history was a human-entered constant, not a
measurement.

## How it was verified

Physical count reported directly by the user with hands on the
hardware. Not independently re-derived or measured by this session —
see `docs/current/HARDWARE_ARCHITECTURE.md`'s correction note.

## Correct approach

Before trusting ANY firmware-derived current/power estimate for
diagnosing a real electrical problem (brownouts, resets, supply
sizing), **verify the physical LED count by direct inspection or count,
independently of whatever `NUM_LEDS`/`LED_COUNT` the firmware currently
reports** — driving more logical pixels than physically exist is
harmless (WS2812 data past the last real LED simply has nothing to land
on), but a software power *estimate* based on an inflated logical count
will overestimate real current draw, and one based on an undercounted
logical value will underestimate it. Either direction can mislead a
power investigation if the assumed count doesn't match the real strip.

Concretely for this project: `NUM_LEDS` in `include/Config.h` is the
single source of truth every render loop, buffer, and
`main.cpp`'s `applyPowerLimit()` estimator reads — correcting it there
alone was sufficient to fix every current-count-dependent computation in
the firmware (all of them already referenced the macro, none hardcoded
a literal LED count — see the V1.1 LED-count audit report for the full
file-by-file check).

## Common failure modes

- Trusting a firmware-reported `[HWTEST] LED count configured: N` line
  as physically verified just because it's printed confidently at boot.
- Using a stale/incorrect LED count to compute an "estimated current"
  figure and treating that estimate as proof (or disproof) of a
  brownout's root cause — the estimate is only as good as the count it's
  built on, and neither direction of count error is safe to assume away.
- Assuming a previously-documented "physically connected assembly"
  count (like Sunny's earlier, itself-unconfirmed "42-LED assembly"
  note) is more trustworthy than the firmware's own `NUM_LEDS` just
  because it sounds more specific — an unverified number is unverified
  regardless of how precisely it's stated.

## Applies to future projects?

Yes — any addressable-LED project where the firmware's configured pixel
count could ever drift from the physically-connected hardware.

## Related Sunny files

`include/Config.h` (`NUM_LEDS`), `src/main.cpp` (`applyPowerLimit()`),
`src/HardwareTest.cpp` (`[HWTEST] LED count configured:` line),
`docs/current/HARDWARE_ARCHITECTURE.md`,
`docs/current/POWER.md` (current brownout investigation),
`docs/lessons/ws2812-power-data-separation.md`.
