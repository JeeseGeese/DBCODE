# Roadmap — sunflower-esp32-s3

A living, provisional roadmap — revise freely as work completes or
plans change. This is not a commitment schedule. For exact *current*
implementation state, see `CURRENT_STATUS.md`/`docs/current/`. For the
V1 baseline this roadmap builds forward from, see `docs/V1/`.

**One active codebase.** Every milestone below is continued
development on the same source tree from the `sunny-v1-baseline`
checkpoint — never a duplicated/forked source folder. See
`docs/V1/VERSIONING.md` for the git-tag-based strategy.

## Milestone gates, not just version numbers

Progress is evaluated by **decision gates** — an explicit, checkable
condition that must be true before the next milestone begins — not
just by incrementing a version number. A milestone can be
code-complete and software-validated while its gate is still open; the
gate is what says "this is actually ready to build on."

```
Sunny V1  (baseline captured 2026-08-07 — see docs/V1/, tag
           sunny-v1-baseline)
   |
   v
Sunny V1.1  (current active development)
   -- speaker buzz/static cleanup, speaker normal-use volume refinement,
      continued LED BaseEffect/AudioOverlay refinement, power/ground
      cleanup, firmware stability/polish -- see "V1.1" below
   |
   v
[GATE] Speaker acceptable
   -- residual buzz/noise root cause understood well enough to pick a
      default volume/gain configuration for normal use (see
      docs/current/SPEAKER.md)
   |
   v
[GATE] Power architecture acceptable
   -- combined LED+amplifier+motor load on the shared 5V rail measured
      under real conditions; either confirmed adequate or a dedicated
      motor supply adopted (see docs/current/POWER.md)
   |
   v
[GATE] V1.1 refinements physically validated
   -- ExpressiveMotion/BehaviorEngine checklists run; no open safety
      concern carried forward
   |
   v
Sunny V1.2  -- Raspberry Pi integration
   |
   v
[GATE] ESP32 <-> Pi communication validated
   -- transport chosen and working; real-time safety confirmed
      independent of Pi presence/responsiveness (see
      docs/architecture/PI_INTERFACE.md)
   |
   v
Sunny V1.3  -- Camera integration (via the Pi, not the ESP32)
   |
   v
[GATE] Vision stable
   -- vision pipeline reliable enough to drive BehaviorEngine state
      requests without spurious/flickering transitions
   |
   v
Sunny V1.4  -- LLM / voice / personality integration
   |
   v
[GATE] Speech + feedback prevention validated
   -- the sunflower does not trigger its own audio-reactive behavior
      from its own speech (see docs/architecture/PI_INTERFACE.md's
      feedback-prevention note)
   |
   v
Sunny V2.0  -- mature integrated Sunny (vision + voice + movement +
               lights, production power system, production wiring/PCB,
               final enclosure, polished behavior)
```

A gate is "passed" when its condition is true and a human has recorded
that observation (per this project's physical-validation policy,
`/AGENTS.md` section 6) — not when the corresponding code merely
exists. Gates may be revisited/reopened if a later milestone's work
reveals the earlier one wasn't actually solid.

## Sunny V1.0 — baseline (captured, frozen)

See `docs/V1/RELEASE_NOTES.md` for the full detail: LEDs + dynamic
BaseEffects + AudioOverlays, INMP441 mic, MAX98357A speaker (shared
I2S, full diagnostic suite, buzz not yet resolved), 4 buttons + unified
Audio Mode, DRV8833 motor fully integrated (`MusicMotorController`
Rev 10.1, `ExpressiveMotion`/`BehaviorEngine`), 18 host tests, and this
documentation baseline itself.

## Sunny V1.1 — current active development

Begins immediately following the `sunny-v1-baseline` tag. No major
processor-architecture change is planned for this phase — the goal is
to earn the "Speaker acceptable" and "Power architecture acceptable"
gates above, plus general refinement. Primary V1.1 goals:

- Speaker buzz/static cleanup (see `docs/current/SPEAKER.md`'s open
  hypotheses) toward the "Speaker acceptable" gate above.
- Speaker normal-use volume refinement.
- Continued LED BaseEffect refinement
  (`docs/development/ADDING_LED_EFFECTS.md`).
- Continued AudioOverlay refinement
  (`docs/development/ADDING_AUDIO_OVERLAYS.md`).
- Power/ground cleanup — measure combined-load power behavior toward
  the "Power architecture acceptable" gate above; evaluate a dedicated
  external motor supply.
- Firmware stability/polish, including the `ExpressiveMotion`/
  `BehaviorEngine` physical validation checklists
  (`docs/current/KNOWN_LIMITATIONS.md`) — the largest current
  software-vs-physical gap.
- Motor behavior may be refined if desired
  (`docs/playbooks/MUSIC_REACTIVE_MOTION_VALIDATION.md`), but motor
  integration is already complete — this is polish, not new work.

## Sunny V1.2 — Raspberry Pi integration

Begins once V1.1's gates are passed. Define the ESP32↔Pi transport and
protocol, processor responsibilities, boot/shutdown/recovery behavior,
and the power relationship. See `docs/architecture/PI_INTERFACE.md`
for the design intent and open questions.

## Sunny V1.3 — Camera integration

Begins once the Pi-communication gate is passed. Camera connects to
and is managed by the Pi, not the ESP32. See
`docs/architecture/CAMERA_INTERFACE.md`.

## Sunny V1.4 — LLM / voice / personality integration

Begins once the vision-stability gate is passed. Speech output (local
pre-recorded first, then dynamic LLM-generated via the Pi), high-level
behavior commands, and feedback prevention (the mic will hear the
speaker). The Pi becomes the higher-level compute/AI platform; the
ESP32 remains the real-time hardware controller — see
`docs/architecture/DESIGN_DECISIONS.md`.

## Sunny V2.0 — mature integrated Sunny

Vision + voice + movement + lights working together, a production
power system, production soldered wiring/a PCB, final enclosure/
mechanical architecture, polished user-facing behavior.

---

This roadmap is provisional and should remain easy to revise. See
`docs/V1/CHANGELOG.md` for how Sunny actually got to V1.0, and
`docs/lessons/`/`docs/playbooks/`/`docs/standards/` for the reusable
engineering content extracted along the way.
