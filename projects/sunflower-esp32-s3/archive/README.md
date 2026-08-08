# Archive

Historical material for sunflower-esp32-s3, preserved for the record,
**not deleted**. Nothing here is part of the default read set for
ordinary development work — see
[`docs/CLAUDE_CONTEXT_GUIDE.md`](../docs/CLAUDE_CONTEXT_GUIDE.md) for
what to read instead.

## What's here

```
archive/
  speaker_bringup/      Historical speaker bring-up plan (Stage S0-S3 narrative)
  motor_bringup/         Full DRV8833 motor bring-up investigation (belt-preload
                          root cause, the k-miss serial race, PWM characterization)
  audio_experiments/     (reserved for future archived audio-analysis experiments)
  led_experiments/       LED test plan / index-mapping bring-up notes
  hardware_test_reports/ (reserved for future archived hardware-test artifacts)
  old_status_reports/    Superseded CURRENT_STATUS.md snapshots, dated
  logs/                  (reserved for future archived serial-capture logs)
  superseded_docs/       Development-planning docs superseded by docs/V1/
                          (ExpressiveMotion/BehaviorEngine development narratives)
```

Some subdirectories are currently empty (reserved) — they exist so a
consistent structure is in place for material that will accumulate
here later (e.g. future dated hardware-test captures), rather than
adding new top-level archive categories ad hoc.

## Why it's archived

Every file here was either:
- A **historical bring-up narrative** — describes an investigation, in
  the order it happened, including dead ends and superseded
  hypotheses. Valuable as a record, but not something a new task should
  need to re-read in full; the useful, durable conclusions have been
  extracted into `docs/lessons/`, `docs/playbooks/`, and the current
  `docs/V1/*.md` files.
- A **superseded planning document** — describes a design that was
  since implemented differently, or describes state (e.g. a GPIO table)
  that predates a later architecture change and is now factually
  stale. These are preserved exactly as originally written — **not**
  rewritten to match current reality — with a pointer added to the
  current source of truth. Rewriting history to look right in
  hindsight would destroy the record of what was actually believed/
  tested at the time, which is often exactly the information a future
  investigation needs.
- A **dated status snapshot** — `CURRENT_STATUS.md` is deliberately
  overwritten in place each time it's updated (see that file's own
  header); its prior versions are preserved here instead of only in
  `git log`, for a session that wants a quick before/after without
  digging through commit history.

## What was NOT archived

Active, still-editable implementation (`src/`, `include/`) is never
archived here, regardless of how much history exists around it — see
`docs/V1/OVERVIEW.md`'s "Files intentionally left active" framing. A
temporary-but-still-compiled diagnostic (e.g. `HardwareTest.cpp`,
`MicRetest.cpp`) stays in `src/`/`include/` with its own removal
instructions in its header comment — it is not "historical" until it's
actually been removed from the build.

## When a future session should search here

- Investigating a *recurrence* of a previously-diagnosed problem (check
  whether it was already root-caused and what was tried).
- Writing a new lesson/playbook and wanting the original detailed
  narrative behind an already-extracted summary.
- Specifically asked to review bring-up history, or asked "has this
  been tried before?"

Otherwise, prefer `docs/V1/`, `docs/lessons/`, and `docs/playbooks/` —
they're the extracted, current-relevant form of everything here.
