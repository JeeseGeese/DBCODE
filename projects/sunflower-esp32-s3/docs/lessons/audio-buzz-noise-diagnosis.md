---
name: audio-buzz-noise-diagnosis
description: Systematically rule out digital-format bugs before assuming electrical noise, and don't claim a root cause that isn't proven
metadata:
  type: lesson
---

# Audio buzz/noise diagnosis

## Problem

A residual buzz/static on the MAX98357A + speaker output, present even
after the digital write path was confirmed 100%-success. Many possible
causes exist (digital packing, I2S clock precision, shared-rail
electrical noise, hardware gain) and it's tempting to guess one without
evidence.

## Root cause / discovery

**Not conclusively found as of the V1 baseline.** What *was* done: a
full source-level review of the sample-generation/packing code (mono/
stereo duplication, slot order, sign handling, overflow, phase
continuity across DMA buffer boundaries, double amplitude scaling)
found no active coding defect. One real (if likely-inert on this
compiler) defect was found and fixed: a left-shift of a possibly-
negative signed value (undefined behavior pre-C++20), rewritten as a
well-defined unsigned-shift-then-reinterpret. A three-way A/B
diagnostic (`speaker fmt1`/`fmt2`/`fmt3`, testing 16-bit/24-bit/32-bit
packing within the same 32-bit I2S slot) was built specifically because
static code review alone could not settle which packing the amplifier
actually prefers.

## How it was verified

Systematic elimination, not a single test: static review ruled out
common digital-format bugs; the buzz was still present after that
review, meaning the remaining hypotheses (packing preference, I2S
clock precision at `use_apll=false`, and shared-5V-rail electrical
noise) are all still open, and none has independent supporting
evidence over the others yet.

## Correct approach

When diagnosing analog-sounding noise from a digital audio path: (1)
rule out digital/format bugs via careful code review first (cheap,
deterministic), (2) build an A/B diagnostic for any remaining
ambiguous choice rather than guessing, (3) separately test whether the
noise correlates with other loads on a shared power rail (motor/LED
activity) versus being present at idle, (4) never write up a
hypothesis as a "root cause" until it's been isolated with a control
condition.

## Common failure modes

- Declaring a root cause from a plausible-sounding theory without a
  test that could have falsified it.
- Fixing one thing (e.g. a code review finding) and assuming it
  resolved a symptom it was never actually shown to cause.
- Not testing whether noise correlates with shared-rail activity —
  the single most common actual cause of "buzz" in a small embedded
  system with a shared, undersized supply.

## Applies to future projects?

Yes — general audio-noise-diagnosis discipline, broadly reusable.

## Related Sunny files

`docs/current/SPEAKER.md`, `src/SpeakerTest.cpp` (`speaker fmt1/fmt2/fmt3`),
`test_host/speaker_fmt_diag.cpp`,
`docs/playbooks/POWER_BROWNOUT_DEBUGGING.md`.
