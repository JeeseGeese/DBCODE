---
name: host-tests-for-embedded-firmware
description: Mirror pure decision logic into standalone host-compiled programs when no on-target test runner exists
metadata:
  type: lesson
---

# Host tests for embedded firmware

## Problem

No PlatformIO `test` environment is set up for this project, but pure
decision logic (state machines, threshold/band classification, timing
arithmetic) benefits enormously from fast, deterministic regression
testing — running only on real hardware for every change is slow and
non-deterministic.

## Root cause / discovery

Not a failure — a deliberately adopted pattern: 18 standalone,
independent g++ programs in `test_host/`, each mirroring the relevant
constants/enums/pure functions inline (no Arduino/ESP-IDF dependency,
no shared build system between files), compiled and run individually
with plain `g++ -std=c++17 -Wall -Wextra`.

## How it was verified

The pattern has repeatedly caught real regressions before physical
testing — e.g. a `SUSTAINED_DRIVE` deadlock (stale array value vs.
live direction) was reproduced and proven fixed via
`music_motor_sustained_drive_deadlock.cpp` before ever touching real
hardware.

## Correct approach

For any new host-testable pure logic: mirror the exact
constants/enums/functions into a new standalone `test_host/*.cpp` file
following the existing files' documented pattern (each file states its
own single-file build/run command in its header comment). Require
`PASS: 0 failure(s)` with **zero compiler warnings** before considering
a change in that area done. When a bug is found via physical testing,
add a host test that would have caught it, wherever the logic is
host-testable at all — this project has direct historical examples of
this exact pattern.

## Common failure modes

- Skipping host tests for logic that's genuinely host-testable "since
  it's small" — small logic bugs (a stale variable, an off-by-one
  threshold) are exactly what this pattern is cheap and fast at
  catching.
- Letting host tests accumulate compiler warnings — treat warnings as
  failures, not noise.
- Assuming host-test-passing is equivalent to physical validation — it
  proves the pure logic in isolation, nothing about real timing,
  electrical behavior, or mechanical response (see
  `docs/current/TESTING.md`'s three-tier validation policy).

## Applies to future projects?

Yes — broadly reusable pattern for any embedded project without an
on-target test runner, especially one using PlatformIO/Arduino where
setting up a full native test environment is nontrivial.

## Related Sunny files

`test_host/*.cpp` (18 files), `docs/current/TESTING.md`.
