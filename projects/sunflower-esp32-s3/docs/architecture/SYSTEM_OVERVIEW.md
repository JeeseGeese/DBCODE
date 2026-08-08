# Architecture — System Overview

Describes **how Sunny is built and why**, independent of which version
is currently active. For current state/status, see `docs/current/`.
For the frozen V1 snapshot, see `docs/V1/`.

## The shape of the system

Sunny is one ESP32-S3 firmware image coordinating five real-time
subsystems through a single-threaded, non-blocking `loop()`:

```
                    main.cpp (setup/loop, frame composition, serial dispatch)
                              |
        +----------+----------+----------+----------+
        |          |          |          |          |
     LEDs        Audio       Motor     Buttons    Serial
   (LedEffects  (SharedI2S,  (MotorDriver +      (Controls.cpp,
   +Overlays)   AudioAnalyzer, layered           single owner)
                SpeakerTest)  behaviors)
```

Each subsystem has a designated **sole owner** module for its hardware
resource (see `SOFTWARE_ARCHITECTURE.md`), and every higher-level
behavior reaches that resource only through the owner's public API.
This is the single strongest architectural decision in this codebase —
see `DESIGN_DECISIONS.md`.

## Why this shape

A single MCU, single-threaded, real-time animatronic system has one
hard constraint: nothing may block, because everything (LED refresh
rate, audio capture continuity, motor safety timing, emergency stop
latency) depends on `loop()` returning quickly and often. The
architecture follows from that constraint:

- **Non-blocking state machines everywhere** (`docs/standards/TESTING_STANDARD.md`'s
  companion principle) — see `DESIGN_DECISIONS.md`'s "Why non-blocking
  architecture?".
- **Single-owner hardware resources** — prevents two modules from
  fighting over the same peripheral in ways that would otherwise
  require locking (unavailable/inappropriate in this environment) or
  produce silent races (this project has hit that exact failure once,
  with two `Serial` readers — see `docs/lessons/serial-dispatch-single-owner.md`).
- **A layered compute model** — the ESP32 owns everything real-time and
  safety-critical; a future Raspberry Pi owns everything higher-level
  (vision, LLM/voice) and is never a dependency for real-time safety.
  See `PI_INTERFACE.md` and `DESIGN_DECISIONS.md`.

## The five subsystems, one line each

- **LED**: `docs/architecture/LED_PIPELINE.md` — base effect + optional
  audio overlay, composed once per frame.
- **Audio**: `docs/architecture/AUDIO_PIPELINE.md` — one shared
  full-duplex I2S bus serving both microphone capture and speaker
  output.
- **Motor**: `docs/architecture/MOTOR_PIPELINE.md` — a layered stack
  from raw GPIO control up through music-reactive and personality-
  driven behavior, with one shared safety net.
- **Buttons/Serial**: single-owner input handling, feeding semantic
  events to every other subsystem — see `SOFTWARE_ARCHITECTURE.md`.
- **(Future) Raspberry Pi / Camera**: `PI_INTERFACE.md` /
  `CAMERA_INTERFACE.md` — not yet implemented, documented as design
  intent.

## See also

`docs/architecture/ESP32_ARCHITECTURE.md`,
`docs/architecture/SOFTWARE_ARCHITECTURE.md`,
`docs/architecture/DESIGN_DECISIONS.md`.
