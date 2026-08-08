# LED / Audio-Overlay Quick Reference

Compact working context for LED effect and audio overlay work. This
file plus the source files it names is normally **all** you need — see
"What NOT to read" at the bottom.

## Source files

```
include/LedEffects.h / src/LedEffects.cpp        8 base effects
include/AudioOverlays.h / src/AudioOverlays.cpp  8 overlays
include/AudioVisualState.h / src/AudioVisualState.cpp  overlay input signals
include/AudioPalette.h / src/AudioPalette.cpp    shared audio-reactive color helpers
include/AudioAnalyzer.h                           AudioFeatures struct (read-only reference)
include/VisualCue.h / src/VisualCue.cpp          overlay on/off flash
include/AutoShowcase.h / src/AutoShowcase.cpp    automatic base-effect rotation
include/Config.h                                  all tunable constants
```

For the actionable "how do I add one" steps, see
`ADDING_LED_EFFECTS.md` / `ADDING_AUDIO_OVERLAYS.md` in this directory.

## BaseEffect list (`LedEffects.h`)

`PETAL_BREATHE, COLOR_WAVE, SUNSET_SPIN, RAINBOW_FLOW, SPARKLE_BLOOM,
FIREFLY_GARDEN, AURORA, SOLAR_FLARE, AUTO_SHOWCASE` (last, not a real
effect — new effects go above it).

## AudioOverlay list (`AudioOverlays.h`)

`OFF, PULSE, RIPPLE, SPARK, LIGHTNING, BASS_BLOOM, SPECTRUM_WAVE,
COLOR_FLOOD, COMET_BURST`.

## AudioFeatures (from AudioAnalyzer.h — read-only, do not modify)

`rms, normalized, envelope, transientStrength, lowFrequencyEnergy,
clap, transient, peak`. Overlays don't read this directly — they read
the derived `AudioVisualState` instead (see `AudioVisualState.h`:
`level/envelope/bass/transient/transientStrength/clap/lowRange/
midRange/highRange/energy8/bass8/transient8`).

## Render pipeline (exact order, `main.cpp`)

```
updateAudioVisualState() -> renderVisualCue() [if active, owns the frame]
  -> else: renderBaseEffect() or renderAutoShowcase()
  -> if overlay enabled: applyAudioOverlay()
  -> scale by brightness -> applyPowerLimit() -> ONE strip.show()
```

## Timing assumptions

`FRAME_INTERVAL_MS = 20` (~50fps target, `Config.h`). No `delay()`
anywhere in this subsystem. Every render function must return in well
under a frame.

## Key Config.h constants

Per-effect timing (e.g. `PETAL_BREATHE_PERIOD_MS`,
`SPARKLE_BLOOM_SPAWN_CHANCE_PER_FRAME`, `FIREFLY_COUNT`), per-overlay
gains/pools (e.g. `PULSE_MIN_WIDTH_LEDS`, `RIPPLE_MAX_COUNT`,
`LIGHTNING_MAX_BOLTS`), and audio tuning (`AUDIO_NOISE_FLOOR`,
`AUDIO_MAX_RMS`, `AUDIO_CLAP_THRESHOLD`, etc. — see
`docs/current/AUDIO_ANALYSIS.md`'s tuning guide). `NUM_LEDS=58`, `LED_PIN=4`.

## Brightness handling

9-level table (`BRIGHTNESS_PERCENTS`/`BRIGHTNESS_RAW`, `Config.h`),
cycled by the Brightness button (GPIO17) or serial `+`/`-`. Applied
**once**, centrally, in `main.cpp` — never inside an effect/overlay.

## Power limiting

`main.cpp`'s `applyPowerLimit()` — software current estimate
(`LED_CURRENT_LIMIT_MA` default 1000mA), scales the whole frame down
if exceeded, throttled `[POWER]` warning at most every 2s. See
`docs/current/POWER.md`.

## Button behavior relevant to effects

| Button | GPIO | Effect |
|---|---|---|
| Mode | 10 | next base effect + next overlay mode (single press); previous base effect only (double press) |
| Mute | 11 | LED output off/on |
| Brightness | 17 | cycle 9 levels |
| Button4 | 5 | short: overlay ON/OFF toggle; long hold: unified Audio Mode (LED + motor) |

## Audio-overlay enable/disable visual cues

Green flash (one) = overlay enabled. Double red flash = overlay
disabled. Fixed ~45% brightness cap, suppressed while muted. See
`docs/current/LED_ENGINE.md`.

## Build / upload / monitor

```bash
cd ~/DOBETTERCODE/DBCODE/projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run
pio device list                                     # confirm port first
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor -p /dev/ttyACM0 -b 115200
```

## Useful serial test commands

`n`/`p` (base effect next/prev), `o` (advance overlay mode), `x`
(overlay on/off), `+`/`-` (brightness), `m` (mute), `effects`/
`overlays` (list all by name), `status`/`v` (full state / audio-visual
signals), `g`/`r` (force the enable/disable cue, diagnostic only).

## Tests to run

`test_host/` has **no** LED/overlay coverage yet (see
`docs/current/KNOWN_LIMITATIONS.md`) — nothing to run there for this
subsystem specifically, but still run the full suite
(`docs/current/TESTING.md`) before considering any firmware change done, and
consider adding a host test for new host-testable logic. Follow
`README.md`'s "Physical test procedure" for a hardware check.

## Docs to update after a change

`README.md`'s effect/overlay lists, this file's lists above, and
(if genuinely part of a new baseline, not routine work)
`docs/current/LED_ENGINE.md`/`AUDIO_OVERLAYS.md`.

---

## What NOT to read for ordinary LED/AudioOverlay work

Speaker historical logs (`archive/speaker_bringup/`,
`docs/current/SPEAKER.md`), motor debug history
(`archive/motor_bringup/`, `docs/current/MOTOR.md`), Raspberry Pi/camera
docs, archived bring-up reports, old temporary hardware test logs
(`HardwareTest.cpp`/`MicRetest.cpp`). Read any of these only if the
specific requested change actually depends on them (e.g. a request to
make an overlay react to the speaker's own output would need
`docs/current/SPEAKER.md`).
