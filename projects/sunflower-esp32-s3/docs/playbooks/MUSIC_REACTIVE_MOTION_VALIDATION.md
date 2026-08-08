# Playbook: Music-Reactive Motion Validation

For validating a motor/actuator behavior driven by live audio-energy
analysis (beat/intensity detection) rather than fixed choreography.

## Before physical testing

- [ ] Confirm the safety invariants independently of any audio logic:
      no `delay()`, no instantaneous reversal, a max-energized-time
      backstop, a reliable emergency stop reachable from every state
      (see `docs/lessons/drv8833-motor-control.md` and
      `docs/lessons/non-blocking-firmware-architecture.md`).
- [ ] Confirm a minimum-reliable-movement floor has been physically
      established for the actual mechanism (see
      `DRV8833_MOTOR_BRINGUP.md`) and is enforced as a hard clamp.
- [ ] Confirm host-tested pure decision logic (band classification,
      threshold hysteresis, reversal-gating) passes before ever
      touching real hardware — see
      `docs/lessons/host-tests-for-embedded-firmware.md`.

## Physical test progression

1. **Single test song/passage, one genre** — confirm the basic
   behavior (does it move when the music is energetic, rest when
   quiet) before anything more nuanced.
2. **A/B a specific feature** (e.g. drop detection) with an explicit
   toggle so the same firmware image can be compared with it on vs.
   off — avoids rebuilding between comparisons and keeps the
   comparison honest.
3. **Wider genre/song variety** only after the single-song behavior is
   judged correct — tuning against one song risks overfitting
   thresholds that don't generalize.
4. **Extended/unattended session** only after shorter supervised runs
   show no runaway state (a decision engine that gets "stuck" in an
   energized state under some rare input combination is the main risk
   class here).

## What to record per test

- Which song/passage, roughly how long.
- Specific moments that looked wrong (too aggressive, too timid, wrong
  reaction to a specific event) with enough detail to reproduce (e.g.
  "reversed during the buildup at ~1:40, should have waited for the
  drop").
- Whether any safety invariant was ever visibly violated (instant
  reversal, no reaction to emergency stop, motor left energized during
  silence).

## Common failure modes

- Tuning thresholds against one song's specific energy profile and
  calling the system "done" — genre-dependent thresholds are a known,
  expected limitation, not a bug, but must be documented as such (see
  `docs/current/MOTOR.md`'s note on Sunny's single-genre-profile
  limitation).
- Treating a passing host test suite as sufficient without ever
  running the real mechanism against real music.

## See also

`docs/current/MOTOR.md` (`MusicMotorController`'s architecture),
`docs/lessons/drv8833-motor-control.md`,
`docs/lessons/host-tests-for-embedded-firmware.md`.
