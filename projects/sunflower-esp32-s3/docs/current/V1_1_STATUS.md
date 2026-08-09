# Sunny V1.1 — Closure Status

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see `docs/V1/`. Update this file, not `docs/V1/`, when things change.

## Status

**V1.1 COMPLETE.**

Physical validation was run on battery power and approved by the user
on 2026-08-08. Implementation, host-testing, documentation, and
physical validation are all done. See "Physical validation result"
below for exactly what was confirmed.

Do not treat "implementation complete" and "physically validated" as
the same claim in future updates to this file — see `/AGENTS.md`
section 6 (physical validation policy). This entry distinguishes them:
implementation/host-testing completed earlier in the V1.1 work; the
physical pass is what moved this file's status from "ready for
validation" to "complete."

## Physical validation result (2026-08-08, battery power)

The checklist in `docs/current/TESTING.md`'s "V1.1 closure validation"
section was run on real hardware, on battery power, and approved by
the user. Confirmed:

- Clean boot; `[HWTEST] LED count configured: 36`; startup HWTEST power
  limiter active; no brownout warning at startup.
- Buttons: Mode, Mute, Brightness, Button4 short (overlay toggle) and
  long (Audio Mode ON/OFF with green/red cues) all functional.
- LED effects and AudioOverlay: multiple base effects and overlays
  cycled correctly, including brightness raised through 100% with the
  power limiter still engaging as expected.
- Microphone: audio-reactive overlay responded to speech/claps.
- MusicMotor: responded to audio; normal high-intensity combined LED +
  motor operation confirmed on battery power with no brownout.
