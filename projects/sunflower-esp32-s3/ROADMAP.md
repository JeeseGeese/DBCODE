# Roadmap — sunflower-esp32-s3

A living roadmap. **Nothing in the "Current milestone," "Upcoming
milestones," "Long-term vision," or "Deferred ideas" sections below is
implemented.** Only the "Completed milestones" section describes finished,
physically-validated work — everything else is planning, in-progress
work, or explicitly deferred design. For exact current implementation
state, see `CURRENT_STATUS.md`, not this file.

This roadmap should be revised as work completes or plans change — it is
not a commitment schedule, and items may be reordered, merged, or dropped
as physical testing reveals what actually needs to happen next.

## Completed milestones

Physically validated, tagged or otherwise closed:

- **`v1.0.0`** — first physically-validated baseline: four-button
  control, WS2812 LED effects, INMP441 audio input and audio-reactive
  overlay, bidirectional DRV8833 motor control, the mechanical belt fix,
  motor+LED coexistence (`MotorPowerGuard`), the centralized serial
  dispatcher, reliable `k` emergency stop, the audio logging toggle, the
  LED index-mapping diagnostic.
- **DRV8833 motor bring-up** — full investigation closed; root cause
  (mechanical belt preload) confirmed and fixed. See
  `docs/DRV8833_MOTOR_BRINGUP.md` section 20.
- **MusicMotorController PWM/speed calibration (Revisions 1-3)** — M80
  established as the physically-validated minimum reliable movement
  command; forward/reverse timing table confirmed at M80/M90/M100.
- **MusicMotorController Revision 9 initial drop detection** — physically
  validated in one drop test (which itself surfaced the issues Revision
  10 addresses).
- **MusicMotorController Revision 10.1 deadlock fix** — physically
  validated; see `CURRENT_STATUS.md` for details.

## Current milestone

**Expressive Motion / MusicMotor Revision 10.1**, on
`feature/expressive-motion-v1` (branched from `v1.0.0`).

Corresponds to Phase 2 of the phased plan below (expressive motor/audio/
LED behavior) plus the ongoing MusicMotorController revision line, which
was not originally scoped as its own numbered phase but has become the
project's primary active development thread. See `CURRENT_STATUS.md` for
exactly what's done, in progress, and open within this milestone.

## Upcoming milestones

Roughly in order, though later items may reorder based on what physical
testing surfaces:

1. **Speaker debugging.** Physical audio-quality validation of the
   MAX98357A output (volume, clarity, cleanliness) beyond the current
   write-path-only verification — named as the explicit next development
   objective as of the 2026-07-30 engineering checkpoint (commit
   `64e8aee`, tag `sunny-rev10.1-checkpoint`). Read `README.md`'s
   "Speaker hardware test" startup safety sequence in full before
   connecting anything.

   **Note on ordering:** this item corresponds to Phase 3 in this same
   list (below), which `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 14
   documents as intended to begin only after Phase 2's movement behaviors
   (`ExpressiveMotion`/`BehaviorEngine`) are physically approved — see
   item 3 below, which is not yet complete. That ordering constraint
   predates this checkpoint and has not been revisited or explicitly
   overridden; it's recorded here rather than silently dropped so the
   next session (or the user) can decide whether to proceed with speaker
   debugging ahead of that approval or to reconcile the two first.
2. **Wider MusicMotorController A/B tuning** across more songs and
   genres beyond the current single `EDM_DUBSTEP` profile — the
   genre-profile architecture already supports adding profiles without
   restructuring the detector.
3. **Complete physical validation of `ExpressiveMotion` and
   `BehaviorEngine`** — run the checklists in
   `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 12 and
   `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 15. This is the largest
   current gap between "software-validated" and "physically validated,"
   and is the Phase 2 approval referenced in item 1's ordering note above.
4. **Dedicated motor power supply evaluation** — replace the current
   bench-only ESP32-3.3V-rail motor power arrangement with a proper
   external supply, common-grounded with the ESP32, sized for actual
   motor current draw including inrush. This removes the underlying
   reason `MotorPowerGuard`'s LED-muting workaround exists at all, though
   it does not have to precede everything above it — see "Long-term
   vision" for how this interacts with `DanceEngine`/`MusicMotorController`
   not using any LED mitigation today.
5. **Phase 3 — speaker and digital amplifier integration** (same
   underlying work as item 1 above, restated here to keep the phase
   numbering below intact). See item 1 for current status and the
   ordering note.
6. **Phase 4 — spoken setting announcements.** Local pre-recorded
   announcements (mute/unmute, brightness level, mode changes, etc.) via
   the central announcement API design in
   `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` sections 15-16 and 19. Not
   started; no code exists yet.
7. **Phase 5 — Raspberry Pi/LLM speech integration.** Dynamic
   LLM-generated speech from a companion computer. Not started; transport
   (USB serial, Wi-Fi socket, Bluetooth, local HTTP/WebSocket, or direct
   I2S) is explicitly undecided — see
   `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 16.

