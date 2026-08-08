# Sunny V1 — Known Limitations

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/KNOWN_LIMITATIONS.md`.

Everything here is genuinely open at the V1 baseline. Treat every
"validated" claim elsewhere in `docs/V1/` as scoped exactly to what it
says, not as a signal the project is close to "done."

## Software-validated only (not yet physically validated)

- **`ExpressiveMotion` idle/audio-reactive movement** — timing tiers,
  weights, and cooldowns are conservative starting values, not tuned
  against the real mechanism. See `EXPRESSIVE_MOTION.md`.
- **`BehaviorEngine` personality states** — same status, its own
  physical tuning checklist not yet run.
- **Unified Audio Mode disable path** — Button 4 long-hold **enable**
  is physically confirmed; long-hold **disable**/safe-stop and the
  motor-ownership-rejection path/cue are not yet confirmed.

## Speaker (see `SPEAKER.md` for full detail)

- Residual buzz/static root cause is **not proven** — several
  hypotheses remain open (packing choice, I2S clock precision,
  electrical noise).
- Final volume/gain configuration is not approved for normal use — the
  physically-tested useful range (35-100%) and current GAIN=GND
  strapping are a bench-testing configuration, not a final decision.

## Motor / power

- **No dedicated external motor power supply** — the DRV8833 shares
  the 5V rail with LEDs and the amplifier (see `POWER.md`). Combined
  peak load and noise under sustained/production use has not been
  physically measured.
- **`DanceEngine`/`MusicMotorController` use no LED-power mitigation**
  by deliberate design — whether sustained high-duty PWM + full LED
  rendering causes disturbance/brownout/resets during real extended
  sessions has not been physically confirmed.
- No current-sensing or thermal-monitoring hardware exists anywhere —
  over-current, stall, and thermal conditions cannot be detected in
  software.
- The motor's occasional dead-stop-start-assist symptom remains
  undiagnosed (see `MOTOR.md`).

## Audio analysis

- No true FFT/spectral analysis anywhere — `lowFrequencyEnergy` and the
  derived low/mid/high control bands are heuristics, not measurements.
  Their noise-floor/max-RMS constants are not hardware-calibrated the
  way the main RMS ones are.

## Test coverage

- No host test currently covers `LedEffects.cpp`/`AudioOverlays.cpp`
  logic.

## Documentation

- `README.md` still embeds a very large amount of historical motor
  bring-up narrative inline (its "Motor driver (DRV8833)" section is
  roughly 1,400 lines). This V1 cleanup did **not** attempt to trim it
  — `docs/V1/MOTOR.md` is now the concise canonical reference; trimming
  README's inline history into `archive/` is a reasonable future
  cleanup step, not done in this pass (see the final report's "Next
  steps").
- `README.md`'s "Host-side regression tests" section still lists only
  the original 12 `MusicMotorController` test files by name; it has
  been updated as part of this cleanup to reflect all 18.
