# Current Status — sunflower-esp32-s3

Fast starting point for a new session. Concise by design — **not** a
history dump. For detail, follow the links below. If you're reading
this long after its last edit, verify anything load-bearing against
`git log`/`git status` first.

**Last updated:** 2026-08-07 — Sunny V1 baseline captured and frozen
(`docs/V1/`). This is the current state of the project; `docs/current/`
mirrors it and will begin tracking Sunny V1.1 once that phase starts
(see `ROADMAP.md`). `docs/architecture/`, `docs/standards/`,
`docs/lessons/`, `docs/playbooks/`, `archive/` also created this pass.
No firmware behavior changed.

## What Sunny is

An ESP32-S3 animatronic sunflower: WS2812 LEDs with dynamic effects +
audio-reactive overlays, an INMP441 microphone, a MAX98357A speaker, a
DRV8833-driven motor with music-reactive and idle/personality movement,
and four buttons — all on one firmware image. Full detail:
[`docs/current/OVERVIEW.md`](docs/current/OVERVIEW.md).

## Current board & peripherals

ESP32-S3-WROOM (N16R8, 16MB flash/8MB PSRAM) · 58x WS2812 LEDs ·
4x buttons · 1x INMP441 mic · 1x MAX98357A + 40mm/4Ω/3W speaker ·
1x DRV8833 + brushed DC motor. GPIOs:
[`docs/current/GPIO_MAP.md`](docs/current/GPIO_MAP.md).

## Physically verified

Four-button control · WS2812 base effects + audio-reactive overlay ·
bidirectional DRV8833 motor control · `MusicMotorController`'s M80
floor and Revision 10.1 fixes · shared full-duplex I2S bus (mic capture
+ speaker write path both work) · speaker audible with an identified
usable range (35-100% digital amplitude, 50-100% clear tones). Detail:
[`docs/current/TESTING.md`](docs/current/TESTING.md).

## Working in firmware (software-validated)

`ExpressiveMotion`/`BehaviorEngine` idle/personality movement ·
unified Audio Mode (Button4 long-hold: LED overlay + motor together) ·
full speaker diagnostic/bench suite (tones, sweep, melody, chord,
noise, automatic volume ladder, 32-bit-slot format A/B diagnostic).

## Partially working / open

- **Speaker**: usable range identified; **residual buzz/static not
  resolved**, root cause not proven. See
  [`docs/current/SPEAKER.md`](docs/current/SPEAKER.md).
- **`ExpressiveMotion`/`BehaviorEngine`**: physical tuning checklists
  not yet run against the real mechanism.
- **Unified Audio Mode**: long-hold *disable* path and the
  motor-ownership-rejection cue not yet physically confirmed (enable
  path is).
- **Power**: motor/LEDs/amplifier share one 5V rail; combined peak
  load under sustained use not physically measured. See
  [`docs/current/POWER.md`](docs/current/POWER.md).

These are the two open decision gates that must pass before Sunny V1.1
begins — see `ROADMAP.md`.

## Current speaker status

Active, ongoing debugging — not paused, not archived. See
[`docs/current/SPEAKER.md`](docs/current/SPEAKER.md) for the full
current-vs-historical breakdown and exact physical observations.

## Current motor status

**Fully integrated, not future work.** DRV8833 + `MotorDriver` (sole
GPIO8/9 owner), `MusicMotorController` (production, Revision 10.1),
`ExpressiveMotion`/`BehaviorEngine` (idle/personality). See
[`docs/current/MOTOR.md`](docs/current/MOTOR.md).

## Current LED/audio status

8 base effects + `AUTO_SHOWCASE`, 8 audio overlays, both physically
verified at the original `v1.0.0` baseline and extended since. See
[`docs/current/LED_ENGINE.md`](docs/current/LED_ENGINE.md) /
[`AUDIO_OVERLAYS.md`](docs/current/AUDIO_OVERLAYS.md). To extend
either, see [`docs/development/`](docs/development/).

## What comes next

See [`ROADMAP.md`](ROADMAP.md) for the full milestone/decision-gate
plan (V1.1 refinement → gates → V1.2 Raspberry Pi → V1.3 camera →
V1.4 LLM/voice → V2.0 production). Immediate priorities: continue
speaker buzz diagnosis and measure combined-load power behavior —
these are the two open gates that must pass before Sunny V1.1 begins.

## Canonical docs

| Need | Read |
|---|---|
| Living reference (always current) | [`docs/current/`](docs/current/) |
| Frozen Sunny V1 baseline snapshot | [`docs/V1/`](docs/V1/) |
| How the system is built and why | [`docs/architecture/`](docs/architecture/) |
| Prescriptive engineering rules | [`docs/standards/`](docs/standards/) |
| Add/modify an LED effect or overlay | [`docs/development/`](docs/development/) |
| Minimal context per task type | [`docs/CLAUDE_CONTEXT_GUIDE.md`](docs/CLAUDE_CONTEXT_GUIDE.md) |
| Generalized lessons from bring-up work | [`docs/lessons/`](docs/lessons/) |
| Reusable bring-up SOPs | [`docs/playbooks/`](docs/playbooks/) |
| Historical logs (speaker/motor bring-up, old status reports) | [`archive/`](archive/) |
| Repository-wide rules | [`/AGENTS.md`](../../AGENTS.md), [`AGENTS.md`](AGENTS.md) |
