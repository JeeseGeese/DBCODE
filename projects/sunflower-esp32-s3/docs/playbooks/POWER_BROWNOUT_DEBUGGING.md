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

## Common failure modes

- Assuming a symptom is caused by whichever subsystem was most
  recently changed, without testing power isolation first.
- Adding decoupling capacitors as a first response without confirming
  the symptom actually correlates with rail noise (see
  `docs/lessons/audio-buzz-noise-diagnosis.md` for a case where
  decoupling was added but did not resolve the symptom, meaning the
  actual cause is still open).
- Documenting UVLO/brownout as a proven root cause without capturing
  supply voltage during the actual failure — see
  `docs/lessons/esp32-brownout-diagnosis.md`.

## See also

`docs/lessons/esp32-brownout-diagnosis.md`,
`docs/lessons/motor-current-noise-mitigation.md`,
`docs/lessons/common-ground-design.md`, `docs/current/POWER.md`.
