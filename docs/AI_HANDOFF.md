# AI Handoff

A living handoff note, updated at the end of (or during) every AI-assisted
work session in this repository. Its job is to let the *next* session —
whether that's the same assistant tomorrow, a different AI product
entirely, or a human reading it cold — pick up work without needing this
conversation's history.

**This file is a template with the last real session's content filled in
below.** Whichever AI assistant is ending a work session should update the
"Current entry" section with what actually happened. Do not leave stale
information in place — overwrite fields that no longer apply, and prefer
a short accurate entry over a long speculative one.

This file is intentionally generic — it does not assume Claude, GPT, or
any particular assistant is doing the work. If something below only makes
sense for a specific project, prefer putting the detail in that project's
own `CURRENT_STATUS.md` and just linking to it here.

See also: `/AGENTS.md` (permanent repository rules) and each project's own
`AGENTS.md` (technical detail) — read both before acting on anything in
this file.

---

## How to fill this out (read before editing)

Replace every field below with the real current state before you stop
work. If a field doesn't apply this session, write "N/A" rather than
deleting the field — a missing field reads as "forgotten," not
"intentionally empty."

- **Current objective** — one or two sentences, plain language, no jargon
  a cold reader wouldn't have.
- **Current branch / Current commit** — exact `git branch --show-current`
  and `git log -1 --format='%H %s'` output, not a paraphrase.
- **Files modified** — the actual file list (`git status --short` is
  fine to paste), plus one line on *why*, if it's not obvious from the
  filenames.
- **Tests performed** — name the actual test(s) run and their actual
  result. "Ran the host test suite, 12/12 passed" not "tests look good."
- **Hardware testing performed** — be explicit if this is "none this
  session." Never imply hardware was touched if it wasn't. See the
  physical validation policy in `/AGENTS.md` — it applies here too.
- **Outstanding issues** — anything broken, half-finished, or known-wrong
  right now.
- **Recommended next task** — the single most useful next step, from your
  perspective ending the session, not a wishlist.
- **Commands to resume** — the literal shell commands the next session
  needs to get back to where you left off (build, upload, monitor, test).
- **Known risks** — anything that could hurt hardware, lose data, or
  mislead the next reader if not flagged explicitly.
- **Anything the next AI absolutely must know** — the one or two things
  that would cause real wasted time or a real mistake if missed. Keep
  this short; if everything is "critical," nothing is.

---

## Current entry

**Session date:** 2026-08-16 — Sunny V1.2 Beta 1 checkpoint session.
Verified, documented, and preserved the current working state of both
Sunny ESP32 projects (body + display) as a formal restore point before
UI development continues. Not a feature-development session.

**Current objective (this entry):** VERIFY → DOCUMENT → PRESERVE →
COMMIT → TAG → PUSH the current state as `sunny-v1.2-beta1`. No
refactoring, recalibration, or behavior changes made to either
project's firmware.

**What happened:**

1. Re-verified (not assumed) both projects' current state: body
   controller source is byte-identical to the `sunny-v1.1` tag; display
   controller source matches exactly what was last built/uploaded in
   the 2026-08-09 session (confirmed via matching RAM/Flash figures on
   a fresh build).
2. Confirmed with the user that the touch-validation screen's physical
   retest (5 targets + TAP TEST, exercising the corrected per-axis
   linear-fit calibration) — left pending at the end of the 2026-08-09
   session — has since been performed and **passed**. V1.2.2 is now
   COMPLETE.
3. Ran the full host-test suite and a clean build for both projects
   fresh this session (see "Tests/build performed" below).
4. Wrote `projects/sunny-display-esp32/docs/V1_2_BETA1_STATUS.md`, the
   canonical Beta 1 checkpoint record (purpose, both controllers'
   status, physically-verified evidence, software-verified evidence,
   current beta capabilities, known limitations).
5. Updated living docs (`CURRENT_STATUS.md`, `ROADMAP.md` in
   `sunflower-esp32-s3`; `README.md`, `docs/DISPLAY_HARDWARE.md` in
   `sunny-display-esp32`) to replace stale "physical retest pending"
   language with the confirmed-complete status, and to point at the
   new Beta 1 status doc. `docs/V1/` was not touched.
6. Committed the checkpoint, tagged `sunny-v1.2-beta1`, and pushed both
   the branch and tag — see the checkpoint commit for the exact scope.

**Full technical record of the V1.2.2 bug/fix (mapping-model error,
comparison of three calibration models, applied constants, the
premature "hitbox fixed it" conclusion and its correction) lives in
`sunny-display-esp32/docs/DISPLAY_HARDWARE.md`'s "Touch calibration
procedure" section — not duplicated here.**

**Tests/build performed this session (fresh, not assumed):**
- Body controller (`sunflower-esp32-s3`): 20/20 host tests pass, 0
  warnings. Clean build SUCCESS — RAM 7.0% (23048/327680), Flash 7.0%
  (456357/6553600), 0 project-source warnings. Source unchanged from
  `sunny-v1.1` — no upload performed (unnecessary).
