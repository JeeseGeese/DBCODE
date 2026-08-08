# Playbook: Safe Firmware Refactor / Cleanup

For reorganizing documentation, archiving historical material, or
restructuring an embedded project **without** intentionally changing
runtime behavior — the exact kind of pass this playbook was extracted
from (see Sunny's own V1 documentation baseline work).

## Before starting

1. Full read of the areas being touched — do not assume existing
   documentation is current; verify against source (see
   `docs/CLAUDE_CONTEXT_GUIDE.md`'s per-task read sets for what
   "relevant" means for a given change).
2. Establish a classification for every file being considered: active
   source, canonical current doc, historical/archive candidate,
   duplicate/superseded, or needs-review. Do not move anything before
   this classification exists.
3. Confirm current build + test baseline (build succeeds, all host
   tests pass) **before** making any change, so a later failure can be
   attributed correctly.

## While reorganizing

- **Archive, never delete**, historical material — preserve filenames
  where helpful, add a pointer explaining what's now current instead.
- Never rewrite a historical document's *content* to match current
  reality — it's a record of what was true/believed at the time it was
  written. Add a note pointing to the current source of truth instead.
- Fix a stale fact only when it's unambiguous (verified against
  current source) and low-risk (a comment or a doc line, not behavior)
  — and say so explicitly when you do, rather than silently
  "correcting" history.
- Keep active, still-evolving subsystems (anything still under real
  development) fully in place and editable — a baseline/checkpoint is
  a snapshot to return to, not a freeze on further work.
- Do not consolidate genuinely separate, actively-maintained modules
  into one file "for tidiness" — this makes future targeted changes
  harder, not easier.

## After reorganizing

1. Rebuild — confirm no include/source path was broken by any move.
2. Rerun the full test suite — confirm identical pass/fail results to
   the pre-change baseline.
3. Spot-check that every internal doc cross-reference (relative links,
   "see X.md") still resolves to the new location.
4. Diff the actual source files that were touched (not just moved) —
   confirm no unintended behavior change slipped in alongside the
   documentation work.
5. Do not commit/tag/push until a human has reviewed the plan and the
   diff — a large reorganization pass is exactly the kind of change
   that benefits from review before it becomes permanent history.

## See also

`docs/current/` (an example of the output this playbook produces),
`archive/README.md` (the archive-not-delete policy in practice).
