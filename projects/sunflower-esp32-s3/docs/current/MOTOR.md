# Sunny — Motor (DRV8833) Status

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

**The motor subsystem is fully integrated into Sunny, not a future
milestone.** This file documents the current active architecture. For
the detailed historical bring-up investigation (belt-preload root
cause, the `k`-miss race condition, PWM characterization attempts), see
`archive/motor_bringup/` — conclusions are extracted below.

## Wiring

See `ELECTRICAL.md`/`GPIO_MAP.md`. IN1=GPIO8, IN2=GPIO9, VCC=shared 5V
rail (corrected 2026-08-01 — see `POWER.md`), GND=common,
SLEEP/nSLEEP=not wired.

## Module stack

```
MotorDriver.cpp           sole owner of GPIO8/GPIO9 -- digitalWrite AND
                           LEDC-PWM primitives. Every layer above calls
                           only this module's exported functions.
        ^
        |
MotorPwmCalibration.cpp    manual/ramp/cycle PWM calibration bench tool
MotorBehavior.cpp          IDLE_SWAY / placeholder simple behaviors
ExpressiveMotion.cpp       idle/audio-reactive gentle movement patterns
BehaviorEngine.cpp         personality-state coordinator, ABOVE ExpressiveMotion
DanceEngine.cpp            superseded, gated off by default (see below)
MusicMotorController.cpp   production music-reactive dancing (Revision 10.1)
MotorPowerGuard.cpp        LED-mute coordination around motor engagement
MotorPriorityMode.cpp      boot-equivalent quiet-system diagnostic
```

Every motor-owning module checks `isAnyMotorDiagnosticActive()` before
starting, and is itself reflected by that check once active — a
bidirectional mutual-exclusion pattern used consistently across all of
them, so nothing can fight another module for the motor.

## `MotorDriver` — the electrical primitives

```cpp
void initMotor();                 // pins OUTPUT, forced LOW -- called first in setup()
void motorForward();              // IN1=HIGH, IN2=LOW
void motorReverse();              // IN1=LOW, IN2=HIGH
void motorStop();                 // IN1=LOW, IN2=LOW (coast)
void motorBrake();                // IN1=HIGH, IN2=HIGH (brake)
void motorForwardMs(uint32_t ms); // blocking helper
void motorReverseMs(uint32_t ms); // blocking helper

// PWM (LEDC), used by MotorPwmCalibration/MusicMotorController/DanceEngine:
uint32_t initMotorPWM(uint32_t freqHz, uint8_t resolutionBits);
void deinitMotorPWM();  // returns pins to plain digitalWrite LOW
void motorPWMForward(uint8_t duty);
void motorPWMReverse(uint8_t duty);
void motorPWMCoast();
```

`initMotor()` runs as the very first line of `setup()`, before
`Serial`/LED/mic init, so the pins never float.

## Startup-assist symptom (documented, not root-caused)

The motor sometimes needs a brief manual assist to start from a dead
stop, then runs normally once turning. Consistent with available
starting torque/current being close to the motor's static-friction
threshold at the current supply voltage — **not proven**, and
undervoltage lockout (UVLO) specifically must not be documented as a
confirmed root cause anywhere in this project (see
`archive/motor_bringup/` section 8 for the full "measured facts vs.
hypotheses" treatment).

## Confirmed root cause (belt preload) — CLOSED

The *original* motor startup failure (complete non-response, not the
dead-stop-assist symptom above) was mechanically caused by excessive/
uneven belt loading — an oblong and/or overly tight belt. Replacing it
resolved the failure. The electrical/GPIO path, the DRV8833 driver, and
the non-blocking firmware were never the problem. See
`archive/motor_bringup/DRV8833_MOTOR_BRINGUP.md` section 20 for the
full closure.

## `MotorPowerGuard` — LED coexistence (temporary workaround)

Mutes LEDs immediately before motor engagement, restores them shortly
after — coordinates via `Controls.h`'s `isMuted()`/`setMuted()` only,
never touches the strip directly. Explicitly a **bench-development
workaround**, not a production fix (see `POWER.md`). Used by
`IDLE_SWAY`/`ExpressiveMotion`/the motor+LED diagnostics.
`DanceEngine`/`MusicMotorController` deliberately do **not** use it —
see `POWER.md`'s note on why.

An experimental `DIM_DURING_MOTION` mode (vs. the default `FULL_MUTE`)
keeps the base LED effect running, ramped to a low test brightness,
during motor engagement — used only by one dedicated bench diagnostic,
not by any normal-use path.

## `MusicMotorController` — production music-reactive dancing

Currently at **Revision 10.1**. Sunny's sole production music-driven
dancing engine (`DanceEngine` is superseded — see below). Reuses
`AudioAnalyzer`'s existing `AudioFeatures.normalized` (no second
capture pipeline, no FFT, no BPM tracking).

```
AudioAnalyzer -> MusicMotorController -> MotorDriver / PWM primitives
```

**Core structure** (do not flatten these back into one energy value):
- Triple independently-smoothed EMA — `fastEnergy` (individual beats),
  `songEnergy` (sustained section intensity), `baselineEnergy` (slow
  adaptive "recent normal level"). `transientDelta = fastEnergy -
  baselineEnergy` drives beat/strong-hit detection.
