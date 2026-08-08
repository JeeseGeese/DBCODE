# Future MAMA-BEAR Knowledge Model — Promotion Recommendation

No MAMA-BEAR directory exists yet anywhere in this environment. This
document is a **recommendation for later**, not an action taken now —
`docs/lessons/` and `docs/playbooks/` are structured so promotion is a
low-effort copy/adapt later, but nothing has been moved.

## Recommended target structure

```
MAMA-BEAR/
    standards/          <- promoted from: docs/standards/*.md (largely
                            as-is — these are already written as
                            prescriptive, project-agnostic rules) plus
                            any broadly-reusable lessons that represent
                            a settled, opinionated best practice (not
                            just "what happened here")
    playbooks/          <- promoted from: docs/playbooks/*.md, largely as-is
    lessons_learned/     <- promoted from: docs/lessons/*.md, largely as-is
    architecture_patterns/ <- new: promoted from the *durable, version-
                            independent* parts of docs/architecture/
                            (e.g. `SOFTWARE_ARCHITECTURE.md`'s 7 numbered
                            patterns, `DESIGN_DECISIONS.md`'s reasoning
                            format) — not the Sunny-specific pipeline
                            docs (LED/AUDIO/MOTOR_PIPELINE.md), which
                            stay project-specific
    templates/           <- new: a generic project-cleanup/documentation-
                            baseline template distilled from this exact
                            pass (docs/current/ vs docs/V1/ split,
                            docs/architecture/ vs docs/standards/ vs
                            docs/lessons/ vs docs/playbooks/ separation,
                            docs/development/ SOP pattern,
                            docs/CLAUDE_CONTEXT_GUIDE.md pattern,
                            archive/ policy, milestone/gate roadmap
                            structure)
```

## Classification of what exists today

### Broadly reusable (candidates for `playbooks/` and `lessons_learned/` as-is)

Everything in `docs/playbooks/` was deliberately written generic, with
Sunny-specific values marked as examples:
`ESP32_BRINGUP.md`, `GPIO_VALIDATION.md`, `BUTTON_BRINGUP.md`,
`WS2812_BRINGUP.md`, `INMP441_BRINGUP.md`, `I2S_DEBUGGING.md`,
`MAX98357A_BRINGUP.md`, `SPEAKER_AUDIO_VALIDATION.md`,
`POWER_BROWNOUT_DEBUGGING.md`, `DRV8833_MOTOR_BRINGUP.md`,
`MUSIC_REACTIVE_MOTION_VALIDATION.md`, `SERIAL_DIAGNOSTICS.md`,
`SAFE_FIRMWARE_REFACTOR.md`.

Most `docs/lessons/*.md` files are similarly general:
`esp32-s3-gpio-selection`, `n16r8-reserved-flash-psram-pins`,
`button-input-pullup-wiring`, `ws2812-power-data-separation`,
`inmp441-i2s-bringup`, `i2s-32bit-container-24bit-mic-handling`,
`i2s-read-nonzero-timeout`, `shared-full-duplex-i2s`,
`max98357a-bringup`, `speaker-bridge-tied-output`,
`max98357a-sd-usage`, `max98357a-gain-configuration`,
`speaker-output-testing-methodology`, `digital-volume-vs-hardware-gain`,
`audio-buzz-noise-diagnosis`, `esp32-brownout-diagnosis`,
`led-power-limiting`, `common-ground-design`,
`serial-dispatch-single-owner`, `non-blocking-firmware-architecture`,
`host-tests-for-embedded-firmware`.

### Sunny-specific (stay in this project, do not promote as-is)

`drv8833-motor-control.md` and `motor-current-noise-mitigation.md`
contain some Sunny-specific specifics (the M80 floor is this exact
mechanism's measured property, not a universal constant) alongside
generally reusable principles — if promoted, split the general
H-bridge-control principle from the Sunny-specific calibrated numbers,
which is already how they're written (numbers marked as "(example:
Sunny)" or similar). Everything under `docs/V1/` is explicitly Sunny's
own frozen baseline snapshot and should never be promoted directly —
only lessons/playbooks extracted *from* it.

### Already formalized in `docs/standards/` (direct promotion candidates, not just "potential")

The following are no longer just informally-recurring patterns —
Sunny now has them written as explicit, prescriptive rules, which
makes them near-zero-effort to promote as-is:

- **Single-owner hardware resource discipline** — formalized in
  `docs/standards/GPIO_STANDARD.md` (rule 3) and
  `docs/architecture/SOFTWARE_ARCHITECTURE.md` ("Pattern 1"). Proven
  across LED/I2S/serial/motor subsystems on this project. A strong
  candidate for a formal "Do Better" firmware standard, not just a
  lesson.
- **Non-blocking `millis()`-based state machines, no `delay()` in
  behavior code, with a defensive max-duration backstop** — formalized
  in `docs/standards/TESTING_STANDARD.md` (rule 5) and
  `docs/lessons/non-blocking-firmware-architecture.md`.
- **Three-tier validation language** (host-validated / software-
  validated / physically-validated, never used interchangeably) —
  formalized in `docs/standards/TESTING_STANDARD.md` (rule 1) and
  `docs/standards/DOCUMENTATION_STANDARD.md` (rule 2).
- **Power/grounding discipline** (never power a current-hungry
  peripheral from the MCU's own rail, common ground mandatory,
  explicit three-state power documentation) — formalized in
  `docs/standards/POWER_STANDARD.md`.

### Archive-not-delete + living/frozen documentation structure (worth eventually formalizing in `templates/`)

- **`docs/current/` vs. `docs/V1/` (living vs. frozen-snapshot)
  documentation split, plus `docs/architecture/` (durable how/why),
  `docs/standards/` (prescriptive rules), `docs/lessons/` (discovered
  facts), `docs/playbooks/` (SOPs), `docs/development/`, `archive/`,
  and `docs/CLAUDE_CONTEXT_GUIDE.md`** — the organizational pattern
  this entire cleanup pass produced is itself a strong `templates/`
  candidate for any future Do Better project reaching its own "first
  major milestone" moment. The milestone/decision-gate roadmap format
  (`ROADMAP.md`) is a related, separately-promotable candidate.

## Recommended promotion process (when explicitly instructed)

1. Copy (don't move) `docs/lessons/*.md` and `docs/playbooks/*.md`
   into `MAMA-BEAR/lessons_learned/` and `MAMA-BEAR/playbooks/`.
2. Strip or generalize any remaining Sunny-specific numbers, replacing
   them with "(see project X for a worked example)" style references
   back to this project instead of embedded specifics.
3. Leave Sunny's own copies in place — `docs/lessons/`/`docs/playbooks/`
   should keep working as this project's own self-contained reference
   even after promotion, per the task's explicit "keep Sunny
   self-contained" instruction.
