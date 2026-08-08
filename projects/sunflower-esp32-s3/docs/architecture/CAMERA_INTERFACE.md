# Architecture — Camera Interface (Design Intent, Not Implemented)

**Nothing described in this file is implemented. No camera hardware
exists in this project.** See `ROADMAP.md` (V1.3) for timing.

## The one fixed decision: the camera connects to the Pi, not the ESP32

The camera is planned to connect to and be managed by a future
Raspberry Pi companion — **not** to be wired as a direct ESP32
peripheral. Do not plan or wire a camera module to any ESP32 GPIO
unless that decision is explicitly, deliberately revisited later (see
`ROADMAP.md`'s explicit ordering: Raspberry Pi integration (V1.2)
must be working *before* camera integration (V1.3) begins).

## Why

The ESP32's role is fixed as the real-time hardware controller (see
`PI_INTERFACE.md` and `DESIGN_DECISIONS.md`'s "Why Raspberry Pi owns
AI and camera?"). Vision processing is compute- and memory-intensive
in a way that doesn't fit the ESP32's role or, generally, its
resources — it belongs on the higher-level compute platform alongside
the LLM/voice work that's also planned there.

## Intended (illustrative only) behavior mapping

From prior planning (preserved in
`archive/superseded_docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 13),
not implemented, not a commitment to this exact mapping:

```
face detected nearby      -> BehaviorState::CURIOUS
face detected + speaking  -> BehaviorState::LISTENING
no motion for a while     -> BehaviorState::IDLE or BehaviorState::SLEEPING
sudden motion/loud event  -> BehaviorState::EXCITED (bounded episode, auto-returns to IDLE)
```

This would flow through the exact same `BehaviorEngine` API described
in `PI_INTERFACE.md` — the camera does not get its own special motor/
LED access path; vision events become `BehaviorState` requests like
everything else.

## Open questions (explicitly undecided)

- Camera hardware/module choice.
- Vision pipeline scope (simple presence/motion detection vs. actual
  face/object recognition) — likely to start minimal.
- How vision events reach `BehaviorEngine` — presumably via whatever
  Pi transport is chosen in `PI_INTERFACE.md`, not a separate channel.
- Power/mounting/mechanical integration — not yet considered at all.

## Decision gate before this becomes real work

Per `ROADMAP.md`: camera work begins only after the Raspberry Pi
integration's own decision gate (ESP32 ↔ Pi communication validated)
passes.

## See also

`docs/architecture/PI_INTERFACE.md`, `docs/current/GPIO_MAP.md`,
`docs/current/POWER.md`, `ROADMAP.md`.