- Intensity bands (`QUIET`/`LOW`/`MEDIUM`/`HIGH`/`PEAK`) from
  `songEnergy`, hysteresis-gated, mapped to an M80-M100 PWM range with
  continuous interpolation inside each band.
- Deterministic beat-action selection (never `random()`) — ordinary
  beats accent; strong hits pick among accent/reverse/hip-shake/
  extended-spin via a per-band modular counter.
- `EXTENDED_SPIN` is open-loop and time-based — **no encoder/position
  sensor anywhere in this project.** Never document an exact rotation
  angle.
- Reversal safety: one shared gate (`checkReversalGate()`/
  `tryRequestReversal()`) — minimum direction hold, reversal cooldown,
  post-spin hold, before any reversal is granted. Every accepted
  reversal ramps to 0, forces both GPIO8/GPIO9 LOW, coasts, then drives
  the new direction. **No instantaneous polarity reversal anywhere.**
- `SUSTAINED_DRIVE` (Revision 7/8) — a renewable, music-driven
  committed FORWARD/REVERSE performance phrase (5-10s initial, may
  extend well past 30-60s) layered alongside, never replacing, the
  hip-shake/reversal/spin/accent machinery.
- Relative/song-adaptive drop detection (Revision 9) — confidence
  scoring + phase machine (BUILDUP/DROP_ARMED/DROP_IMPACT/DROP_ACTIVE/
  DROP_RELEASE), additive alongside Revision 7/8's absolute-band
  sustained-drive machinery.
- Revision 10 — speed-authority cap ("bounded lending"), a
  `MotionTier` choreography-role palette, `QUIET_BUILDUP`/`MELLOW`
  duty-cycle pulses, and a drop-phrase vocabulary
  (`FULL_SUSTAIN`/`SUSTAINED_REVERSAL`/`DROP_BOOTY_SHAKE`/
  `DROP_PUNCH_AND_HOLD`/`DOUBLE_PUNCH`/`SUSTAIN_WITH_ACCENTS`).
- Revision 10.1 — fixed a genuine `SUSTAINED_DRIVE` deadlock (stuck at
  M0 while logging an unapplied step; root cause: a stale array value
  used instead of live `currentDirection`), added
  `checkSustainedDriveInvariant()` as defense-in-depth, physically
  validated.
- `MUSICAL_RAMP_DOWN` — most genuine musical endings wind the motor
  down gradually rather than stopping instantly; never affects real
  safety stops (`k`/disable/hardware faults), which always call
  `hardStop()` directly.

**M80 (80% PWM duty) is the physically-validated minimum reliable
movement command** — below it, movement is not dependable. All active
PWM behaviors clamp to this floor; only commanded coast/stop (M0) and
genuine deceleration may go below it.

**Safety invariants preserved across the whole motor stack** (verified
by inspection at every revision):
- No `delay()` anywhere in any behavior/controller module.
- No instantaneous polarity reversal — every module inserts an
  explicit stop/coast segment between forward and reverse.
- A generic ~2000ms max-energized-runtime safety net
  (`MOTION_MAX_ENERGIZED_MS` and equivalents) backstops every behavior,
  as a defensive ceiling, not the primary timing mechanism.
- `k` is a full emergency stop from any state, checked first,
  unconditionally, even mid-word, in the central serial dispatcher.

## `DanceEngine` — superseded, gated off by default

`ENABLE_LEGACY_DANCE_ENGINE=0` in `DanceEngine.h` — retained only for
historical reference/rollback safety, not called from `main.cpp` at
all while disabled. `MusicMotorController` Revision 10.1 is the sole
production dancing engine. Full removal is pending a physical
validation checklist (see `CURRENT_STATUS.md`'s "DanceEngine removal
checklist").

## `ExpressiveMotion` / `BehaviorEngine` — idle/personality movement

See `EXPRESSIVE_MOTION.md` for the full detail. Software-validated
only — physical tuning checklists exist and have **not** been run
against the real mechanism (see `KNOWN_LIMITATIONS.md`).

## Current test commands

`f`/`k` (raw forward/emergency stop), `mf`/`mr`/`m1`-`m100`/`mramp`/
`mcycle`/`mstop`/`mkick`/`mstatus` (`MotorPwmCalibration`), `musicmotor
on/off/status/...` (see `MusicMotorController.h` for the full
30+-command tuning surface), `motion`/`behavior` word commands (see
`EXPRESSIVE_MOTION.md`), `2`/`3`/`5`/`6` (bench diagnostics: priority
test, breakaway test, motor+LED coexistence, LED index map).

## Known tuning surfaces / limitations

- No current-sensing or thermal-monitoring hardware — over-current,
  stall, and thermal conditions cannot be detected in software.
- Motor + LED coexistence still relies on `MotorPowerGuard`'s
  mute-workaround for the modules that use it at all; `DanceEngine`/
  `MusicMotorController` use no mitigation (deliberate design decision,
  not yet physically stress-tested for extended sessions).
- Wider physical A/B tuning of the Revision 10 drop-phrase vocabulary
  against more songs/genres beyond the single `EDM_DUBSTEP` profile
  tested so far remains open.
