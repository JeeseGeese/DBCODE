# Playbook: Momentary Button Bring-Up

## Wiring

1. One terminal to a validated GPIO (see `GPIO_VALIDATION.md`), one to
   GND.
2. `pinMode(pin, INPUT_PULLUP)` — no external resistor needed for a
   simple momentary button.
3. Pressed = pin reads LOW.

## Software

1. Debounce in software using `millis()` timestamps — never a blocking
   `delay()`-based debounce (see
   `docs/lessons/non-blocking-firmware-architecture.md`).
2. Produce semantic events (press edge, release edge, hold, double-
   click) from one small, reusable layer — don't scatter raw pin reads
   through application logic.
3. If the project has an emergency-stop or other safety-critical
   input, verify it is checked independently of and with priority over
   ordinary button processing (see `SERIAL_DIAGNOSTICS.md` for the
   serial-input analog of this same principle).

## Test procedure

1. Single press each button — confirm exactly one event per physical
   press, no double-fires from contact bounce.
2. Rapid repeated presses — confirm no missed or doubled events.
3. Hold for several seconds — confirm a long-press action (if any)
   fires exactly once, not repeatedly while held.
4. If any button gates a physically hazardous action (motor, high
   LED brightness), confirm the gated action never fires from button
   noise/bounce alone.

## See also

`docs/lessons/button-input-pullup-wiring.md`, `docs/current/BUTTONS.md`
(Sunny's example).
