---
name: breadboard-prototype-transient-load-risk
description: Solderless breadboard/Dupont prototypes can behave materially worse under transient current load than permanent low-resistance wiring
metadata:
  type: lesson
---

# Breadboard/Dupont prototype transient-load risk

## Problem

A circuit that works fine under steady-state or low-current conditions
on a solderless breadboard with Dupont jumper wiring can still fail
under **transient** high-current load (a motor reversal, an LED
brightness step) even when nothing about the steady-state wiring looks
wrong.

## Root cause / discovery

Sunny's current prototype uses a solderless breadboard/prototype-PCB
environment, Dupont jumper wiring, and relatively long temporary
interconnects in places. During the V1.1 power/brownout investigation
(2026-08-08), the most reproducible brownout condition involved
transient, combined high-current events (LED load + motor reversals,
especially near M100). Breadboard contact strips and Dupont
friction-fit connectors have nontrivial, load-dependent contact
resistance that steady-state continuity checks don't reveal — a
connection that measures fine at rest can still sag momentarily under a
fast current transient.

## How it was verified

Not fully isolated with instrumentation (no oscilloscope/multimeter
transient capture performed) — this is a physically-motivated
hypothesis grounded in the prototype's construction, reinforced by a
separate, stronger finding from the same investigation: swapping the
incoming power source (computer USB → battery pack) eliminated the
observed brownouts under the same load conditions (see
`docs/lessons/power-diagnostic-evidence-vs-proof.md` and
`docs/current/POWER.md`'s "Leading hypothesis" section) — which shows
the *supply path*, not necessarily the breadboard wiring itself, may be
the dominant factor in this specific case. Breadboard/Dupont contact
resistance remains a contributing suspect, not a confirmed cause.

## Correct approach

Treat a breadboard/Dupont prototype as inherently more suspect than
soldered wiring for any transient-load symptom (reset, brownout,
flicker, glitch) — check every ground and power junction under load,
not just at rest, before attributing the symptom to a component or
architectural cause. Prioritize ruling out prototype wiring quality
before considering a redesign (see
`docs/current/POWER.md`'s "Investigation list").

## Common failure modes

- Concluding a component or architectural cause from a symptom that
  only appears under transient load, without first ruling out
  breadboard/Dupont contact quality specifically.
- Treating a steady-state continuity check as sufficient evidence a
  connection is fine — it doesn't test transient/load-dependent contact
  resistance.
- Skipping straight to a hardware redesign (e.g. a second dedicated
  supply) before cheaper prototype-wiring checks have been done.

## Applies to future projects?

Yes — any breadboard/Dupont-wired prototype with transient high-current
loads (motors, LED strips, actuators), not Sunny-specific.

## Related Sunny files

`docs/current/POWER.md` (Current brownout investigation, sections E and
"Investigation list"), `docs/current/ELECTRICAL.md`,
`docs/lessons/common-ground-design.md`,
`docs/lessons/power-diagnostic-evidence-vs-proof.md`.
