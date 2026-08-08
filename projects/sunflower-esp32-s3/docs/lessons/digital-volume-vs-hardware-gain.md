---
name: digital-volume-vs-hardware-gain
description: Two multiplicative loudness controls exist (digital sample amplitude and analog hardware gain) — vary and record them independently
metadata:
  type: lesson
---

# Digital volume vs. hardware gain

## Problem

Total loudness is the product of two independent controls — the
digital sample amplitude (a firmware constant/scale, e.g. "5% of
full-scale") and the amplifier's own hardware gain (e.g. the
MAX98357A's `GAIN` pin strapping). Changing only one, or not tracking
which was changed, makes loudness results hard to reproduce or reason
about.

## Root cause / discovery

Sunny's speaker work changed both independently over time: digital
amplitude was made runtime-adjustable via a bench "volume ladder"
(`speaker v`/`+`/`-`), while `GAIN` was separately strapped to GND as a
one-time hardware change. Conflating "turned it up" without specifying
which control was ambiguous in early notes.

## How it was verified

Once separated and each documented explicitly (see
`docs/current/SPEAKER.md`/`docs/current/ELECTRICAL.md`), the physical
observations became reproducible and attributable — e.g. "35-100%
digital amplitude is useful, at the current GAIN=GND hardware
strapping" is a complete, reproducible statement; "it got louder" is
not.

## Correct approach

Document both values explicitly, together, for every loudness
observation: the digital amplitude fraction/percent AND the hardware
gain configuration. When testing loudness or distortion, change one at
a time and record both.

## Common failure modes

- Reporting "I turned it up" without saying which control.
- Assuming a digital-amplitude-only test (like a volume ladder) fully
  characterizes loudness/distortion behavior without also noting the
  hardware gain it was run at — the same ladder run at a different
  GAIN strapping is not the same test.

## Applies to future projects?

Yes — general lesson for any system with both a software amplitude
scale and a hardware-strapped analog gain.

## Related Sunny files

`docs/current/SPEAKER.md`, `docs/current/ELECTRICAL.md`,
`include/Config.h` (`SPEAKER_BENCH_VOLUME_STEPS_FRACTION`).
