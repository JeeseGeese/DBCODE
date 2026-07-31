# Current Status — sunflower-esp32-s3

A concise snapshot of *current* project state. This file is meant to be
updated often — more often than `README.md`'s narrative documentation —
and to go stale quickly on purpose, because it only claims to describe
"now." If you're reading this more than a few sessions after its last
edit, verify anything load-bearing against `git log`, `git status`, and
`README.md` rather than trusting it blindly.

Last updated: 2026-07-30 (checkpoint commit + tag pass, same day as initial
creation).

## Project status

**Not complete. Active development checkpoint.** This is a hobby
animatronic-sunflower project under continuous, incremental development —
not a finished product, not a shipped release, and not paused. Treat
every "validated" claim below as scoped exactly to what it says, not as a
signal the project is close to done.

## Current milestone

**Expressive Motion / MusicMotor Revision 10.1**, on branch
`feature/expressive-motion-v1` (branched from tag `v1.0.0`).

Within this milestone, `MusicMotorController` (music-reactive motor
choreography) is the actively-developed system, currently at its own
internal Revision 10.1 — see `README.md`'s "MusicMotorController" section
and `projects/sunflower-esp32-s3/AGENTS.md` section 6 for the architecture.
`ExpressiveMotion` (idle/audio-reactive gentle movement) and
`BehaviorEngine` (personality-state coordination layered on top of
`ExpressiveMotion`) are both part of this same milestone and are
software-validated but not yet physically validated — see "Known
limitations" below.

## Recently completed work

(Most recent first. As of the 2026-07-30 engineering checkpoint, all of
this is committed at `64e8aee` — see "Current git milestone" below.)

- MusicMotorController Revision 10.1: fixed a genuine `SUSTAINED_DRIVE`
  deadlock (stuck at M0 while logging an unapplied step) and a stale
  `sustainedDriveLowEnergySinceMs` diagnostic bug; added
  `checkSustainedDriveInvariant()` defense-in-depth and two precise
  stop-reason labels (`deceleration_handoff`/`direction_change_handoff`).
  Physically validated per the README's Revision 10.1 write-up. Covered
  by `test_host/music_motor_sustained_drive_deadlock.cpp`.
- MusicMotorController Revision 10: speed-authority cap ("bounded
  lending"), `MotionTier` pulse/rest duty-cycle for slower-feeling
  motion, faster/more decisive drop entry, and a drop-phrase vocabulary
  (`FULL_SUSTAIN`/`SUSTAINED_REVERSAL`/`DROP_BOOTY_SHAKE`/
  `DROP_PUNCH_AND_HOLD`/`DOUBLE_PUNCH`/`SUSTAIN_WITH_ACCENTS`).
- MusicMotorController Revision 9: relative/song-adaptive EDM/dubstep
  drop detection (confidence scoring + phase machine), physically
  validated in an initial drop test that itself surfaced the Revision 10
  issues above.
- MusicMotorController Revisions 3-8: physical PWM calibration (M80
  floor confirmed), sustained drop hold, reverse hip-shake, wobble cue,
  calibrated spin durations, detailed decision diagnostics, renewable
  performance phrases, lifelike silence/low-energy handling.
- `SharedI2S` single full-duplex I2S port (replacing a two-controller
  design that failed conclusively on hardware) — enables the MAX98357A
  speaker bring-up (`SpeakerTest`) alongside the existing INMP441
  microphone on one port.
- `BehaviorEngine` personality-state coordinator (`MANUAL`/`IDLE`/
  `CURIOUS`/`LISTENING`/`THINKING`/`EXCITED`/`SLEEPING`) layered on top
  of `ExpressiveMotion`.
- `ExpressiveMotion` pattern-based idle/audio-reactive movement
  (`GENTLE_SWAY` through `DRAMATIC_SWEEP`, audio-triggered patterns).
- `DanceEngine` V1 (earlier, simpler mic-driven choreography — still
  present and user-selectable, mutually exclusive with
  `MusicMotorController` at runtime, not merged into it).
- `MotorPwmCalibration` PWM primitives and calibration tooling.
- `v1.0.0` tagged baseline: four-button control, WS2812 effects, INMP441
  audio input + overlay, bidirectional DRV8833 motor control, the
  mechanical belt fix, motor+LED coexistence (`MotorPowerGuard`), the
  centralized serial dispatcher, reliable `k` emergency stop.

## Physically validated

(Human-observed real-hardware behavior, per `/AGENTS.md` section 6 — not
build success, not host tests, not serial-log inspection alone.)

- Four-button control, WS2812 LED effects, INMP441 audio input +
  audio-reactive overlay (`v1.0.0` baseline).
- Bidirectional DRV8833 motor control (forward/reverse/stop), mechanical
  belt-preload fix, motor+LED coexistence under `MotorPowerGuard`
  `FULL_MUTE`, centralized serial dispatcher, `k` emergency stop
  reliability (10/10 trials both directions).
- `MusicMotorController`'s M80 active-movement floor and forward/reverse
  timing table (quarter/half/full-turn approximate durations at
  M80/M90/M100).
- `MusicMotorController` Revision 9's initial drop detection (one
  physical drop test) and Revision 10.1's deadlock fix.

