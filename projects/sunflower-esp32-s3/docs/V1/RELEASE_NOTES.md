# Sunny V1 — Release Notes

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** This is inherently point-in-time material; see `CURRENT_STATUS.md` for what's active now.

Sunny V1 is the first formal engineering baseline beyond the original
`v1.0.0` tag (four-button + LED + mic only). It consolidates everything
built on the `feature/expressive-motion-v1` branch since then: motor
integration (`MusicMotorController` through Revision 10.1), the unified
Audio Mode, expressive/personality motion, and the full MAX98357A
speaker bring-up + diagnostic suite.

## What's new since `v1.0.0`

- **Motor fully integrated**: DRV8833 bidirectional control, PWM speed
  control, `MusicMotorController` (production music-reactive dancing,
  10.1 revisions of physical calibration and choreography refinement),
  `ExpressiveMotion` (idle/audio-reactive gentle movement),
  `BehaviorEngine` (personality-state coordinator).
- **Unified Audio Mode**: Button 4 long-hold toggles LED audio overlay
  + `MusicMotorController` together as one coordinated state.
- **`DanceEngine` superseded**: gated off by default
  (`ENABLE_LEGACY_DANCE_ENGINE=0`), retained for rollback only.
- **Speaker bring-up**: shared full-duplex I2S architecture (replacing
  a two-controller design that failed on hardware), a large speaker
  diagnostic/bench-test suite (tones, sweep, melody, chord, noise, an
  automatic volume ladder, and a 32-bit-slot packing A/B diagnostic),
  and a physically-identified usable amplitude range on a new 40mm/4Ω/
  3W speaker. Residual buzz not yet resolved — see `SPEAKER.md`.
- **Power-distribution documentation correction** (2026-08-01): the
  DRV8833 and MAX98357A actually run from the shared 5V rail, not the
  ESP32's 3.3V pin as earlier docs incorrectly stated.
- **Centralized serial dispatcher**: single `Serial` reader, reliable
  `k` emergency stop (root-caused and fixed a prior two-reader race
  that intermittently dropped `k`).
- **18 host-side regression tests** (up from 0 at `v1.0.0`).

## This cleanup pass (V1 documentation baseline)

This specific pass (2026-08-07) did not change firmware behavior. It:
- Established `docs/V1/` as the canonical reference snapshot.
- Added `docs/development/` SOPs for extending LED effects and audio
  overlays.
- Added `docs/lessons/` and `docs/playbooks/` extracted from prior
  bring-up work.
- Archived historical bring-up logs (preserved, not deleted) under
  `archive/`.
- Rewrote `CURRENT_STATUS.md` to be concise.
- Fixed two stale documentation/comment mismatches found during audit
  (`MotorDriver.h`'s power comment, `README.md`'s host-test count).

## Not included in V1

See `OVERVIEW.md`'s "What is NOT yet in V1" and `ROADMAP.md`.
