# AGENTS.md — DBCODE Repository Instructions

This file is permanent repository instruction for any AI assistant working
in this repository — Claude Code, ChatGPT/Codex, or any future assistant.
It does not expire and is not tied to any single conversation. If you are
an AI assistant starting a new session here, read this file first, then
read `docs/AI_HANDOFF.md` for the current session's specific state, then
read the relevant `projects/<name>/AGENTS.md` for the project you're
touching.

This document governs *how* to work in this repository. It intentionally
does not describe the current state of any project — project state lives
in each project's own `CURRENT_STATUS.md` and `ROADMAP.md`, because it
changes far more often than these rules do.

## 1. Repository philosophy

DBCODE is a general embedded/firmware/software workspace (ESP32, ESP8266,
Raspberry Pi Pico, Arduino, Raspberry Pi, Python utilities, desktop
software, CLI tools, shared libraries, experimental projects, future
robotics firmware) under active, incremental, hands-on development. Every
project in `projects/` is real hardware or real software being built by a
single engineer collaborating with AI assistants across many sessions.

Consequences of that for how you should work:

- **Nothing here is a demo or a toy.** Firmware changes drive real motors,
  real LED strips, and real speakers on real desks. Treat electrical and
  mechanical safety as seriously as you'd treat production data-loss risk
  in a backend system.
- **No project in this repository should be assumed complete**, no matter
  how thorough its documentation reads. Documentation here is written to
  be precise and historically accurate about *what has been tried and
  observed*, not to project confidence about a finished product. If a
  project's docs read as "done," treat that as a documentation smell, not
  a reason to stop asking what's still open — check its `CURRENT_STATUS.md`
  and `ROADMAP.md` for what's actually still active.
- **Continuity across sessions matters more than elegance in the moment.**
  A future AI assistant — possibly a different model, possibly you months
  later — needs to be able to pick up mid-task from documentation alone,
  without replaying this conversation. Optimize for that reader.

## 2. Coding standards

- Match the existing style of the file/module you're editing before
  reaching for a personal preference. This codebase has established,
  deliberate conventions (see each project's `AGENTS.md` for specifics) —
  consistency with neighboring code beats an abstractly "better" pattern.
- Prefer small, targeted changes over broad rewrites. If a change reveals
  that a broader refactor is warranted, document the recommendation rather
  than silently expanding scope.
- Do not introduce a new abstraction, library, or architectural pattern
  to solve a problem that already has an established solution elsewhere
  in the same project (see "file organization expectations" below on
  checking for prior art first).
- On embedded/firmware projects specifically: never assume a hardware
  behavior, timing constant, or pin capability — verify against the
  installed framework/SDK source, datasheet, or direct measurement, and
  say so in the commit/doc when you do. Guessed hardware facts documented
  as verified facts are a direct source of wasted debugging time later.
- No dead code left "just in case." If something is superseded, remove it
  and note *why* in the commit message and/or relevant doc — git history
  is the place for old versions, not commented-out blocks.

## 3. Documentation standards

- Documentation in this repository is a **record of engineering reality**,
  not marketing copy. State what has been verified, what hasn't, what
  failed, and what's still a hypothesis — explicitly, using those words.
- Every claim of "working," "validated," or "fixed" must say *how* it was
  validated (host test, serial log, physical observation) and *when*
  relevant. An unqualified "works" is not acceptable in this repository —
  see the physical validation policy below.
- Preserve engineering rationale — the *why*, not just the *what*. A
  reader who only sees the current state of the code cannot recover why a
  design choice was made, why an alternative was rejected, or what
  constraint forced a particular workaround. When you make a non-obvious
  decision, write down the reasoning where a future reader will find it
  (a doc section, a commit message — not just this conversation).
- **Update documentation whenever behavior changes.** A doc describing
  behavior that no longer matches the code is worse than no doc at all,
  because it actively misleads the next reader (human or AI). If you
  change what a module does, update the doc that describes it in the same
  change — don't defer it to "later."
- Do not delete documented failure history to make a project look more
  finished. A record of "we tried X, it failed because Y" is exactly the
  kind of information that keeps a future assistant from re-trying X.
- When a document's scope grows large enough that finding information in
  it is itself hard, that's a signal to split it — but don't do so
  speculatively; wait until it's actually a problem.

## 4. Git safety rules

- **Do not perform destructive git operations without explicit
  permission**, in this session, for this specific action. This
  includes: `git push --force` (especially to a shared/default branch),
  `git reset --hard`, `git checkout -- <path>` or `git restore` that would
  discard uncommitted work, `git clean -f`/`-fd`, deleting branches or
  tags, rewriting published history (`rebase -i`, `commit --amend` on
  anything already pushed/shared), and `git tag -d`/`git push --delete`.
  A prior approval for one destructive action is not standing approval
  for future ones — ask again each time.
- Before any command that could discard uncommitted work, run `git
  status` first (and `git stash -u` or commit first if it would otherwise
  be lost). Never assume a clean working tree.
- Prefer creating a new commit over amending, unless the user explicitly
  asks for an amend and the target commit has not been pushed/shared.
- Never skip hooks (`--no-verify`) or bypass signing unless explicitly
  instructed.
- Tags in this repository (e.g. `v1.0.0` in `sunflower-esp32-s3`) mark
  physically-validated milestones. Do not move, delete, or reuse an
  existing tag. A new milestone gets a new tag.
- When staging changes, add specific files by name rather than `git add
  -A`/`git add .` — this repository mixes source, build artifacts, and
  documentation, and blanket staging risks pulling in something that
  shouldn't be committed (secrets, local config, stray build output not
  yet covered by `.gitignore`).