## Host validated

(Deterministic `test_host/*.cpp` programs — pure decision logic only, no
hardware involved. See `projects/sunflower-esp32-s3/AGENTS.md` section 9
for how to run these.)

12 files as of Revision 10.1, covering: choreography dynamics,
choreography invariants, debug diagnostics, the Revision 10.1 diagnostic-
label fix, intensity-band invariants, pipeline profiles, the Revision 9
relative-drop detector, the renewable-phrase system, rotation-commitment
logic, silence rampdown, sustained-drive behavior, and the Revision 10.1
deadlock regression specifically. All 12 must pass with zero compiler
warnings before a change in the area they cover is considered done — see
each file's own header comment for its individual build/run command.

## Known limitations

- **`ExpressiveMotion`'s idle/audio-reactive movement and
  `BehaviorEngine`'s personality states are software-validated only.**
  Neither has completed its own physical validation checklist
  (`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 12,
  `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 15) — timing tiers,
  weights, and cooldowns throughout are conservative starting values, not
  yet tuned against the real mechanism.
- **The speaker (MAX98357A) has only been serial-log verified** — write
  path confirmed working end-to-end (100% write success observed), but
  actual audio quality/volume on real hardware has not been confirmed.
- **The DRV8833 still runs from the ESP32's 3.3V logic rail** — a known,
  unresolved electrical constraint, not a production-ready power
  architecture. See `projects/sunflower-esp32-s3/AGENTS.md` section 5.
- **No true FFT/spectral analysis** — the bass-impact signal is a
  single-pole low-pass RMS proxy, not real frequency-band extraction; its
  noise-floor/max-RMS constants are not hardware-calibrated the way the
  main RMS ones are.
- **No thermal or current-sensing hardware** — over-current, stall, and
  thermal conditions cannot be detected in software on this hardware,
  full stop.
- **`DanceEngine` and `MusicMotorController` both remain present and
  user-selectable**, mutually exclusive at runtime but not merged into a
  single engine — this is a deliberate current-state fact, not
  necessarily a permanent one (see `ROADMAP.md`).
- **Boot takes ~30-40 seconds before serial commands are processed**
  (`HardwareTest` + `MicRetest` both block at the end of `setup()`) — this
  is intentional bring-up tooling, not a bug, but affects every physical
  test session's timing.
- Further physical A/B tuning of the Revision 10 drop-phrase vocabulary
  and the Revision 9 relative-drop detector against a wider range of
  songs/genres is explicitly called out as open in the README (genre-
  profile architecture exists for this — only `EDM_DUBSTEP` is populated
  so far).

## Current active work

**Speaker debugging** — physical audio-quality validation of the
MAX98357A output (volume, clarity, cleanliness) beyond the current
write-path-only verification. This is the explicitly named next
development objective as of the 2026-07-30 engineering checkpoint (commit
`64e8aee`, tag `sunny-rev10.1-checkpoint`). No speaker hardware testing
has occurred yet — see "Physically validated" and "Known limitations"
below; this section will be updated once it does.