- Display controller (`sunny-display-esp32`): 4/4 host test files pass
  (26 checks), 0 warnings. Clean build SUCCESS — RAM 34.0%
  (111452/327680), Flash 51.0% (668557/1310720), 0 warnings. No new
  upload performed — the already-flashed firmware (source-identical) is
  what the user physically retested.

**Hardware testing performed this session:** none directly (no new
upload) — this was a verification/documentation/checkpoint session. The
physical retest referenced above was performed by the user; see item 2.

**Outstanding issues (unchanged, carried forward as Beta 1 known
limitations — full list in `V1_2_BETA1_STATUS.md`):**
- Body↔display communication transport is unresolved — the display
  board's near-total lack of free GPIOs (effectively one bidirectional
  pin) is a real constraint, not yet designed around.
- The six placeholder screens (HOME/AUDIO/MOTION/LEDS/DIAGNOSTICS/
  SETTINGS) have no real content — enum values and dispatcher routing
  only.
- Flash partition scheme on the display project (`esp32dev` defaults)
  hasn't been reconsidered against the confirmed 4MB flash.
- Body-controller brownout root cause remains a strong hypothesis
  (computer-USB vs. battery power), not formally isolated. Residual
  speaker buzz remains unresolved. Neither is a Beta 1 blocker — both
  are pre-existing, documented `sunny-v1.1` limitations.

**Recommended next task:** V1.2.3 — Sunny UI foundation. See
`sunny-display-esp32/docs/DISPLAY_HARDWARE.md`'s "Later Sunny UI
architecture" section for the planned screen list and the
navigation-infrastructure-only groundwork already in place
(`isScreenImplemented()`).

**Commands to resume:**

```bash
cd ~/DOBETTERCODE/DBCODE
git status                     # should be clean after this session's checkpoint
git log --oneline --decorate -5
git tag --list                 # sunny-v1.2-beta1 should be present

cd projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run                        # body controller
cd test_host && for f in *.cpp; do n="${f%.cpp}"; g++ -std=c++17 -Wall -Wextra -o "/tmp/$n" "$f" && "/tmp/$n"; done

cd ../../sunny-display-esp32
pio run                        # display controller
cd test_host && for f in *.cpp; do n="${f%.cpp}"; g++ -std=c++17 -Wall -Wextra -o "/tmp/$n" "$f" && "/tmp/$n"; done

pio device list                # confirm port before any upload -- both boards may be connected simultaneously
```

**Known risks:**

- Uploading to the wrong board is a real risk when both ESP32s are
  connected simultaneously — always confirm the exact port/VID:PID
  before any upload (body controller: WCH CH343, `1A86:55D3`; display
  controller: WCH CH340, `1A86:7523`).
- The display board's near-total lack of free GPIOs means almost any
  future peripheral/feature decision on it needs to check
  `DISPLAY_HARDWARE.md`'s GPIO audit first.

**Anything the next AI absolutely must know:**

1. Sunny V1.2 is a **two-controller architecture** — the display has its
   own ESP32; the body ESP32-S3 will never drive it directly.
2. `sunny-display-esp32/docs/DISPLAY_HARDWARE.md` is the one file that
   answers almost every hardware/architecture question about the UI
   controller. `sunny-display-esp32/docs/V1_2_BETA1_STATUS.md` is the
   checkpoint record.
3. **Software-clean is not the same as physically-correct** — this
   project hit that repeatedly during V1.2.2 (rotation=1's silent
   upside-down render, a coordinate-mapping bug that got misdiagnosed
   as a hitbox-size issue because "it activates now" was mistaken for
   "it's accurate now"). Always get a human's physical confirmation
   before calling anything visually/positionally validated.
4. Touch calibration (`TouchManager.cpp`'s `CAL_SWAP_AXES`/`CAL_SCALE_X`/
   `CAL_OFFSET_X`/`CAL_SCALE_Y`/`CAL_OFFSET_Y`) is a **fitted linear
   model**, not a raw-min/max model — there is no more
   `rawXMin`/`rawXMax`/`invertX`/`invertY` anywhere in this project;
   don't reintroduce that shape.
5. The calibration screen (`CalibrationManager.cpp`) is available via
   `showScreen(SunnyUIScreen::CALIBRATION)` as a diagnostic/manual mode
   for future recalibration — it never self-applies its output.
6. The `lv_conf.h` widget-disable lesson (don't hand-disable individual
   LVGL widgets without checking their internal dependency graph) is a
   real, hard-won finding — don't reintroduce that bug when extending
   the UI later.
7. `sunny-v1.2-beta1` is Beta 1, not V1.2 final — V1.2.3 through V1.2.8
   are not started. Treat it as a restore point, not a finish line.
