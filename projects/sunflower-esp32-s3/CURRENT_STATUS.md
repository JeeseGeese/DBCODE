# Current Status — sunflower-esp32-s3

Fast starting point for a new session. Concise by design — **not** a
history dump. For detail, follow the links below. If you're reading
this long after its last edit, verify anything load-bearing against
`git log`/`git status` first.

**Last updated:** 2026-08-16 — **Sunny V1.1: COMPLETE** (physically
validated on battery power, checkpointed, tagged `sunny-v1.1`, pushed —
see `docs/current/V1_1_STATUS.md`). **Sunny V1.2 (touchscreen/UI) is
ACTIVE** in a separate project — V1.2.1 and V1.2.2 both complete and
physically validated (V1.2.2's corner-mapping bug fix, a fitted linear
model with <2px error on the measured dataset, has now been physically
retested and confirmed accurate). **Sunny V1.2 Beta 1** (`sunny-v1.2-beta1`)
checkpoints this state — see "What comes next" below.
`docs/V1/` stays frozen exactly as tagged at `sunny-v1-baseline`
(2026-08-07) and remains untouched.

Previous entries (2026-08-08, earlier the same day): power/brownout
investigation documented in full (`docs/current/POWER.md`) — 36-LED
correction, HWTEST power-limiter fix (physically verified), computer-
USB-vs-battery brownout finding (leading hypothesis, not formally
closed); speaker/audio refinement sprint (volume ladder, new
diagnostics, APLL closed).

Earlier entry (2026-08-07): Sunny V1 baseline captured, frozen
(`docs/V1/`), and tagged (`sunny-v1-baseline`). `docs/architecture/`,
`docs/standards/`, `docs/lessons/`, `docs/playbooks/`, `archive/` were
also created for the V1 baseline.

## What Sunny is

An ESP32-S3 animatronic sunflower: WS2812 LEDs with dynamic effects +
audio-reactive overlays, an INMP441 microphone, a MAX98357A speaker, a
DRV8833-driven motor with music-reactive and idle/personality movement,
and four buttons — all on one firmware image. Full detail:
[`docs/current/OVERVIEW.md`](docs/current/OVERVIEW.md).

## Current board & peripherals

ESP32-S3-WROOM (N16R8, 16MB flash/8MB PSRAM) · 36x WS2812 LEDs
(corrected 2026-08-08; physically confirmed count, was 58) ·
4x buttons · 1x INMP441 mic · 1x MAX98357A + 40mm/4Ω/3W speaker ·
1x DRV8833 + brushed DC motor. GPIOs:
[`docs/current/GPIO_MAP.md`](docs/current/GPIO_MAP.md).

## Physically verified

Four-button control · WS2812 base effects + audio-reactive overlay ·
bidirectional DRV8833 motor control · `MusicMotorController`'s M80
floor and Revision 10.1 fixes · shared full-duplex I2S bus (mic capture
+ speaker write path both work) · speaker audible with an identified
usable range (35-100% digital amplitude, 50-100% clear tones) · 36 LEDs
+ HWTEST power limiter · combined LED+overlay+MusicMotor+mic+speaker
operation on battery power with no brownout (V1.1 closure validation,
2026-08-08). Detail: [`docs/current/TESTING.md`](docs/current/TESTING.md),
[`docs/current/V1_1_STATUS.md`](docs/current/V1_1_STATUS.md).

## Working in firmware (software-validated)

`ExpressiveMotion`/`BehaviorEngine` idle/personality movement ·
unified Audio Mode (Button4 long-hold: LED overlay + motor together) ·
full speaker diagnostic/bench suite (tones, sweep, melody, chord,
lowmidhigh, speechtest, musictest, noise, silencecheck, carriercheck,
isolate, automatic volume ladder, 32-bit-slot format A/B diagnostic,
V1.1 normal-use volume ladder 35-100%/70% default).

## Partially working / open

- **Speaker**: usable range identified, V1.1 normal-use volume ladder
  (35-100%, 70% default) and new buzz-isolation/realistic-content
  diagnostics implemented and uploaded; **residual buzz/static not
  resolved**, root cause not proven, none of this sprint's changes
  physically tested yet. APLL investigated and closed (not supported on
  this chip/framework, source-verified). See
  [`docs/current/SPEAKER.md`](docs/current/SPEAKER.md).
- **`ExpressiveMotion`/`BehaviorEngine`**: physical tuning checklists
  not yet run against the real mechanism.
- **Unified Audio Mode**: long-hold *disable* path and the
  motor-ownership-rejection cue not yet physically confirmed (enable
  path is).
- **Power**: motor/LEDs/amplifier share one 5V rail. Combined LED+motor
  brownouts were reproducible under computer-USB power; battery-pack
  power has so far eliminated them under the same load — strong evidence,
  **electrical root cause not formally closed**. See
  [`docs/current/POWER.md`](docs/current/POWER.md)'s "Current brownout
  investigation" section for the full record.

These are accepted, documented V1.1 limitations — V1.1 is complete
despite them; see `docs/current/V1_1_STATUS.md`'s exit-criteria table
and "Explicitly NOT claimed" section for exactly what remains open for
future hardware refinement/productization work.

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
plan (V1.1 **complete** → **V1.2 touchscreen/UI (active)** → V1.3
Raspberry Pi → V1.4 camera → V1.5 LLM/voice → V2.0 production).

**V1.2 is now active** in a separate PlatformIO project,
[`../sunny-display-esp32/`](../sunny-display-esp32/) — a second,
independent ESP32 controller (ELEGOO ESP32-WROOM-32E touch display, not
this project's ESP32-S3). See that project's
[`docs/DISPLAY_HARDWARE.md`](../sunny-display-esp32/docs/DISPLAY_HARDWARE.md)
for the two-controller architecture, hardware identity, and bring-up
status. **V1.2.1 (display/touch standalone bring-up) is COMPLETE**
(physically validated 2026-08-08). **V1.2.2 (touch calibration) is now
COMPLETE** — a real mapping bug (2026-08-09) treated an inset
calibration target's raw reading as if measured at the true screen
edge, producing ~30-42px corner error (masked at first by a
coincidentally-accurate center and an enlarged hitbox that made corners
*activate* without actually being *accurate*). Replaced with a per-axis
linear fit against the measured targets' true screen positions (<2px
error on the dataset), and the spatial accuracy of the fix has since
been physically retested and confirmed. This project (`sunflower-esp32-s3`,
the Sunny body controller) remains untouched by V1.2 work and stays at
its validated `sunny-v1.1` state. See
[`../sunny-display-esp32/docs/V1_2_BETA1_STATUS.md`](../sunny-display-esp32/docs/V1_2_BETA1_STATUS.md)
for the full **Sunny V1.2 Beta 1** checkpoint record.

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
