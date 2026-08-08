# Sunny — Software Architecture (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

## Module map

```
main.cpp            setup()/loop() wiring, frame composition, serial dispatch, power limit
Config.h             every tunable constant, grouped by feature, with rationale comments
Controls.cpp/.h      buttons, serial command parsing, selection state (effect/overlay/brightness/mute)

LED / audio-visual:
  LedEffects.cpp/.h        8 base effects + shared render helpers
  AudioAnalyzer.cpp/.h     I2S capture, AudioFeatures
  AudioVisualState.cpp/.h  richer per-frame control signals
  AudioPalette.cpp/.h      audio-reactive color helpers
  AudioOverlays.cpp/.h     8 overlays
  AutoShowcase.cpp/.h      automatic base-effect rotation
  VisualCue.cpp/.h         overlay on/off confirmation flash

Audio I/O:
  SharedI2S.cpp/.h    sole I2S_NUM_0 owner (full-duplex, mic RX + speaker TX)
  SpeakerTest.cpp/.h  speaker bring-up/diagnostic test suite (still active)

Motor:
  MotorDriver.cpp/.h          sole GPIO8/9 owner
  MotorPowerGuard.cpp/.h      LED-mute coordination around motor engagement
  MotorPriorityMode.cpp/.h    boot-equivalent quiet-system diagnostic
  MotorPwmCalibration.cpp/.h  manual PWM bench tool
  MotorBehavior.cpp/.h        simple IDLE_SWAY behavior
  DanceEngine.cpp/.h          superseded, gated off (ENABLE_LEGACY_DANCE_ENGINE=0)
  MusicMotorController.cpp/.h production music-reactive dancing (Rev 10.1)
  ExpressiveMotion.cpp/.h     idle/audio-reactive gentle movement
  BehaviorEngine.cpp/.h       personality-state coordinator (above ExpressiveMotion)

Bring-up/diagnostic (temporary, self-labeled, see their own header comments for removal instructions):
  HardwareTest.cpp/.h  boot-time LED+mic verification sequence
  MicRetest.cpp/.h     dedicated from-scratch INMP441 diagnostic
```

## Single-owner resource convention

The strongest architectural convention in this codebase: **every
hardware resource has exactly one owning module**, and every
higher-level behavior reaches that resource only through the owner's
public API — never directly.

| Resource | Sole owner |
|---|---|
| GPIO8/GPIO9 (motor) | `MotorDriver.cpp` |
| `I2S_NUM_0` driver install/config | `SharedI2S.cpp` |
| `Serial.read()`/`available()` | `main.cpp`'s `pollSerialDispatcher()` |
| The single `Adafruit_NeoPixel strip` / `strip.show()` | `main.cpp` |
| LED mute save/restore around motor engagement | `MotorPowerGuard.cpp` |
| Audio feature extraction | `AudioAnalyzer.cpp` |

**Never create a second owner of any resource in this table.**

## Serial dispatch (single-reader discipline)

`main.cpp`'s `pollSerialDispatcher()` is the **only** `Serial.read()`/
`available()` call site in the program. This was not always true — an
earlier version had two independent readers (the motor/LED interceptor
and `Controls.cpp`'s own loop), which raced and intermittently dropped
`k` (~50% miss rate under specific timing — see
`docs/lessons/serial-dispatch-single-owner.md`). The fix:
`Controls.cpp` no longer reads `Serial` itself; it's fed one byte at a
time via `feedSerialByte()`. `k` is checked first, unconditionally,
even mid-word, and is re-serviced defensively at several points per
frame.

## Render loop order (exact, from `main.cpp`)

See `LED_ENGINE.md`'s "Render pipeline" section for the full sequence.
Key invariant: **exactly one `strip.show()` call per composed frame.**

## Coding conventions

- `enum class` for every mode/state/phase type.
- Tunable constants live in `Config.h`, grouped by feature, each with a
  comment on what raising/lowering it does.
- Shared timing values are tiers referenced by name, not repeated
  literals.
- Percent-based motor speed (the "M" scale) converts to raw PWM duty in
  exactly one place (`percentToMotorPwm()` in `MusicMotorController.cpp`).
- Deterministic decision logic over `random()` wherever a decision
  needs to be reproducible for tuning (e.g. `selectBeatAction()`'s
  modular per-band counters).
- Reserved single-character serial commands must never contain `k`
  (checked unconditionally, even mid-word).
- No `delay()` in any behavior/controller module — everything is
  `millis()`-based and non-blocking.

## Future Raspberry Pi integration surface

Not implemented. The intended integration points (documented design
only, see `archive/superseded_docs/` for the full detail):
`BehaviorEngine`'s public API for movement/personality state, a future
`beginAudioOutput()`-shaped API sitting above `SharedI2S` for
audio-output handoff, and a yet-to-be-determined transport (USB serial,
Wi-Fi socket, Bluetooth, local HTTP/WebSocket). Real-time motor safety
(`k`, the max-energized safeguard, `MotorPowerGuard`) is a deliberate,
already-stated requirement to remain entirely on the ESP32 regardless
of transport — the higher-level computer must never be a dependency
for `k` to work. See `ROADMAP.md`.
