# Sunny V1 — Overview

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/OVERVIEW.md`.

Canonical snapshot of the Sunny/sunflower-esp32-s3 animatronic sunflower
at the **Sunny V1** engineering baseline (2026-08-07). This is a
**checkpoint, not a code freeze** — active firmware continues to evolve
from this point as V1.1, V1.2, etc. See `CURRENT_STATUS.md` for what's
currently active.

## What Sunny is

An ESP32-S3-based animatronic sunflower: WS2812 LED "petals" driven by
dynamic effects and audio-reactive overlays, an INMP441 microphone for
audio analysis, a MAX98357A amplifier + speaker for sound output, four
buttons for direct control, and a DRV8833-driven brushed DC motor for
expressive movement — including music-reactive dancing
(`MusicMotorController`) and idle/personality-driven motion
(`ExpressiveMotion` / `BehaviorEngine`).

## V1 scope (what's included in this baseline)

- ESP32-S3-WROOM (N16R8) platform, single firmware image, Arduino
  framework via PlatformIO.
- 58x WS2812-compatible LEDs, 8 base effects + automatic showcase
  rotation, 8 audio-reactive overlays layered independently on top.
- INMP441 I2S microphone, feature extraction (RMS/envelope/bass proxy/
  transient/clap detection).
- MAX98357A I2S amplifier + a 40mm/4Ω/3W speaker, sharing one
  full-duplex I2S bus with the microphone. Speaker debugging is **still
  active/ongoing** — see `docs/V1/SPEAKER.md`.
- DRV8833 H-bridge + brushed DC motor, fully integrated: direct
  digitalWrite control, PWM speed control, `MusicMotorController`
  (music-reactive dancing, Revision 10.1), `ExpressiveMotion` (idle/
  audio-reactive gentle movement), `BehaviorEngine` (personality-state
  coordinator on top of `ExpressiveMotion`).
- Four physical buttons (Mode, Mute, Brightness, Button4/Audio).
- Centralized, single-owner serial command dispatcher with a reliable
  `k` emergency stop reachable from every subsystem.
- 18 host-side (`test_host/`) regression tests covering
  `MusicMotorController` decision logic, the unified Audio Mode/Button4
  integration, and the speaker bring-up/bench/format/multitone/
  volume-ladder test suites.

## What is NOT yet in V1

- Raspberry Pi integration (planned next major phase — see
  `ROADMAP.md`).
- Camera integration (planned to follow Raspberry Pi integration, not
  before it).
- LLM/voice/personality speech output (design-only, documented in
  archived planning material — see `docs/V1/SOFTWARE_ARCHITECTURE.md`).
- A dedicated external motor power supply (motor currently shares the
  5V rail with LEDs and the amplifier — see `docs/V1/POWER.md`).
- Production/soldered wiring or a PCB (current build is breadboard/
  bench wiring).
- A fully resolved speaker buzz/noise root cause (see
  `docs/V1/SPEAKER.md` — usable range identified, root cause of residual
  buzz not conclusively proven).

## Document map (docs/V1/)

| File | Covers |
|---|---|
| `OVERVIEW.md` | This file |
| `HARDWARE_ARCHITECTURE.md` | Full physical system: MCU, peripherals, how they connect |
| `GPIO_MAP.md` | Every GPIO assignment, single source of truth |
| `ELECTRICAL.md` | Wiring specifics, decoupling, verified vs. recommended |
| `POWER.md` | Power rails, current draw, known contention/risk |
| `BUTTONS.md` | Physical button behavior |
| `MICROPHONE.md` | INMP441 wiring, format, verified behavior |
| `SPEAKER.md` | Current MAX98357A status — separate from historical bring-up logs |
| `I2S_ARCHITECTURE.md` | Shared full-duplex I2S bus design |
| `LED_ENGINE.md` | BaseEffect rendering architecture |
| `AUDIO_OVERLAYS.md` | AudioOverlay rendering/blending architecture |
| `AUDIO_ANALYSIS.md` | AudioAnalyzer/AudioFeatures/AudioVisualState pipeline |
| `MOTOR.md` | DRV8833 + MotorDriver + PWM + MusicMotorController |
| `EXPRESSIVE_MOTION.md` | ExpressiveMotion + BehaviorEngine |
| `SOFTWARE_ARCHITECTURE.md` | Module map, ownership rules, render/loop structure |
| `TESTING.md` | Host tests, physical test procedure |
| `KNOWN_LIMITATIONS.md` | What's software-validated only, what's unresolved |
| `RELEASE_NOTES.md` | What changed to reach this baseline |
| `CHANGELOG.md` | Git-history-derived timeline of major milestones |
| `VERSIONING.md` | Recommended git-tag versioning strategy (not yet executed) |

These describe **what was known/verified at this checkpoint**. They are
reference documents, not source code — the actual behavior is always
defined by `src/`/`include/`. If a V1 doc and the code ever disagree
after V1.1+ changes, the code wins; update the doc.
