---
name: serial-dispatch-single-owner
description: Two independent Serial readers in the same firmware will race and silently drop bytes — root-caused and fixed on this exact project
metadata:
  type: lesson
---

# Serial diagnostics: single-owner dispatch

## Problem

An emergency-stop key (`k`) intermittently failed to cancel one
specific long-running diagnostic (~50% miss rate across isolated,
precisely-timed repeated trials), while working reliably for a
shorter-lived diagnostic using the identical cancellation plumbing.

## Root cause / discovery

The codebase had **two independent `Serial` consumers**: `main.cpp`'s
motor/LED interceptor (ran first each `loop()` iteration) and
`Controls.cpp`'s own separate `while (Serial.available())` loop
(called later, via `updateControls()`). A byte arriving in the gap
between the two calls was read by `Controls.cpp` first — silently
absorbed into its line buffer (no dispatch, since a lone `k` has no
trailing newline) and never seen by the interceptor. The longer-running
diagnostic simply gave the race far more `loop()` iterations to land
badly than the shorter one.

## How it was verified

Not inference — proof. Temporary counters showed a counter in
`Controls.cpp` that should always read zero (nothing there should ever
see a reserved byte) instead incremented in lockstep with every
observed `k`-miss, while the interceptor's own `k`-seen counter stayed
flat on those same misses. `strip.show()`'s own measured duration
stayed constant regardless of outcome, ruling out hardware/RMT timing
as the mechanism.

## Correct approach

`Serial.read()`/`available()` must be called from **exactly one place**
in the entire program. Every other module receives already-read bytes
via a feed function (`feedSerialByte(char)`), never reading `Serial`
independently. A reserved/emergency byte (`k`) is checked first,
unconditionally, before handing off to any line-buffering logic —
except while a word-command line is already mid-type, in which case it
belongs to that word instead (see the codebase's own `f` inside
"effects" collision, which a naive "always intercept reserved bytes"
fix broke before this refinement).

## Common failure modes

- Adding a second, seemingly-independent `Serial.available()` loop
  anywhere "just for this one feature" — reintroduces the exact race,
  even if it seems isolated.
- Fixing this kind of race with a plausible-sounding change and
  declaring victory without an isolated, repeated-trial test
  methodology (10/10 trials in each direction, spot checks across every
  relevant phase) — this project's own fix was validated that
  rigorously before being trusted.

## Applies to future projects?

Yes — extremely broadly reusable. Single-owner I/O consumption is a
general concurrency-safety principle, not Sunny- or embedded-specific.

## Related Sunny files

`src/main.cpp` (`pollSerialDispatcher()`), `src/Controls.cpp`
(`feedSerialByte()`), `docs/current/SOFTWARE_ARCHITECTURE.md`,
`docs/playbooks/SERIAL_DIAGNOSTICS.md`.
