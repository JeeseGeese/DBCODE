# Playbook: DRV8833 Motor Bring-Up

## Wiring

IN1/IN2 to two validated GPIOs, VCC to a rail rated for the motor
(confirm — do not assume a logic rail is adequate; see
`docs/lessons/drv8833-motor-control.md`), GND common, motor to
OUT1/OUT2.

## Software — first test

1. `digitalWrite`-only control first (forward/reverse/stop/brake) —
   no PWM yet. Confirms the electrical/GPIO path independent of any
   PWM/timing complexity.
2. Never command an instantaneous reversal — always coast/stop first,
   for at least a brief, real interval, before the opposite direction.
3. Add a defensive maximum-continuous-energized-time backstop from the
   start, even during raw digitalWrite testing.

## If the motor doesn't move (or moves intermittently)

Work through causes in this order — mechanical causes are common and
easy to overlook in favor of electrical/firmware theories:

1. **Mechanical**: excessive/uneven load (belt tension, gearing bind,
   friction) — try turning the mechanism by hand; if it's stiff or
   uneven, that's a strong candidate before touching firmware. This
   was the actual, confirmed root cause of a real intermittent-failure
   investigation on this exact class of project (see
   `docs/lessons/drv8833-motor-control.md` and the archived
   `archive/motor_bringup/DRV8833_MOTOR_BRINGUP.md` for the full
   closure).
2. **Electrical**: `digitalRead()` readback matches commanded state?
   Any other GPIO/LEDC channel conflict? Supply voltage adequate under
   load (see `POWER_BROWNOUT_DEBUGGING.md`)?
3. **Firmware**: correct pin mapping, no accidental brake/coast state,
   no competing module also driving the same pins.

Do not declare a root cause (especially "undervoltage"/UVLO) without
direct evidence (a captured voltage measurement during the actual
failure) — see `docs/lessons/esp32-brownout-diagnosis.md`.

## Adding PWM speed control

Once digitalWrite control is confirmed: physically characterize the
minimum PWM duty that reliably starts and sustains movement on the
*actual installed mechanism* — do not assume a percentage from a
datasheet or a different build. Clamp all active-movement commands to
that floor; only genuine stop/coast may go below it.

## Safety invariants to enforce from day one

- No `delay()` in any motor-behavior module.
- No instantaneous polarity reversal, ever.
- A generic maximum-continuous-energized-time safety net, independent
  of each behavior's own timing logic.
- A single, reliable, unconditional emergency-stop path reachable from
  every state.

## See also

`docs/lessons/drv8833-motor-control.md`,
`docs/lessons/motor-current-noise-mitigation.md`,
`docs/lessons/non-blocking-firmware-architecture.md`, `docs/current/MOTOR.md`.
