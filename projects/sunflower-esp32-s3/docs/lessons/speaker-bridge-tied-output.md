---
name: speaker-bridge-tied-output
description: MAX98357A speaker output is bridge-tied (BTL) — never connect either terminal to ground
metadata:
  type: lesson
---

# Speaker bridge-tied (BTL) output

## Problem

A naive assumption that one speaker terminal is "ground-referenced"
(like many simple audio outputs) can lead to shorting an amplifier
output to ground, and to unsafe oscilloscope probing.

## Root cause / discovery

The MAX98357A drives its speaker output bridge-tied (BTL) — **neither**
SPK+ nor SPK− is ground-referenced. A standard oscilloscope probe
referenced to ground can short one output leg to ground through the
probe itself if used carelessly on a BTL output.

## How it was verified

Documented directly from the MAX98357A's known output topology;
enforced in this project's own wiring rule (speaker connects only
between SPK+ and SPK−, never to ground) and its Stage S0 preflight
checklist (`docs/SPEAKER_BRINGUP_PLAN.md`, archived).

## Correct approach

Wire the speaker only between the amplifier's two dedicated output
terminals. If probing is ever needed, use equipment/technique rated
for differential (non-ground-referenced) measurement, not a standard
single-ended scope probe.

## Common failure modes

- Connecting one speaker terminal to system ground "for a return
  path," which doesn't exist on a BTL output and can damage the
  amplifier.
- Ground-referenced oscilloscope probing that shorts one leg to ground
  through the probe.

## Applies to future projects?

Yes — general Class-D BTL amplifier wiring/measurement rule, not
Sunny-specific.

## Related Sunny files

`docs/current/ELECTRICAL.md`, `docs/current/SPEAKER.md`,
`docs/playbooks/MAX98357A_BRINGUP.md`.
