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
Sunny V1.1  -- ESP32 refinement / reliability / speaker / power /
               documentation -- COMPLETE (see "V1.1" below) -- was NOT
               a new-feature sprint; the goal was a stable platform to
               build the touchscreen/UI on top of.
   |
   v
[GATE] V1.1 physically validated -- PASSED (2026-08-08, battery power)
   -- see docs/current/V1_1_STATUS.md's "Physical validation result" --
      combined-load operation, speaker/LED/button/mic checks all
      confirmed, no open safety concern carried forward. Electrical
      brownout root cause was NOT required to be formally closed to
      pass this gate (see below) -- it needed to be documented,
      understood, and not actively unsafe, which it is.
   |
   v
Sunny V1.2  -- Touchscreen + UI programming -- ACTIVE (Beta 1 checkpoint,
                Phase V1.2.3 next)
   -- ELEGOO ESP32-WROOM-32E touch display, separate project
      (projects/sunny-display-esp32/); standalone bring-up and touch
      calibration complete -- see "V1.2" below
   |
   v
[GATE] Touchscreen UI stable
   -- display initializes reliably, core screens (status/diagnostics/
      controls) work without corrupting or blocking existing LED/audio/
      motor timing, UI-to-subsystem interface is clean (read-only status
      + explicit command paths, not direct internal-state pokes)
   |
   v
Sunny V1.3  -- Raspberry Pi integration
   |
   v
[GATE] ESP32 <-> Pi communication validated
   -- transport chosen and working; real-time safety confirmed
      independent of Pi presence/responsiveness (see
      docs/architecture/PI_INTERFACE.md)
   |
   v
Sunny V1.4  -- Camera integration (via the Pi, not the ESP32)
   |
   v
[GATE] Vision stable
   -- vision pipeline reliable enough to drive BehaviorEngine state
      requests without spurious/flickering transitions
   |
   v
Sunny V1.5  -- LLM / voice / personality integration
   |
   v
[GATE] Speech + feedback prevention validated
   -- the sunflower does not trigger its own audio-reactive behavior
      from its own speech (see docs/architecture/PI_INTERFACE.md's
      feedback-prevention note)
   |
   v
Sunny V2.0  -- mature integrated Sunny (vision + voice + movement +
               lights + touchscreen UI, production power system,
               production wiring/PCB, final enclosure, polished
               behavior)
