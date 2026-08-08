# Sunny V1 — Versioning Strategy (Recommendation, Not Yet Executed)

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** This is inherently point-in-time material; see `CURRENT_STATUS.md` for what's active now.

**No tag, branch, or release has been created by this document.** This
is a recommendation, prepared for approval. See the task's final
report for the exact next step to take once approved.

## Goals

- One active source tree — never a duplicated `V1.1`/`V1.2` folder.
- Sunny V1 preserved permanently and exactly recoverable.
- Future development (V1.1, V1.2, ...) continues from V1 on the same
  branch.
- Minimal future Claude/human context usage to determine "what's the
  current version" and "how do I get back to V1."

## Recommended model

```
main / feature/expressive-motion-v1  = active development, always
annotated Git tags                    = permanent stable-milestone markers
docs/V1/                              = the V1 documentation SNAPSHOT (frozen prose,
                                         describing what was true at the tag)
CURRENT_STATUS.md                     = always describes the CURRENT active
                                         development state, not V1 specifically
```

`docs/V1/*.md` is a point-in-time reference, not a live document — it
should not be edited to describe post-V1 changes (see
`docs/V1/OVERVIEW.md`'s own note on this). `CURRENT_STATUS.md` is the
opposite: always rewritten to reflect *now*, whatever version that is.

### Recommended tag name

`sunny-v1-baseline`

Rationale: this repository already has an unrelated `v1.0.0` tag (the
much earlier four-button/LED/mic-only baseline, predating motor/
speaker work entirely). A bare `v1.0.0`-style name for this much later,
far more complete milestone would be genuinely confusing next to the
existing tag. `sunny-v1-baseline` matches this project's existing
naming convention for engineering checkpoints
(`sunny-rev10.1-checkpoint`, `sunny-audio-mode-v1-physical-validation`)
while being unambiguous about what it marks.

### Do release branches add value here?

**No, not recommended.** A release branch would mean two lines of
development to keep synchronized for no real benefit — this project
has one person/session working on one codebase at a time, and the
explicit goal is avoiding duplicate/divergent source. An annotated tag
already gives an exact, permanent, checkoutable snapshot without that
maintenance burden. Revisit only if this project ever needs to
actively maintain two divergent lines simultaneously (e.g. patching an
old deployed unit while developing a new one) — not the case today.

## How to return to Sunny V1 exactly, later

```bash
git checkout sunny-v1-baseline          # detached HEAD, read-only inspection
# or, to branch off it for a hotfix/rollback:
git checkout -b rollback-from-v1 sunny-v1-baseline
```

## How to compare V1 vs. a later version (e.g. V1.1)

```bash
git diff sunny-v1-baseline..HEAD -- projects/sunflower-esp32-s3
git log sunny-v1-baseline..HEAD --oneline -- projects/sunflower-esp32-s3
```

## How to create future milestone tags

When a future milestone (V1.1, V1.2, ...) is physically validated and
worth marking permanently:

1. Update `CURRENT_STATUS.md` to reflect the new state.
2. Optionally snapshot a new `docs/V1.1/` (or rename convention TBD at
   that time) if the milestone is significant enough to warrant its
   own frozen reference set — not required for every tag.
3. `git tag -a sunny-v1.1-<short-description> -m "..."` (annotated,
   never lightweight, matching this project's existing tag style).
4. Do not move, delete, or reuse an existing tag (repository-wide rule,
   `/AGENTS.md`).

## How a future Claude session determines the active development version

1. Read `CURRENT_STATUS.md` — its "Last updated" line and content
   describe the current state directly.
2. `git describe --tags` (or `git tag --sort=-creatordate | head`) to
   see the most recent milestone tag reached.
3. `git log --oneline -10` for anything since the last tag.

Do **not** infer the active version from `docs/V1/` alone — that
directory is frozen at the V1 tag by design and will not reflect V1.1+
changes unless a future session deliberately creates a new frozen
snapshot for a later milestone.
