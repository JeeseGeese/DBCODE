# Playbook: Power / Brownout Debugging

## First: check the free evidence

The ESP32 ROM bootloader unconditionally prints its reset reason on
every boot (e.g. `rst:0xf (BROWNOUT_RST)`) — check captured boot logs
before adding any new instrumentation. See
`docs/lessons/esp32-brownout-diagnosis.md`.

## Never disable the hardware brownout detector to make a symptom go away

That trades a visible, safe failure mode for silent, unsafe operation
under low voltage.

## Isolating shared-rail contention

If multiple current-hungry loads (motor, LEDs, amplifier) share one
supply rail:

1. Test each load individually at rest, then combined, watching for
   symptoms in the *other* loads (e.g. does LED brightness dip when
   the motor engages?).
2. If a multimeter/scope is available: measure `VIN`/`VCC` at each
   peripheral during idle, then during each load individually, then
   combined — record the minimum voltage observed.
3. Do not attribute a symptom to a shared-rail cause without at least
   the qualitative test in step 1 — "it's probably the power" is a
   hypothesis, not a finding, until compared against an isolated-load
   baseline.

## Check the incoming source-power path, not just on-board distribution

Added after Sunny's V1.1 investigation (2026-08-08, see
`docs/current/POWER.md`'s "Leading hypothesis" section): before assuming
the failure is in on-board wiring/distribution, check whether it's
actually the **incoming supply itself**. A cheap, high-value test:

1. Reproduce the failure condition on the current power source (record
   exactly how, including load combination and intensity).
2. Swap ONLY the incoming power source (e.g. computer USB → battery
   pack, or a different USB port/cable/charger) — change nothing else.
3. Repeat the exact same failure condition. If the symptom disappears,
   that's strong evidence pointing at the source-power path (port
   current limiting, cable resistance, source impedance) — but it does
   **not** by itself identify which specific link in that path is
   responsible. See `docs/lessons/power-diagnostic-evidence-vs-proof.md`
   before writing this up as a "confirmed" cause.

## Check breadboard/Dupont prototype wiring under load, not just at rest

If the current build uses a solderless breadboard and/or Dupont jumper
wiring (common during bring-up before a soldered/PCB revision exists):
breadboard strips and friction-fit connectors have load-dependent
contact resistance that a resting continuity check won't reveal. Check
every ground and power junction specifically under the transient load
that reproduces the symptom, not just at rest — see
`docs/lessons/breadboard-prototype-transient-load-risk.md`.

## Common failure modes

- Assuming a symptom is caused by whichever subsystem was most
  recently changed, without testing power isolation first.
- Adding decoupling capacitors as a first response without confirming
  the symptom actually correlates with rail noise (see
  `docs/lessons/audio-buzz-noise-diagnosis.md` for a case where
  decoupling was added but did not resolve the symptom, meaning the
  actual cause is still open) — and conversely, don't blame a capacitor
  for a coincidental result without ruling out confounding wiring
  changes made around the same time (see `docs/current/POWER.md`'s
  "1000µF bulk capacitor" section for a concrete example of exactly this
  confound).
- Documenting UVLO/brownout as a proven root cause without capturing
  supply voltage during the actual failure — see
  `docs/lessons/esp32-brownout-diagnosis.md`.
- Testing diagnostic/startup code (e.g. a hardware-test LED sequence)
  as if it were exempt from the same power-safety limiting normal
  operation uses — see `docs/lessons/led-power-limiting.md`'s HWTEST
  finding.

## See also

`docs/lessons/esp32-brownout-diagnosis.md`,
`docs/lessons/motor-current-noise-mitigation.md`,
`docs/lessons/common-ground-design.md`,
`docs/lessons/breadboard-prototype-transient-load-risk.md`,
`docs/lessons/power-diagnostic-evidence-vs-proof.md`,
`docs/lessons/led-power-limiting.md`, `docs/current/POWER.md`.
