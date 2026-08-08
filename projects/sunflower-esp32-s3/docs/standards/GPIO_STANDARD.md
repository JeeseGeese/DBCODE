# Standard — GPIO Assignment

Prescriptive rule for this project (and a reasonable default for other
Do Better firmware projects). Derived from
`docs/lessons/esp32-s3-gpio-selection.md` and
`docs/lessons/n16r8-reserved-flash-psram-pins.md`.

## Rule

1. Every GPIO in active use **must** appear in exactly one place: the
   project's canonical GPIO map (`docs/current/GPIO_MAP.md`). No GPIO
   assignment is valid unless it's recorded there.
2. Before assigning a new GPIO, run
   `docs/playbooks/GPIO_VALIDATION.md`'s full checklist — reserved
   flash/PSRAM pins (checked against the *exact* module variant, not a
   generic reference), boot-strapping pins, USB/JTAG pins, and every
   pin already in the canonical map.
3. Every hardware resource (not just a pin — the peripheral it
   belongs to, e.g. an I2S controller) has exactly **one owning
   module**. Every other module reaches it only through that module's
   public API. Never a second `i2s_driver_install()`, never a second
   direct `digitalWrite()` on a pin another module owns.
4. Document the owner in the canonical GPIO map's "Owner" column at
   the same time the pin is assigned — not as a follow-up.
5. Unassigned/future pins are never silently "reserved" by
   convention alone — if a future feature is expected to need a pin,
   say so explicitly in the map (see `docs/current/GPIO_MAP.md`'s
   "Unassigned" note) rather than leaving it undocumented.

## Rationale

See `docs/architecture/DESIGN_DECISIONS.md`'s "Why this GPIO layout?"
and `docs/architecture/SOFTWARE_ARCHITECTURE.md`'s "Pattern 1:
single-owner hardware resources."