Wider physical calibration/validation of the MusicMotorController
Revision 10/10.1 drop-phrase vocabulary and speed-authority cap against a
wider range of real songs (beyond the single EDM/dubstep drop test
performed so far) also remains open and is not superseded by the speaker
work — both are legitimate active threads.

## Highest priority next objectives

1. **Speaker debugging** (see "Current active work" above) — physical
   audio-quality validation of the MAX98357A output. Read `README.md`'s
   "Speaker hardware test" startup safety sequence in full before
   connecting anything (keep MAX98357A `SD` at GND until
   `[SPEAKER] Digital silence active` prints).
2. Wider physical A/B testing of MusicMotorController Revision 10.1
   across more songs/genres (see "Known limitations" above).
3. Run the physical validation checklists for `ExpressiveMotion`
   (`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 12) and
   `BehaviorEngine` (`docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 15) —
   currently the largest gap between "software-validated" and
   "physically validated" in the project.

See `ROADMAP.md` for the fuller planning picture beyond these immediate
items.

**Resolved as of this checkpoint (kept here for continuity, not as an
open item):** the working tree previously had substantial uncommitted
content (`MotorPwmCalibration` through `MusicMotorController` Revision
10.1 and all 12 `test_host/` files). That gap is closed — see "Current
git milestone" below. `docs/AI_HANDOFF.md` has the full before/after
detail if a future session needs it.

## Open engineering questions

- Should `DanceEngine` eventually be retired in favor of
  `MusicMotorController`, or is there a reason to keep both
  user-selectable long-term? Not yet decided.
- What is the actual root cause class for cases where the motor needs a
  manual assist from a dead stop under load — was this fully explained by
  the mechanical belt-preload fix, or could a residual case remain under
  different mechanical conditions? Treated as closed per
  `docs/DRV8833_MOTOR_BRINGUP.md` section 20, but worth re-confirming
  after any future mechanical change (new belt, new load).
- What's the right transport for the future Raspberry Pi/LLM speech
  integration (`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` sections 15-17)?
  Explicitly undecided — USB serial, Wi-Fi socket, Bluetooth, local
  HTTP/WebSocket, and direct I2S are all still open candidates.
- Is a dedicated external motor power supply (separate from the LED
  strip's supply) planned for a specific future milestone, or still
  open-ended? Affects how much further bench-workaround tooling
  (`MotorPowerGuard`) is worth investing in versus fixing at the power
  layer.

## Current git milestone

- **Last physically-validated-baseline tag:** `v1.0.0`.
- **Current branch:** `feature/expressive-motion-v1`.
- **Engineering-checkpoint commit:** `64e8aee` — "feat: checkpoint Sunny
  motor and audio development through MusicMotor Rev 10.1" (2026-07-30).
  Consolidates `MotorPwmCalibration`, `DanceEngine`, `SharedI2S`,
  `SpeakerTest`, `MusicMotorController` through Revision 10.1,
  `HardwareTest`, `MicRetest`, and the full 12-file `test_host/`
  regression suite — everything previously described as "uncommitted
  working-tree content" in this file and in `docs/AI_HANDOFF.md` is now
  committed history. `git log`/`git show` can be trusted for this
  project's current firmware content again.
- **Documentation commit:** follows `64e8aee` on the same branch —
  `README.md`, `/AGENTS.md`, `/CLAUDE.md`, `/docs/AI_HANDOFF.md`, this
  file, and `ROADMAP.md`. See `git log --oneline -5` for its exact hash.
- **New checkpoint tag:** `sunny-rev10.1-checkpoint`, created locally on
  the documentation commit above. **Not** a claim of physical validation
  or of being a finished release — an active-development engineering
  checkpoint marker only, explicitly named as such in its own annotation.
  **Not pushed** — local only, per explicit instruction not to push
  without separate approval.
- No prior milestone tag existed for the expressive-motion/MusicMotor
  work before this — a future tag (e.g. covering the point where
  `ExpressiveMotion` and `BehaviorEngine` complete physical validation, or
  where speaker debugging concludes) has not been proposed or
  named yet.
