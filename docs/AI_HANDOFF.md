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

**Session date:** 2026-07-30 (checkpoint pass, same calendar day as the
infrastructure session below)

**Current objective:** Resolve the previously-flagged uncommitted
working-tree gap by committing the existing implementation and test work
as one honest engineering checkpoint, then commit the AI infrastructure
separately. This was a git-history/documentation task, not new firmware
development — no source file's logic was modified to make this checkpoint
happen; the code was committed as-is.

**Current branch:** `feature/expressive-motion-v1`

**Current commit (engineering checkpoint):** `64e8aee` — "feat: checkpoint
Sunny motor and audio development through MusicMotor Rev 10.1"
(2026-07-30). This is followed by a second, documentation-only commit
(see below) that this file itself is part of.

**The working-tree gap flagged in the previous entry is now resolved.**
Everything previously listed as uncommitted — `MotorPwmCalibration`,
`DanceEngine`, `SharedI2S`, `SpeakerTest`, `MusicMotorController` through
Revision 10.1, `HardwareTest`, `MicRetest`, and all 12
`test_host/music_motor_*.cpp` regression files, plus the `MotorDriver` PWM
primitives, the `AudioAnalyzer` migration to `SharedI2S`, and the
associated `Config.h`/`Controls.cpp`/`main.cpp`/`.gitignore` changes — is
now committed at `64e8aee`. `git log`/`git show` for this project can now
be trusted for this content; the previous entry's warning to distrust
`git log` no longer applies as of this commit.

**Committed as one checkpoint, not reconstructed feature-by-feature.**
None of the above was actually committed incrementally as it was built —
there was no real per-feature commit history to recover. A prior session
proposed a 7-commit feature-by-feature split; the user explicitly chose a
two-commit grouping instead (implementation+tests, then documentation) to
preserve truthful checkpoint history rather than manufacture history that
never existed. See `64e8aee`'s commit message for the full rationale and
contents list.

**Two temporary, self-labeled diagnostics remain intentionally present**
in the committed code (not removed, not hidden):

- `AudioAnalyzer.cpp`'s boot-time I2S RX trace, bounded to `Config.h`'s
  `MIC_RX_TRACE_CALL_COUNT = 5` calls — proves the `MIC_I2S_SLOT_INDEX`
  choice against real captured hardware data.
- `SpeakerTest`'s `'loud'` command — explicitly labeled "TEMPORARY
  DIAGNOSTIC ONLY" at its definition site.

Also still present and unchanged: `HardwareTest` and `MicRetest`
(each documents its own full removal instructions in its own header) and
the ~30-40s blocking boot sequence they cause — see
`projects/sunflower-esp32-s3/AGENTS.md` section 12.

**Files touched this checkpoint pass:**

- Commit `64e8aee`: all 34 files listed in that commit's own message
  (implementation + `test_host/`) — no new content authored this pass,
  the files were already present in the working tree from prior sessions
  and were committed as-is.
- This documentation commit (in progress as this entry is written):
  `README.md` (unmodified content, just newly committed),
  `/AGENTS.md`, `/CLAUDE.md`, `/docs/AI_HANDOFF.md` (this file, edited),
  `/projects/sunflower-esp32-s3/AGENTS.md`,
  `/projects/sunflower-esp32-s3/CURRENT_STATUS.md` (edited),
  `/projects/sunflower-esp32-s3/ROADMAP.md` (edited).

**Tests performed tonight, exact results:**

- Full `test_host/` suite, all 12 files, `g++ -std=c++17 -Wall -Wextra`:
  **12/12 passed, 0 compiler warnings, 0 runtime failures.**
- Clean PlatformIO build (`pio run -t clean && pio run`,
  `env:esp32-s3-devkitc-1`): **SUCCESS**, 0 project-source warnings (only
  the pre-existing, already-documented `ARDUINO_USB_MODE redefined`
  framework notices), RAM 7.2% (23432/327680 bytes), Flash 6.8%
  (445017/6553600 bytes).

**Hardware testing performed: none.** No hardware was connected or
operated during this checkpoint pass. This commit and this entry make no
physical-validation claim beyond what was already physically observed and
documented in prior sessions — see `README.md` and
`docs/DRV8833_MOTOR_BRINGUP.md` for that existing history, which is
unchanged by tonight's work.

**Outstanding issues:**

- None newly introduced by this checkpoint. The working-tree/git-history
  gap that was the top outstanding issue in the previous entry is
  resolved as of `64e8aee`.
- This documentation set still has not been used by a second AI session
  to confirm it's sufficient for cold-start orientation in practice —
  carried forward from the previous entry, still open.

**Recommended next task:** **Speaker debugging** — physical audio-quality
validation of the MAX98357A output (volume, clarity) beyond the current
write-path-only verification. This is the explicitly stated next planned
objective as of this checkpoint. See
`projects/sunflower-esp32-s3/CURRENT_STATUS.md` and `ROADMAP.md` for the
fuller priority list (physical validation of `ExpressiveMotion`/
`BehaviorEngine` remains open too, and isn't superseded by the speaker
work — both are legitimate next steps, speaker debugging is simply what
was named for immediately next).

**Commands to resume:**

```bash
cd ~/DOBETTERCODE/DBCODE
git status                     # should be clean as of this checkpoint
git log --oneline --decorate -5
cd projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run                        # confirm it still builds
cd test_host && for f in *.cpp; do n="${f%.cpp}"; g++ -std=c++17 -Wall -Wextra -o "/tmp/$n" "$f" && "/tmp/$n"; done
```

See `projects/sunflower-esp32-s3/AGENTS.md` for upload/monitor/test
commands, and its "Speaker hardware test" cross-reference (README.md) for
the required MAX98357A startup safety sequence before beginning speaker
debugging on real hardware.

**Known risks:**

- The motor (DRV8833), LED strip, and (in development) the MAX98357A
  amplifier currently share one 5V rail (corrected 2026-08-01 — this
  bullet previously and incorrectly said the ESP32's 3.3V rail; only
  the INMP441 microphone runs from 3.3V), with known,
  physically-observed motor/LED contention — see that project's
  `AGENTS.md` for the electrical constraints. Any firmware change that
  increases simultaneous motor+LED+amplifier current draw should be
  treated as physically risky until re-validated.
- No current-sensing or thermal-monitoring hardware exists on this
  project — over-current, stall, and thermal conditions cannot be
  detected in software, only avoided by conservative timing/duty limits.
- Speaker debugging (the next planned objective) involves an amplifier
  and a physical speaker for the first time — read the full startup
  safety sequence in `README.md`'s "Speaker hardware test" section
  (keep MAX98357A `SD` at GND until `[SPEAKER] Digital silence active`
  prints) before connecting anything.

**Anything the next AI absolutely must know:**

1. This repository (and the sunflower project specifically) is **not
   complete** — do not write or imply otherwise anywhere, including in
   casual chat responses to the user. Tonight's work was a checkpoint,
   not a release.
2. Read `/AGENTS.md` section 6 (physical validation policy) before
   writing any status claim about hardware behavior. It is the most
   commonly violated rule in embedded-project documentation generally,
   and this repository is explicit about not tolerating it. This
   checkpoint pass involved zero hardware — do not let the large commit
   size imply otherwise.
3. `git log`/`git show` are trustworthy again for this project as of
   `64e8aee` — the previous entry's warning about the working tree not
   matching history no longer applies. Verify with `git status`/`git log`
   before assuming this is still true, as always.
4. Nothing has been pushed, tagged-and-pushed, or uploaded to hardware.
   Everything from this checkpoint pass is local only unless a later
   session/entry says otherwise.