```

**Sequence correction (2026-08-08):** Touchscreen/UI now comes
immediately after V1.1, **before** Raspberry Pi integration (previously
V1.2) — Pi is now V1.3, camera V1.4, LLM/voice V1.5. This reflects a
deliberate reprioritization, not a scope change to any individual
milestone's content. Later milestones (V1.4 onward) remain provisional
and may be renumbered again as work approaches them.

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

## Sunny V1.1 — ESP32 refinement / reliability / speaker / power / documentation — COMPLETE

Began immediately following the `sunny-v1-baseline` tag. **Not a
new-feature sprint** — the goal was a stable ESP32 platform to build
V1.2's touchscreen/UI on top of, not new subsystem capability. **Status
as of 2026-08-08: COMPLETE** — implementation, documentation, and
physical validation (battery power) all passed — see
`docs/current/V1_1_STATUS.md` for the full record.

Accomplished this milestone:

- Speaker diagnostic/refinement framework: normal-use volume ladder
  (35/50/60/70/80/90/100%, 70% default), silence/carrier/lowmidhigh/
  speech/music diagnostics, APLL investigated and closed. Residual
  low-frequency buzz remains **unresolved and prototype-sensitive**,
  documented as such, not claimed fixed — see `docs/current/SPEAKER.md`.
- 36-LED physical count correction (was incorrectly 58) — see
  `docs/lessons/verify-physical-led-count.md`.
- HWTEST power-safety fix: the startup LED test sequence now uses the
  same `applyPowerLimit()` current-limiting path as normal rendering,
  physically verified. See `docs/current/POWER.md` section D.
- Brownout/reset-loop/solid-white incident investigated and documented.
  Leading hypothesis (computer-USB vs. battery-pack source power) is
  **strong evidence, not a formally closed electrical root cause** — see
  `docs/current/POWER.md`'s "Current brownout investigation".
- 330Ω WS2812 data-line resistor, 1000µF bulk capacitor, and breadboard/
  Dupont prototype limitations all documented without overclaiming.
- Full host test suite restored/verified (20/20 passing).

**This gate does not require the brownout electrical root cause to be
formally closed** — it requires the finding to be documented,
understood, and not actively unsafe (brownout detector stays enabled,
no permanent performance-crippling workaround was applied to hide the
prototype power limitation — `MusicMotorController` remains fully
operational). See "V1.1 exit criteria" in `docs/current/V1_1_STATUS.md`.

## Sunny V1.2 — Touchscreen + UI programming — ACTIVE (Beta 1 checkpoint)

**Status as of 2026-08-16: ACTIVE. V1.2.1 (display/touch standalone
bring-up) is COMPLETE (physically validated). V1.2.2 (touch calibration)
is COMPLETE** — a real mapping bug (the original model produced
~30-42px corner error; an inset target's raw reading was wrongly treated
as measured at the true screen edge, and an earlier "enlarging the
hitbox fixed it" conclusion was premature and has since been corrected)
was fixed with a per-axis linear fit (<2px error on the measured
dataset), and its physical retest has since confirmed the fix is
spatially accurate. **This checkpoint is preserved as `sunny-v1.2-beta1`**
— see
[`../sunny-display-esp32/docs/V1_2_BETA1_STATUS.md`](../sunny-display-esp32/docs/V1_2_BETA1_STATUS.md)
for the full record; V1.2.3 (Sunny UI foundation) is the next
sub-phase, not yet started. Hardware is identified and
confirmed (ELEGOO ESP32 2.8" Touch Display, ESP32-WROOM-32E, ILI9341,
XPT2046 resistive touch — LCDWIKI model E32R28T) — **not** a
Raspberry-Pi-attached peripheral; it's a **second, independent ESP32
controller**. See
[`../sunny-display-esp32/docs/DISPLAY_HARDWARE.md`](../sunny-display-esp32/docs/DISPLAY_HARDWARE.md)
for the full hardware identity, pin map, driver stack, and bring-up
status, and that same doc's "V1.2 internal roadmap" section for the
V1.2.1–V1.2.8 sub-phase breakdown this milestone follows.

Goals (unchanged from the original framing, now being executed):

- Bring up the display/touch hardware standalone — **V1.2.1 physically
  validated 2026-08-08**: renders correctly right-side-up in landscape
  at `rotation=3` (`rotation=1` was tried first and found upside down on
  real hardware), touch initializes and maps correctly. See the
  UI-controller project's own docs. **Touch calibration (V1.2.2)
  physically validated** — the corrected per-axis linear-fit model
  (fixed 2026-08-09 after a real corner-mapping bug) is applied in
  `TouchManager.cpp`; the board boots into a touch-validation screen,
  and its spatial accuracy across all 5 targets + TAP TEST has been
  physically confirmed.
- UI framework: **LovyanGFX + LVGL 9.5.0**, selected after inspecting
  the actual confirmed hardware (not assumed in advance) — see
  `DISPLAY_HARDWARE.md`'s "Driver stack selection" section for the
  evaluated alternatives and why.
- Sunny's local UI architecture: DISPLAY HARDWARE / TOUCH HARDWARE /
  UI STATE / UI SCREENS / COMMUNICATION separation, established in the
  new project — see its `docs/DISPLAY_HARDWARE.md`'s "Software
  architecture" section. The UI layer will consume status, not bypass
  existing owners, once V1.2.4 (body↔display communication) begins —
  not implemented yet.
- Diagnostics/status screens, controls/settings screens, animations —
  all still design-only (V1.2.5–V1.2.7), not yet implemented.
- The body controller (`sunflower-esp32-s3`) is **untouched** by V1.2
  work so far and remains at its validated `sunny-v1.1` state — the two
  projects are electrically unconnected (standalone-first bring-up;
  see `DISPLAY_HARDWARE.md`'s safety rule).

See `docs/CLAUDE_CONTEXT_GUIDE.md`'s "Touchscreen/UI work (V1.2)"
section for the minimal context set for continuing this milestone.

## Sunny V1.3 — Raspberry Pi integration

Begins once the touchscreen-UI gate is passed. Define the ESP32↔Pi
transport and protocol, processor responsibilities, boot/shutdown/
recovery behavior, and the power relationship. See
`docs/architecture/PI_INTERFACE.md` for the design intent and open
questions.

## Sunny V1.4 — Camera integration

Begins once the Pi-communication gate is passed. Camera connects to
and is managed by the Pi, not the ESP32. See
`docs/architecture/CAMERA_INTERFACE.md`.

## Sunny V1.5 — LLM / voice / personality integration

Begins once the vision-stability gate is passed. Speech output (local
pre-recorded first, then dynamic LLM-generated via the Pi), high-level
behavior commands, and feedback prevention (the mic will hear the
speaker). The Pi becomes the higher-level compute/AI platform; the
ESP32 remains the real-time hardware controller — see
`docs/architecture/DESIGN_DECISIONS.md`.

## Sunny V2.0 — mature integrated Sunny

Vision + voice + movement + lights + touchscreen UI working together,
a production power system, production soldered wiring/a PCB, final
enclosure/mechanical architecture, polished user-facing behavior.

---

This roadmap is provisional and should remain easy to revise. See
`docs/V1/CHANGELOG.md` for how Sunny actually got to V1.0, and
`docs/lessons/`/`docs/playbooks/`/`docs/standards/` for the reusable
engineering content extracted along the way.
