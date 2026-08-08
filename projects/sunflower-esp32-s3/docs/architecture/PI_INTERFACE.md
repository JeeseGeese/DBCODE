# Architecture — Raspberry Pi Interface (Design Intent, Not Implemented)

**Nothing described in this file is implemented.** No Raspberry Pi
hardware, transport, or protocol code exists in this project yet. This
is the intended integration surface, so a future implementation session
starts from a considered design rather than none. See `ROADMAP.md`
(V1.2) for when this is planned.

## Division of responsibility (the one fixed decision)

**The ESP32 remains fully, independently responsible for real-time
hardware safety, regardless of what the Raspberry Pi is doing or
whether it's even connected.** Emergency stop, the motor max-energized
backstop, and `MotorPowerGuard` must never depend on the Pi being
present, booted, or responsive. The Pi is planned to own everything
*above* real-time control: vision (via a future camera, see
`CAMERA_INTERFACE.md`), speech/LLM interaction, and any other
higher-level compute. See `DESIGN_DECISIONS.md`'s "Why Raspberry Pi
owns AI and camera?".

## Intended integration surface

`BehaviorEngine`'s existing public API
(`setBehaviorState()`/`getBehaviorState()`/`behaviorStateName()`/
`startBehaviorDemo()`/`printBehaviorStatus()`) is the intended
semantic entry point — a future Pi sends high-level state requests,
not low-level motor/LED commands. Sketch (from prior planning,
preserved in `archive/superseded_docs/BEHAVIOR_ENGINE_DEVELOPMENT.md`
section 12):

```
"BEHAVIOR LISTENING" -> setBehaviorState(BehaviorState::LISTENING)
"BEHAVIOR THINKING"  -> setBehaviorState(BehaviorState::THINKING)
"BEHAVIOR EXCITED"   -> setBehaviorState(BehaviorState::EXCITED)
"BEHAVIOR IDLE"      -> setBehaviorState(BehaviorState::IDLE)
```

## Open questions (explicitly undecided)

- **Transport**: USB serial, Wi-Fi socket, Bluetooth, local HTTP/
  WebSocket, or direct I2S are all still candidates — none chosen.
- **Audio handoff**: if the Pi needs to inject or receive PCM audio, it
  must go through one explicit owner sitting above `SharedI2S`, not a
  second I2S consumer — see `AUDIO_PIPELINE.md`. A candidate shape
  (`beginAudioOutput()`/`queuePcm()`/`playAnnouncement()`) exists in
  the same archived planning doc, section 17, but is unimplemented.
- **Boot/shutdown/recovery behavior** between the two processors —
  not yet designed.
- **Power relationship** between the ESP32 and the Pi — not yet
  designed; see `docs/current/POWER.md` for the ESP32-side constraints
  any design must respect.
- **Feedback prevention**: the microphone will hear the speaker (and,
  eventually, the Pi's own speech output) — a `LISTENING`/`SPEAKING`/
  `POST_SPEECH_COOLDOWN` state design exists in the same archived doc,
  section 18, not implemented.

## Decision gate before this becomes real work

Per `ROADMAP.md`: Raspberry Pi integration is the next major
phase (V1.2), planned to begin only once the V1.1 decision gates
(speaker acceptable, power architecture acceptable) are passed —
starting Pi integration on top of an unresolved speaker/power question
would make it hard to tell which system introduced a new symptom.

## See also

`docs/current/SOFTWARE_ARCHITECTURE.md`, `ROADMAP.md`,
`archive/superseded_docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` (sections
12, 14), `archive/superseded_docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`
(sections 15-19).
