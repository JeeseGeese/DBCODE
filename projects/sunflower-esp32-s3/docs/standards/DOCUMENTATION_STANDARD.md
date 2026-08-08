# Standard — Documentation

Prescriptive rule for this project, largely inherited from and
compatible with `/AGENTS.md`'s repository-wide documentation
standards.

## Rule

1. **Distinguish living from frozen documentation explicitly.**
   `docs/current/` always reflects the active development state and is
   the file to edit. `docs/V1/` (and any future `docs/V1.1/`, etc.) is
   a frozen snapshot, never edited after its baseline is captured —
   only referenced.
2. **Never claim physical validation without hardware evidence.** Use
   the three-tier language explicitly: host-validated /
   software-validated / physically-validated (see
   `docs/standards/TESTING_STANDARD.md`). "Works" alone is not an
   acceptable claim.
3. **Archive historical material, never delete it.** A superseded or
   historical document gets a pointer to its new location and to the
   current source of truth — its own content is preserved exactly as
   originally written, not rewritten to match current reality.
4. **State uncertainty explicitly.** A hypothesis that hasn't been
   isolated with a control condition is a hypothesis, not a root
   cause — say so in those words (see
   `docs/lessons/audio-buzz-noise-diagnosis.md` for a worked example).
5. **Every generalizable finding becomes a lesson or a standard**, not
   just a line buried in a bring-up log — see
   `docs/lessons/README.md`-equivalent framing in `docs/lessons/*.md`'s
   own format (Problem/Root cause/Verification/Correct approach/
   Failure modes/Applies elsewhere?/Related files).
6. **Keep `CURRENT_STATUS.md` short.** It is the fastest starting
   point for a new session, not a history dump — link out to
   `docs/current/`/`docs/architecture/` for detail rather than
   inlining it.
7. **New documentation cross-references use relative paths** and are
   spot-checked to resolve after any file move.

## Rationale

See `/AGENTS.md` sections 3 and 6 (this project's parent repository
rules) — this standard is a Sunny-specific restatement/extension of
those, not a replacement.
