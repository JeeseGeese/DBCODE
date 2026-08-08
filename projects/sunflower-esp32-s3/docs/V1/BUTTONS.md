# Sunny V1 — Buttons

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/BUTTONS.md`.

Four momentary pushbuttons, each wired between its GPIO and GND, using
the ESP32's internal `INPUT_PULLUP` (no external resistors). Debounced
in `Controls.cpp`. Physically verified at the `v1.0.0` baseline and
unchanged since.

| Button | GPIO | Behavior |
|---|---|---|
| Mode | 10 | Single press: advance base effect **and** advance the selected (but not necessarily active) audio-overlay mode together. Double press: previous base effect only (does not touch overlay). |
| Mute | 11 | Toggles LED output off/on. Blanks the strip instantly; current effect/overlay selection is preserved, not reset. |
| Brightness | 17 | Cycles the 9-level brightness table. |
| Button 4 (Audio) | 5 | Short press: toggle the LED audio-reactive overlay ON/OFF (green flash = on, double red flash = off). Long hold (~900ms): toggle the unified **Audio Mode** — LED overlay + `MusicMotorController` together (see `MOTOR.md`). |

## Serial equivalents

`n`/`p` (base effect next/previous), `o` (advance overlay mode), `x`
(toggle overlay on/off), `+`/`-` (brightness up/down), `m` (mute
toggle) — see `docs/development/LED_AUDIO_QUICK_REFERENCE.md` for the
full current serial command surface.

## Emergency stop independence

`k` (emergency stop) is checked first, unconditionally, in the central
serial dispatcher — independent of all button processing. No button
action can block or delay it.

## Future buttons (not implemented)

A future Button 5 (motion-mode cycling) and Button 6 (voice-prompt
toggle / push-to-talk) are documented as **design-only** in archived
planning material (`archive/superseded_docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`
section 22) — not wired, no GPIOs assigned. Any real implementation
must go through `docs/playbooks/GPIO_VALIDATION.md` and
`docs/playbooks/BUTTON_BRINGUP.md` first.
