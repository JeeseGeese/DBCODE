---
name: button-input-pullup-wiring
description: Wiring momentary buttons with INPUT_PULLUP and no external resistors
metadata:
  type: lesson
---

# Button INPUT_PULLUP wiring

## Problem

Wire multiple momentary pushbuttons cheaply and reliably without a
resistor per button.

## Root cause / discovery

Not a failure investigation — a working default applied consistently.
Each of Sunny's 4 buttons is wired between its GPIO and GND, using the
ESP32's internal pull-up (`INPUT_PULLUP`), so a press pulls the pin
LOW. No external resistors needed.

## How it was verified

Physically confirmed working for all 4 buttons at the original
`v1.0.0` baseline (see `docs/current/BUTTONS.md`) — debounced edge/hold/
double-click detection all function correctly in `Controls.cpp`.

## Correct approach

`pinMode(pin, INPUT_PULLUP)`, treat LOW as pressed, debounce in
software (time-based, not a hardware RC filter). Keep debounce/click-
timing logic in one small, reusable layer that produces semantic
events (press/release/double-click) rather than scattering raw pin
reads through the codebase.

## Common failure modes

- Forgetting `INPUT_PULLUP` and reading a floating pin.
- Debounce logic that blocks (`delay()`) instead of using `millis()`
  timestamps — breaks every other non-blocking subsystem sharing the
  same `loop()`.
- No debounce at all — a mechanical button bounces and produces
  multiple spurious edges per physical press.

## Applies to future projects?

Yes — broadly reusable, standard practice, not Sunny-specific.

## Related Sunny files

`src/Controls.cpp` (`buttonPressedEdge()`, `pollClickTracker()`),
`docs/current/BUTTONS.md`, `docs/playbooks/BUTTON_BRINGUP.md`.
