# Playbook: GPIO Validation Before Wiring

Run before assigning any new GPIO to a peripheral, on any project.

## Checklist

- [ ] Pin is not already assigned to anything else in the project's
      canonical GPIO map (example: Sunny's `docs/current/GPIO_MAP.md`).
- [ ] Pin is not reserved for flash/PSRAM on this exact module variant
      (see `docs/lessons/n16r8-reserved-flash-psram-pins.md` for why
      this needs the *exact* variant's datasheet, not a generic one).
- [ ] Pin is not a boot-strapping pin, or its required state at reset
      is known and compatible with the new wiring.
- [ ] Pin is not a USB/JTAG-sensitive pin the board actually needs.
- [ ] If digital I/O: pull-up/pull-down requirement is known (internal
      vs. external resistor).
- [ ] If analog/PWM: peripheral capability on this exact pin confirmed
      (not every GPIO supports every peripheral function).
- [ ] Common ground with whatever is being wired to this pin is
      confirmed (see `docs/lessons/common-ground-design.md`).
- [ ] The new assignment is added to the project's canonical GPIO map
      **before** or **immediately after** wiring — never left
      undocumented "for now."

## After wiring

- [ ] Flash a minimal test that only exercises the new pin (read or
      write, whichever applies) before integrating it into the full
      application — isolates wiring problems from application logic.
- [ ] Confirm no boot-loop, no flash-corruption, no unexpected reset
      after adding the new peripheral.

## See also

`docs/lessons/esp32-s3-gpio-selection.md`, `docs/current/GPIO_MAP.md`
(Sunny's example of a maintained canonical map).
