# Playbook: Speaker Audio Validation

For characterizing a bring-up-complete speaker/amplifier's real
behavior, after `MAX98357A_BRINGUP.md` (or equivalent) is done.

## Build a progressive amplitude ladder, not one fixed tone

1. Choose a set of digital amplitude levels spanning low to full scale
   (example: Sunny used 2/5/8/12/18/25/35/50/65/80/100%).
2. At each level, play multiple frequencies (a single tone can hide
   frequency-specific resonance/distortion) — include at least a low,
   mid, and high tone, plus a short musical/multi-tone passage if the
   application will play music/speech.
3. Fade in/out at every tone boundary (10-20ms) to avoid clicks that
   could be mistaken for the thing you're diagnosing.
4. Always return to genuine digital silence between levels — never
   jump amplitude mid-tone.
5. Abort immediately and return to silence on any I2S write fault
   (error, zero-byte, or partial write) — don't let an automated
   multi-minute ladder run drive garbage into the amplifier
   unattended.

## Record, per level

- Perceived loudness (qualitative is fine if no SPL meter is
  available).
- Whether distortion is audible, and how it changes with level
  (louder/quieter/unchanged relative to the signal).
- Any buzz/hiss/static, and whether it scales with level or stays
  constant (constant-regardless-of-level buzz suggests a different
  cause than level-dependent distortion — see
  `docs/lessons/audio-buzz-noise-diagnosis.md`).
- Whether the amplifier or power system shows any instability (audible
  clicking, brief cutout, an ESP32 reset).

## Isolate variables

- Test at idle (no other loads active) vs. with other current-hungry
  loads active (motor, bright LEDs) — see
  `POWER_BROWNOUT_DEBUGGING.md`.
- If a digital-format A/B diagnostic exists (e.g. multiple bit-packing
  options), test each independently at the same amplitude/frequency
  for a fair comparison.
- Record the hardware gain configuration alongside every result (see
  `docs/lessons/digital-volume-vs-hardware-gain.md`) — a result is not
  reproducible without it.

## See also

`docs/lessons/speaker-output-testing-methodology.md`,
`docs/lessons/digital-volume-vs-hardware-gain.md`, `docs/current/SPEAKER.md`
(Sunny's example results).
