---
name: power-diagnostic-evidence-vs-proof
description: A stronger supply making a power symptom disappear is strong diagnostic evidence, not proof of which exact component was failing — and isn't license to permanently degrade performance as a workaround
metadata:
  type: lesson
---

# Power diagnostic evidence vs. proof

## Problem

Two related traps when debugging an intermittent power/brownout
problem: (1) treating "the symptom went away when I changed X" as
proof that X was definitively the cause, and (2) reaching for a
permanent performance-reducing workaround (lower brightness cap, slower
motor, disabled feature) before the actual power path has been
characterized.

## Root cause / discovery

During Sunny's V1.1 power/brownout investigation (2026-08-08), the
strongest single finding was that swapping the ESP32's incoming power
source from the **computer's USB connection** to a **battery pack**
eliminated previously-reproducible brownouts under the same combined
LED + `MusicMotorController` load conditions. This is genuinely strong
evidence — but it does not, by itself, identify *which specific
component* in the computer-USB path was the limiting factor (the port's
own current limiting, the cable's resistance, the computer's internal
power management, or something else upstream). "The problem went away"
narrows the search space dramatically; it does not close it.

## How it was verified

The USB-vs-battery swap was a real, repeatable A/B observation (brownouts
reproducible on USB power, absent on battery power, under the same load).
It was **not** a controlled, multi-node voltage-measurement study — no
oscilloscope/multimeter capture at multiple points during the actual
transient was performed. See `docs/current/POWER.md`'s "Leading
hypothesis: incoming source-power capability" section for the full,
explicitly-hedged writeup.

## Correct approach

- State findings like this as "strong evidence" or "leading hypothesis,"
  never "confirmed root cause," until the specific failing
  component/conductor has been isolated (e.g. via controlled voltage
  measurement at each node, or systematically swapping only the cable,
  then only the port, etc.).
- Do not respond to an uncharacterized power problem by permanently
  reducing product performance (e.g. capping LED brightness or motor
  speed indefinitely) as a "fix" — that trades a real, fixable power
  issue for a permanently degraded product before the actual limiting
  factor is even known. A temporary, explicitly-labeled workaround
  during active investigation is different from a permanent design
  decision made to avoid characterizing the problem.
- Once a leading hypothesis like this exists, prioritize the cheap
  next step that would move it toward closure (here: voltage
  measurement, or a more granular A/B test) over a more expensive
  architectural response (e.g. a second dedicated supply) that the
  hypothesis hasn't yet justified.

## Common failure modes

- Documenting "battery pack fixed it" as "the computer USB port was
  defective" — neither the cable, the port, nor the computer's power
  delivery has been individually isolated.
- Using a strong-but-incomplete finding as license to stop
  investigating, when a cheap next measurement could turn a hypothesis
  into a confirmed cause.
- Reaching for a permanent performance cap as a substitute for
  electrical root-cause work, especially under time pressure — see
  `docs/lessons/motor-current-noise-mitigation.md` for the same caution
  already applied to the motor/LED shared-rail contention.

## Applies to future projects?

Yes — general embedded/electrical debugging methodology, not
Sunny-specific.

## Related Sunny files

`docs/current/POWER.md` (Current brownout investigation, section H),
`docs/lessons/esp32-brownout-diagnosis.md`,
`docs/lessons/breadboard-prototype-transient-load-risk.md`,
`docs/playbooks/POWER_BROWNOUT_DEBUGGING.md`.
