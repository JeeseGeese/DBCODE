# Standard — Power

Prescriptive rule for this project. Derived from
`docs/lessons/ws2812-power-data-separation.md`,
`docs/lessons/common-ground-design.md`,
`docs/lessons/motor-current-noise-mitigation.md`,
`docs/lessons/led-power-limiting.md`.

## Rule

1. **Never power a current-hungry peripheral (LEDs, motor, amplifier)
   from the MCU's own logic-rail pin.** Use a separately-rated supply.
2. **Common ground is mandatory** between every power rail in the
   system and the MCU — regardless of how many separate rails exist.
   Verify with continuity if in doubt.
3. Every peripheral's power rail assignment is recorded in
   `docs/current/POWER.md`/`ELECTRICAL.md`, distinguishing three
   states explicitly, in these exact terms:
   - **physically verified wiring** — what's actually connected and
     confirmed working.
   - **currently tested configuration** — a specific chosen setting
     among options (e.g. a gain strap), not necessarily final.
   - **future production recommendation** — not yet done.
4. A software current estimate/limiter (where applicable, e.g. LEDs)
   is a bring-up safety aid, never a substitute for correct electrical
   sizing — document it as such explicitly wherever it's mentioned.
5. Before claiming a shared-rail load combination is safe, test each
   load individually and combined (see
   `docs/playbooks/POWER_BROWNOUT_DEBUGGING.md`) — do not assume from
   datasheet current ratings alone.
6. Never disable a hardware brownout detector to suppress a symptom.
   Root-cause it instead.

## Rationale

See `docs/architecture/DESIGN_DECISIONS.md`'s "Why power limiting?"
and `docs/current/POWER.md` for this project's own current, unresolved
shared-5V-rail risk as a worked example of why this standard exists.
