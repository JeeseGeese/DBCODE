# Sunny — Overview (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

Canonical living reference for the Sunny/sunflower-esp32-s3 animatronic
sunflower. **Sunny V1.1 is complete** (physically validated
2026-08-08 — see `docs/current/V1_1_STATUS.md`), building forward from
the **Sunny V1** baseline (captured 2026-08-07, frozen in `docs/V1/`,
tagged `sunny-v1-baseline`). Active development now begins **Sunny
V1.2 — touchscreen/UI** (see `ROADMAP.md`). See `CURRENT_STATUS.md` for
the fastest current-state summary; this file and its siblings in
`docs/current/` are the fuller living reference underneath it.

## What Sunny is

An ESP32-S3-based animatronic sunflower: WS2812 LED "petals" driven by
dynamic effects and audio-reactive overlays, an INMP441 microphone for
audio analysis, a MAX98357A amplifier + speaker for sound output, four
buttons for direct control, and a DRV8833-driven brushed DC motor for
expressive movement — including music-reactive dancing
(`MusicMotorController`) and idle/personality-driven motion
(`ExpressiveMotion` / `BehaviorEngine`).

## Current scope (what's implemented today)

- ESP32-S3-WROOM (N16R8) platform, single firmware image, Arduino
  framework via PlatformIO.
- 36x WS2812-compatible LEDs (corrected 2026-08-08, physically
  confirmed; was 58 — see `docs/lessons/verify-physical-led-count.md`),
  8 base effects + automatic showcase rotation, 8 audio-reactive
  overlays layered independently on top.
- INMP441 I2S microphone, feature extraction (RMS/envelope/bass proxy/
  transient/clap detection).
- MAX98357A I2S amplifier + a 40mm/4Ω/3W speaker, sharing one
  full-duplex I2S bus with the microphone. Speaker debugging is **still
  active/ongoing** — see `docs/current/SPEAKER.md`.
- DRV8833 H-bridge + brushed DC motor, fully integrated: direct
  digitalWrite control, PWM speed control, `MusicMotorController`
  (music-reactive dancing, Revision 10.1), `ExpressiveMotion` (idle/
  audio-reactive gentle movement), `BehaviorEngine` (personality-state
  coordinator on top of `ExpressiveMotion`).
- Four physical buttons (Mode, Mute, Brightness, Button4/Audio).
- Centralized, single-owner serial command dispatcher with a reliable
  `k` emergency stop reachable from every subsystem.
- 20 host-side (`test_host/`) regression tests covering
  `MusicMotorController` decision logic, the unified Audio Mode/Button4
  integration, and the speaker bring-up/bench/format/multitone/
  volume-ladder test suites.

## What is NOT yet implemented

- Raspberry Pi integration (planned next major phase — see
  `ROADMAP.md`).
- Camera integration (planned to follow Raspberry Pi integration, not
  before it).
- LLM/voice/personality speech output (design-only, documented in
  archived planning material — see `docs/current/SOFTWARE_ARCHITECTURE.md`).
- A dedicated external motor power supply (motor currently shares the
  5V rail with LEDs and the amplifier — see `docs/current/POWER.md`).
- Production/soldered wiring or a PCB (current build is breadboard/
  bench wiring).
- A fully resolved speaker buzz/noise root cause (see
  `docs/current/SPEAKER.md` — usable range identified, root cause of residual
  buzz not conclusively proven).

## Document map (docs/current/)

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
| `TESTING.md` | Host tests, physical test procedure, V1.1 closure validation checklist |
| `KNOWN_LIMITATIONS.md` | What's software-validated only, what's unresolved |
| `V1_1_STATUS.md` | Sunny V1.1 closure status, exit criteria, accomplishments, explicitly-not-claimed items |

For point-in-time material (what changed to reach V1, a git-history
timeline, the versioning strategy), see `docs/V1/RELEASE_NOTES.md`/
`CHANGELOG.md`/`VERSIONING.md` — those concepts are inherently
historical and live only in the frozen snapshot, not here. For durable
"how the system is built and why" material (independent of which
version is currently active), see `docs/architecture/`. For
prescriptive "how future work should be done" rules, see
`docs/standards/`.

These `docs/current/` files describe **what is true right now**. They
are reference documents, not source code — the actual behavior is
always defined by `src/`/`include/`. If this file and the code ever
disagree, the code wins; update this doc, not the other way around.