## Long-term vision

Direction, not a committed spec:

- A sunflower that responds to its environment (sound, and eventually
  presence/attention via a future camera) with movement, light, and
  voice that reads as a coherent, alive-feeling personality —
  `BehaviorEngine`'s named states (`CURIOUS`/`LISTENING`/`THINKING`/
  `EXCITED`/`SLEEPING`) are the current best expression of that intent,
  though they're still software-only.
- A future Raspberry Pi (or similar) companion computer providing speech
  recognition and LLM-driven responses, communicating with the ESP32 over
  a yet-to-be-determined transport, with the ESP32 remaining fully
  independently responsible for real-time motor safety (`k`, the
  max-energized safeguard, `MotorPowerGuard`) regardless of what the
  higher-level computer is doing — this independence is a deliberate,
  already-stated design requirement, not just a current implementation
  detail (`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 16).
- Feedback-aware listening: the sunflower must never be able to trigger
  its own audio-reactive behavior from its own speaker (see "Deferred
  ideas" below — the design exists, nothing is implemented).
- A properly powered motor subsystem (dedicated supply) that removes the
  current shared-rail LED/motor contention entirely, rather than working
  around it.
- Physical inputs beyond the current 4 buttons — 1-2 additional momentary
  buttons and/or 1-2 latching switches for conversation/voice control —
  see "Deferred ideas."

## Deferred ideas

Documented, explicitly **not implemented**, not currently scheduled. Kept
here so a future session doesn't have to re-derive the design from
scratch, and so nobody mistakes "documented" for "planned for the next
milestone":

- **Motion accent** — a subtle brightness/highlight pulse synchronized to
  movement start, coexisting with the currently-animating base effect.
  Deferred because implementing it without a real
  compositing/layering mechanism in the LED render pipeline would require
  a larger, more invasive change than currently justified — see
  `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 7. Revisit only if a
  proper LED layering mechanism becomes independently justified.
- **Per-state Behavior Engine LED accents** (including a `SLEEPING`
  dim/rest presentation) — same reasoning and same deferral as motion
  accent, see `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 6. Explicit
  instruction on record: do not introduce a new LED rendering pipeline to
  force this in.
- **Central announcement API + priority queue** (`AnnouncementId`,
  bounded fixed-size priority queue, coalescing of duplicate
  announcements) — full design in
  `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` sections 15 and 19. No code
  exists.
- **I2S resource ownership for the amplifier beyond current bring-up** —
  `beginAudioOutput()`/`queuePcm()`/`playAnnouncement()`-shaped API
  described in section 17 of the same doc; `SharedI2S` as it exists today
  is the low-level bring-up layer this would sit on top of, not this API
  itself.
- **Feedback prevention during speech** (`AudioInteractionState`:
  `LISTENING`/`SPEAKING`/`POST_SPEECH_COOLDOWN`) — suppress clap
  detection and audio-reactive motor/LED triggers while the sunflower is
  speaking through its own speaker. Design only, section 18 of the same
  doc.
- **Buttons 5 and 6** — proposed roles: Button 5 for motion-state cycling
  (short press) / `motion demo` (long press); Button 6 for voice-prompt
  toggle (short press) / push-to-talk (long press). Not wired, no GPIOs
  assigned. See `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 22 for
  the full electrical assumptions and safety notes any real
  implementation must satisfy.
- **Latching switches for conversation/voice control** — a
  conversation-enable switch and a spoken-announcements-enable switch,
  treated as persistent physical state rather than momentary button
  events. Not wired, no GPIOs assigned. Full behavioral requirements
  (must sample actual position at boot, must not silently drift from
  physical state, must not block other subsystems) are in
  `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 23.
- **Camera-driven behavior mapping** (face detected → `CURIOUS`, etc.) —
  illustrative only, `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 13. No
  camera hardware exists in this project.
- **A dedicated `SPEAKING` BehaviorState** — deliberately not added yet;
  `LISTENING` is judged close enough to "attentive, mostly still" for
  now. Add only if a future generation needs materially different
  movement while actively speaking versus listening (see
  `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 14).
- **Merging `DanceEngine` into `MusicMotorController`** (or retiring
  `DanceEngine` outright) — not decided either way; see the open
  engineering question in `CURRENT_STATUS.md`.