- Speaker subsystem: `speaker volume` confirmed 70% default;
  `speaker silencecheck`/`carriercheck` remained clean; `lowmidhigh`/
  `speechtest`/`musictest` all usable. The known residual low-frequency
  buzz was still present — expected, not a regression (see "Explicitly
  NOT claimed" below).
- Combined system: LEDs + AudioOverlay + MusicMotor + mic + speaker all
  active simultaneously on battery power with no reset/brownout.

This satisfies the V1.1 exit criteria that required physical
confirmation (see the updated table below).

## What V1.1 is (and isn't)

V1.1 is an **ESP32 refinement / reliability / speaker / power /
documentation** milestone, not a new-feature sprint. Its purpose is to
produce a platform stable enough to build V1.2's touchscreen/UI work on
top of. No major processor-architecture change was made; no new
subsystem (motor, LEDs, mic, speaker) was added — all four were already
integrated at the V1.0 baseline.

## Major V1.1 accomplishments

- **Speaker diagnostic/refinement framework**: normal-use volume ladder
  35/50/60/70/80/90/100%, 70% boot default (was a conservative 2-25%
  bring-up ladder). New diagnostics: `silencecheck`, `carriercheck`,
  `lowmidhigh`, `speechtest`, `musictest`, `isolate`. `use_apll`
  investigated and closed with source-level HAL evidence (not supported
  on ESP32-S3 in this framework).
- **36 physical LEDs corrected** — firmware previously reported 58 (a
  value that predates this project's own independent verification).
  `NUM_LEDS` is now the physically-confirmed count; every render
  loop/buffer/the power estimator already read it symbolically, so one
  constant change was sufficient.
- **HWTEST routed through the power limiter** — the boot-time LED test
  sequence previously wrote raw, unscaled full-brightness frames
  (~2196mA estimated for SOLID WHITE at the corrected LED count),
  bypassing the same protection normal rendering has always had. Fixed
  and **physically verified** on real hardware.
- **1000mA LED current budget retained** — not raised to make a symptom
  disappear; the estimator's accuracy was corrected (count fix), not
  its safety threshold.
- **Brownout/reset-loop investigation documented** — a real reset-loop/
  stuck-solid-white incident occurred during this milestone (resolved,
  not permanent damage) and is preserved for future reference, along
  with the battery-vs-computer-USB power finding (see below).
- **330Ω WS2812 data resistor documented** as current hardware practice
  (`ESP32 GPIO4 -> 330Ω -> WS2812 DIN`), a signal-integrity measure, not
  a power-rail component.
- **Breadboard/Dupont prototype limitations documented** — ground-
  reference sensitivity (MAX98357A ground change visibly corrupting
  LED colors) and general transient-load fragility of the current
  solderless prototype are recorded as reusable lessons, not folded
  into any single "root cause" claim.
- **Full host test suite restored and extended** — 20/20 `test_host/*.cpp`
  files compile and pass, 0 warnings.

## Explicitly NOT claimed

- **Speaker buzz is NOT completely solved.** A residual low-frequency
  buzz remains, documented as unresolved and prototype-sensitive — see
  `docs/current/SPEAKER.md`. A usable normal-volume range was
  established despite this; that is a different, weaker claim than "the
  buzz is fixed."
- **The brownout electrical root cause is NOT formally closed.**
  Computer-USB source power remains the **leading hypothesis** (strong
  physical evidence: battery-pack power has eliminated the brownouts
  under the same previously-problematic load), not a proven cause. No
  controlled multi-node voltage measurement or component-level A/B test
  has isolated exactly which link in the USB path was limiting. See
  `docs/current/POWER.md`'s "Current brownout investigation" for the
  full record.
- **No permanent PCB has been electrically validated.** The current
  hardware is still a solderless breadboard/Dupont prototype. All
  power/ground findings in this milestone describe *this* prototype's
  behavior, not a production PCB's.

These three remain open items for future hardware refinement/
productization work — not blockers for V1.1 closure, which only
requires them to be *documented accurately*, not resolved.

## V1.1 exit criteria

| # | Criterion | Status |
|---|---|---|
| 1 | Physical LED count authoritative at 36 | ✅ Verified — `NUM_LEDS=36` in `include/Config.h`, confirmed on real hardware boot log (`[HWTEST] LED count configured: 36`) |
| 2 | LED effects and AudioOverlay still operate correctly | ✅ Physically confirmed on battery power (2026-08-08) — multiple base effects and overlays cycled correctly |
| 3 | LED current limiting active in normal rendering | ✅ Verified — `applyPowerLimit()` unchanged in the normal render path, confirmed via host test and code review |
| 4 | Startup HWTEST uses the same power limiter, no unprotected full-white frame | ✅ Verified — physically confirmed on real hardware (`[POWER] Throttling: estimated 2196mA exceeds 1000mA limit, scaling by 0.46`) |
| 5 | ESP32 brownout reporting remains enabled | ✅ Verified — hardware brownout detector never disabled; `esp_reset_reason()` print added this milestone, purely additive |
| 6 | Brownout/reset-loop/solid-white incident documented | ✅ Done — `docs/current/POWER.md` sections A/B |
| 7 | Battery-pack power has physically operated Sunny under previously problematic LED+MusicMotor load without brownouts | ✅ Physically observed (reported by the user, documented in `docs/current/POWER.md` section H) |
| 8 | Computer-USB source power documented as leading hypothesis, not formally closed | ✅ Done — explicit wording in `docs/current/POWER.md` section H |
| 9 | MusicMotor remains operational, not artificially crippled | ✅ Verified — no change to `MusicMotorController`'s M80 floor, speed tables, or decision logic this milestone |
| 10 | Speaker output physically usable | ✅ Physically observed in earlier V1.1 work (35-100% usable range, 50-100% clear tones) — see `docs/current/SPEAKER.md` |
| 11 | Remaining low-frequency buzz documented as unresolved/prototype-sensitive | ✅ Done — see `docs/current/SPEAKER.md`, not marked fixed anywhere |
| 12 | Digital speaker silence/carrier tests remain clean | ✅ Physically confirmed on battery power (2026-08-08) — `silencecheck`/`carriercheck` both clean |
| 13 | MAX98357A/mic shared-I2S architecture intact | ✅ Verified — `SharedI2S.cpp` unchanged this milestone except an APLL-investigation comment (no behavior change) |
| 14 | Buttons and LED visual cues remain functional | ✅ Physically confirmed on battery power (2026-08-08) — all four buttons + green/red Audio Mode cues |
| 15 | 330Ω resistor documented as current hardware practice | ✅ Done — `docs/current/ELECTRICAL.md`, `docs/current/POWER.md` |
| 16 | 5V bulk-capacitor and prototype-ground findings documented without overclaiming | ✅ Done — `docs/current/POWER.md` sections E/G |
| 17 | Complete host suite passes | ✅ Verified — 20/20 files, 0 warnings, 0 failures |
| 18 | Firmware builds cleanly | ✅ Verified — clean build, SUCCESS, 0 project-source warnings |
| 19 | No unrelated DBCODE project changed | ✅ Verified — `git status` confirms all changes scoped to `sunflower-esp32-s3` (plus the shared `docs/AI_HANDOFF.md`) |
| 20 | V1.2 roadmap begins with touchscreen/display + UI | ✅ Done — `ROADMAP.md` updated |

**All 20 criteria are now ✅.** 16 were software/documentation-verified
or previously physically observed; 4 (#2, #12, #14, and the combined-
system aspect of #7/#9) required the physical validation pass and were
confirmed on 2026-08-08 (see "Physical validation result" above).

## Physical validation checklist (for reference / future re-validation)

The exact checklist run above lives in `docs/current/TESTING.md`'s
"V1.1 closure validation" section — kept there (not duplicated here)
for future re-validation after any change that touches boot, buttons,
LEDs, mic, motor, or speaker behavior. Covers: boot, buttons, LEDs,
mic, motor, speaker, and combined-system checks, all on **battery
power**. Computer-USB full-load testing is explicitly **not** required
to pass V1.1 — see `docs/current/POWER.md` for why.
