# Sunny V1 — Audio Analysis & Tuning

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/AUDIO_ANALYSIS.md`.

See `MICROPHONE.md` for wiring/I2S format. This file covers the
analysis pipeline and how to tune it.

## Pipeline

```
I2S capture (SharedI2S, 16kHz)
  -> AudioAnalyzer.cpp: DC correction, RMS, adaptive noise floor,
     envelope (attack/release), transient rise-rate, clap/transient
     edge detection, low-pass bass proxy
  -> AudioFeatures (see MICROPHONE.md for every field)
  -> AudioVisualState.cpp: level/bass/transient + derived low/mid/high
     control bands (see AUDIO_OVERLAYS.md — NOT real frequency bands)
  -> consumed by: AudioOverlays.cpp, MusicMotorController.cpp,
     ExpressiveMotion.cpp (AUDIO_REACTIVE mode), BehaviorEngine.cpp
     (LISTENING's clap nudge)
```

All consumers read the same `AudioFeatures`/`AudioVisualState` — there
is exactly one microphone-analysis pipeline in the project; nothing
duplicates raw-sample decisions elsewhere.

## Tuning guide (all constants in `include/Config.h`)

- **Too sensitive in a quiet room**: raise `AUDIO_NOISE_FLOOR`, or
  lower `AUDIO_NOISE_FLOOR_ADAPT_MARGIN` / raise
  `AUDIO_NOISE_FLOOR_ADAPT_RATE` so the adaptive floor tracks real room
  noise faster.
- **Not sensitive enough to speech**: lower `AUDIO_NOISE_FLOOR` (or its
  adaptive floor bound `AUDIO_NOISE_FLOOR_MIN`), or lower
  `AUDIO_MAX_RMS` so normal speech reaches further up the 0-1 range.
- **Music response too weak**: lower `AUDIO_MAX_RMS`, or raise
  `AUDIO_PULSE_GAIN` / `BASS_BLOOM_GAIN` for the specific overlay.
- **Constant false clap triggers**: raise `AUDIO_CLAP_THRESHOLD`, or
  raise `AUDIO_CLAP_COOLDOWN_MS` if it's rate rather than sensitivity.
- **Release too slow** (pulse stuck bright after sound ends): raise
  `AUDIO_RELEASE_SMOOTHING` (higher = faster decay).
- **Response too jittery**: lower `AUDIO_ATTACK_SMOOTHING`, or lower
  `AUDIO_RELEASE_SMOOTHING` (slower, steadier fall).
- **Lightning triggering too often**: raise
  `AUDIO_LIGHTNING_COOLDOWN_MS`, or tighten the trigger condition in
  `AudioOverlays.cpp`'s `applyLightning()`.

## Diagnostics

`d` (or `printAudioDiagnostics()`) — full on-demand dump of current
`AudioFeatures` + active tuning constants, always prints when called.
`7` toggles the **continuous** background output (the periodic
`[AUDIO]` heartbeat + mic fault-check WARN/HINT lines) — off by
default, independent of the one-shot dump and independent of sampling/
rendering themselves.

## Known limitation

No true FFT/spectral analysis anywhere in this project — see
`AUDIO_OVERLAYS.md`'s "Signal provenance" section. `MusicMotorController`
(see `MOTOR.md`) reuses this exact same `AudioFeatures.normalized`
signal for its own energy-transient beat detection — also **not** true
frequency-isolated bass detection, an approximation stated explicitly
in that module's own header comment.