## 5. Testing philosophy

This repository draws a hard, explicit line between three different kinds
of "it works," and documentation must always be clear about which one is
being claimed:

1. **Host-validated** — a deterministic, host-compiled test (see each
   project's `test_host/` or equivalent) passed. This proves the pure
   decision logic is correct in isolation. It proves nothing about
   hardware.
2. **Software-validated** — the firmware builds, uploads, and runs
   without crashing, and serial logs show the expected internal state
   transitions. This proves the code runs on the target MCU. It does
   *not* prove the physical, real-world behavior (movement, sound,
   light) is correct, safe, or matches intent.
3. **Physically validated** — a human has directly observed the real
   hardware behave correctly (motor moved as expected, LEDs displayed the
   right pattern, speaker produced clean audio at a safe volume, etc.)
   and that observation is recorded in the docs.

These are not a strict ladder that always must be climbed in order, but
they are never interchangeable, and **advancing from one level to the next
in documentation requires the evidence for that level, not the previous
one.**

- Where a host-test harness exists for a module's pure logic, prefer
  adding/extending host tests over hand-verifying logic by re-reading
  code. They're fast, deterministic, and catch regressions this
  conversation won't be present to catch later.
- Regressions found and fixed should get a host test that would have
  caught them, where the logic is host-testable at all (this repository
  has direct historical examples of exactly this pattern — bugs found
  during physical or serial validation, then permanently covered by a new
  host test file).

## 6. Physical validation policy

**Never claim physical validation without hardware evidence.** This is
the single most important rule in this document.

- Do not write "physically validated," "confirmed working on hardware,"
  "tested and passes," or any equivalent phrase in documentation, commit
  messages, or status reports unless a human has actually operated the
  physical device and reported the observed result to you in this
  conversation (or a prior one, cited).
- If you have only built, uploaded, or read code, say exactly that: "not
  yet physically validated," "builds cleanly, physical behavior unknown,"
  "software-validated only." These are not weaker ways of saying
  "working" — they are different, true claims, and the honest one is
  always required.
- If a physical test was performed but only partially — some cases
  observed, others not — document precisely which cases were covered and
  which weren't. Do not round a partial pass up to a full one.
- When in doubt about whether a claim in existing documentation was ever
  actually physically verified, say so rather than silently treating it
  as verified because it reads confidently.

## 7. Host testing policy

- Host-side tests exist specifically because this repository's firmware
  often has no on-target (PlatformIO `test` env) test runner set up, and
  pure decision logic (state machines, band/threshold classification,
  timing arithmetic) can be exercised without hardware by mirroring the
  relevant constants/enums/functions into a standalone host-compiled
  program. Follow this existing pattern for new host-testable logic
  rather than inventing a different test framework.
- Host tests validate logic in isolation, deliberately without Arduino/
  ESP-IDF dependencies, so they build fast and run anywhere. They are not
  a substitute for physical validation of anything that depends on real
  timing, real electrical behavior, or real mechanical response — say so
  explicitly when documenting a host-tested feature's status.
- All host tests in a project must pass, cleanly (including zero compiler
  warnings, where that's the project's stated bar), before a change in
  the area they cover is considered done. If a project's docs specify an
  exact pass criterion (e.g. "PASS: 0 failure(s)"), meet it exactly, not
  approximately.

## 8. Engineering reporting expectations

- Report what you actually did, not what you intended to do. If a build
  didn't run, a test wasn't executed, or a claim couldn't be verified in
  this session, say so plainly rather than letting confident phrasing
  imply otherwise.
- Distinguish facts you directly observed (build output, test output, a
  human's report of physical behavior) from inferences and hypotheses you
  are drawing from them. This repository's existing docs are written this
  way on purpose (see `DRV8833_MOTOR_BRINGUP.md`'s explicit "measured
  facts vs. hypotheses" sections) — follow that pattern.
- When you fix a bug, record the root cause you actually found, not a
  plausible-sounding guess. If the true root cause is genuinely unknown,
  say that, and say what remains a hypothesis rather than a confirmed
  cause.
- Flag risk and uncertainty proactively, especially around anything
  touching motors, power, or other physically hazardous subsystems — do
  not wait to be asked "is this safe?"

## 9. File organization expectations

- Before adding a new file/module, check whether an existing module
  already owns the relevant responsibility (see each project's `AGENTS.md`
  for subsystem/ownership tables). This repository has a strong,
  consistently-applied convention of single-owner modules (e.g. one
  module is the sole owner of a given set of GPIO pins, or of a shared
  peripheral) — do not create a second owner of something that already
  has one.
- Match each project's existing directory layout (`include/`, `src/`,
  `test_host/` or equivalent) rather than introducing a new one.
- Cross-project documentation belongs in `docs/`; project-specific
  documentation belongs inside that project's own directory. Don't
  duplicate the same information in both places — link instead.
- Temporary bring-up/diagnostic tooling should say so in its own header
  comment (what it's for, and ideally how to fully remove it later) so a
  future reader doesn't mistake it for permanent architecture — see
  section 10 below.

## 10. Diagnostics vs. production architecture

- **Never silently remove diagnostics.** If a diagnostic, test command,
  or debug-only code path is being removed, say so explicitly in the
  commit message and update any documentation that references it. Silent
  removal breaks the next person's ability to reproduce a past
  investigation.
- **Distinguish temporary diagnostics from production architecture**,
  explicitly, in both code and docs. This repository's existing
  convention: temporary/bench-development modules are labeled as such in
  their own doc sections and header comments (e.g. "temporary
  bench-development workaround," "diagnostic only, not a production
  fix"), and known-temporary constants/modes are called out as not yet
  approved for production use. Follow this pattern for new temporary work
  rather than letting a diagnostic quietly become load-bearing.
- When a diagnostic uncovers a real bug, preserve the diagnostic's value
  as a historical record (git history and/or a doc section) even after
  the diagnostic tool itself is removed — see `DRV8833_MOTOR_BRINGUP.md`
  section 12 for the established pattern (a diagnostic module's final
  state committed for history immediately before removal, with the exact
  `git show`/`git checkout` commands recorded to retrieve it later).

## 11. How this file relates to other documentation

```
AGENTS.md (this file)              — permanent rules, all projects
docs/AI_HANDOFF.md                 — reusable handoff template, updated every session
projects/<name>/AGENTS.md          — project-specific technical instructions
projects/<name>/CURRENT_STATUS.md  — current snapshot, updated often
projects/<name>/ROADMAP.md         — living roadmap, updated often
docs/*.md                          — cross-project or deep-dive technical documentation
```

If you find a contradiction between this file and a project-specific
`AGENTS.md`, the project-specific file wins for that project's own
technical details (pin assignments, module ownership, build commands),
but the safety and process rules in this file (git safety, physical
validation policy, diagnostics-vs-production) always apply repository-wide
and cannot be overridden by a project file.
