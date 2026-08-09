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

**Session date:** 2026-08-08 (final V1.1 validation/checkpoint-prep
pass — the fourth and last V1.1 session today; see
`docs/current/V1_1_STATUS.md` for the full accomplishment list across
all four)

**Current objective (this pass, updated):** Physical validation was
run by the user on battery power and **approved** — see
`docs/current/V1_1_STATUS.md`'s "Physical validation result" section
for exactly what was confirmed (boot/buttons/LEDs/overlay/mic/motor/
speaker/combined-system, no brownout). **Sunny V1.1 is now COMPLETE.**
Status docs (`V1_1_STATUS.md`, `CURRENT_STATUS.md`, `OVERVIEW.md`,
`ROADMAP.md`) have been updated accordingly.

**Checkpoint commit/tag/push status: IN PROGRESS, not finished as of
this entry.** Before staging, a working-tree audit found
`platformio.ini` modified **outside any V1.1 session** — it now
includes `lvgl/lvgl@^9.5.0` in `lib_deps` (a V1.2 touchscreen-UI
library) and has had its explanatory comments stripped/reformatted
(consistent with a PlatformIO IDE auto-format, not a manual edit by any
AI session this project has record of). This is flagged as an
ambiguous/out-of-scope file for the V1.1 commit and excluded pending
the user's decision — **do not assume it's been resolved one way or
the other; check `git status`/`git diff platformio.ini` and this file's
own latest state before continuing the checkpoint.**

**Working tree audit this pass:** all uncommitted changes classified —
speaker/audio refinement, 36-LED correction, HWTEST power-safety fix,
power/brownout documentation, tests, and context/handoff/roadmap docs.
**No ambiguous or unrelated changes found** — the one untracked file
outside this classification (`.gitignore`) predates all four sessions
and is an ordinary `.pio`/`.vscode` ignore file, not V1.1 work.

**Roadmap re-sequenced this pass:** V1.2 is now **touchscreen/UI**
(previously undefined at this slot); Raspberry Pi moved to V1.3; camera
to V1.4; LLM/voice to V1.5. See `ROADMAP.md`. `docs/CLAUDE_CONTEXT_GUIDE.md`
gained a "Touchscreen/UI work (V1.2)" minimal-read-set section so the
next session doesn't need to load V1.1's debugging history to start UI
work.

**Current branch:** `feature/expressive-motion-v1` (unchanged).

**Working tree status:** NOT committed — accumulated uncommitted
changes across all four of today's V1.1 sessions (speaker sprint + LED
correction + power/brownout documentation + this validation pass). Run
`git status`/`git diff` before assuming what's in the tree.
**Do not commit/tag/push without the user's explicit approval of the
physical validation results** — that gate has not been passed yet as
of this entry.

**Tests/build performed this pass:**
- Full host suite: **20/20 `test_host/*.cpp` files present, compiled,
  and passing, 0 warnings** (verified fresh this pass, not assumed from
  an earlier session).
- Clean build (`pio run -t clean && pio run`): **SUCCESS**, 0
  project-source warnings (only the pre-existing, documented
  `ARDUINO_USB_MODE redefined` framework notices), RAM 7.0%
  (23048/327680 bytes), Flash 7.0% (456341/6553600 bytes).
- No upload was performed this pass (no source changed, nothing new to
  flash) — the currently-flashed firmware is from the LED-count
  session earlier today.

**Hardware testing performed this pass: none** — this was a
verification/documentation pass, not a physical-hardware session. See
`docs/current/V1_1_STATUS.md`'s exit-criteria table for exactly which
items are already physically verified (from earlier sessions) vs.
still need the checklist run.

**Outstanding issues:**

- The V1.1 physical validation checklist has not been run yet — this
  is the actual remaining gate, not a code or documentation gap.
- Residual speaker buzz/static remains unresolved and unproven (accepted
  V1.1 limitation, not a blocker — see `docs/current/V1_1_STATUS.md`).
- Electrical brownout root cause remains not formally closed (accepted
  V1.1 limitation, not a blocker — same doc).

**Recommended next task:** Run the physical validation checklist
(`docs/current/TESTING.md`) on battery power and report results back,
per-step, not as a single pass/fail. On approval: V1.1 checkpoint
commit/tag/push, then V1.2 touchscreen/UI work can begin (hardware
selection first — see `ROADMAP.md`'s V1.2 section).

**Commands to resume:**

```bash
cd ~/DOBETTERCODE/DBCODE
git status                     # working tree has accumulated uncommitted changes
cd projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run                        # confirm it still builds
cd test_host && for f in *.cpp; do n="${f%.cpp}"; g++ -std=c++17 -Wall -Wextra -o "/tmp/$n" "$f" && "/tmp/$n"; done
pio device list                # confirm port before any upload -- don't assume it's still whatever it was last session
```

**Known risks:**

- The motor (DRV8833), LED strip, and MAX98357A amplifier share one 5V
  rail; only the INMP441 microphone runs from 3.3V. The new, louder
  default speaker volume (70% vs. the old 5% bring-up default) draws
  meaningfully more amplifier current than before.
- No current-sensing or thermal-monitoring hardware exists on this
  project.
- `speaker isolate on` commands the motor stopped once but does not
  prevent an independently-active motor behavior from re-engaging it —
  not a safety interlock.
- The prototype is still solderless breadboard/Dupont wiring — see
  `docs/lessons/breadboard-prototype-transient-load-risk.md`.

**Anything the next AI absolutely must know:**

1. This repository (and the sunflower project specifically) is **not
   complete** — V1.1 is "ready for validation," not "complete," until
   the user runs the physical checklist and approves. Do not write or
   imply V1.1 is done before that happens.
2. **Do not commit, tag, or push** until the user explicitly approves
   the physical validation results — this is an explicit gate from the
   user's own instructions this session, not a general caution.
3. Read `/AGENTS.md` section 6 (physical validation policy) and
   `docs/current/V1_1_STATUS.md`'s exit-criteria table before writing
   any status claim about V1.1 — it already distinguishes
   verified/physically-observed/accepted-limitation per criterion;
   don't collapse that back into a single "done" claim.
4. V1.2 is now **touchscreen/UI**, not Raspberry Pi — if a future
   session's instructions still say "V1.2 = Raspberry Pi," the roadmap
   has since been corrected; trust `ROADMAP.md`/`CURRENT_STATUS.md`
   over a stale instruction.
5. Nothing was committed, pushed, or tagged today (any of the four
   sessions). The working tree has real uncommitted changes as of this
   entry.
