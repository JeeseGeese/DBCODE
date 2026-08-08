# Sunny V1 — Changelog

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** This is inherently point-in-time material; see `CURRENT_STATUS.md` for what's active now.

Git-history-derived timeline of major milestones, oldest first. Exact
commit list: `git log --oneline` on `feature/expressive-motion-v1`
(branched from `v1.0.0`). This is a milestone summary, not a full
commit-by-commit log — see `git log` directly for that.

| Milestone | Commit (approx.) |
|---|---|
| Initial workspace + sunflower project scaffold | `44f5862` |
| Four-button sunflower control interface | `11875d3` |
| Verified INMP441 microphone diagnostics | `c998d4f` |
| Verified audio-reactive LED pulse mode | `0f487df` |
| Modularized firmware, richer audio overlays, AUTO_SHOWCASE | `becec47` |
| DRV8833 motor bring-up project (separate board) added | `a4cb1c8` |
| **`v1.0.0` tag** — first physically-validated baseline | tag |
| Final DRV8833 motor electrical diagnostic preserved | `0005bd0` |
| DRV8833 motor bring-up completed and documented | `c9457fd` |
| Non-blocking sunflower motor behavior layer | `330c979` |
| Temporary LED power guard for motor movement | `9db12f7` |
| Idle-sway pulse tuning (multiple commits) | `f77eb71`, `e12b632` |
| Runtime motor priority check + aggressive breakaway test | `1ccd523`, `4d173de` |
| Mechanical motor fix + 42-LED layout documented | `558e686` |
| Experimental dim-LED motor coexistence test | `89bfdc5` |
| Intermittent `k`-cancellation risk recorded, then fixed | `69aca9a`, `f6bed8d` |
| Quiet audio diagnostics + LED index mapper | `9fb6a7a` |
| Non-blocking expressive motion controller | `3ee21f2` |
| Expressive motion validation plan | `60ef79b` |
| Dual-purpose Button4 (LED overlay + motor audio-reactive) | `85c9fbc` |
| Dramatic/varied expressive motion (named pattern engine) | `e80b7bd` |
| Behavior Engine personality-state coordinator | `381d145` |
| **Motor + audio checkpoint through MusicMotor Rev 10.1** | `64e8aee` (tag `sunny-rev10.1-checkpoint`) |
| Sunny Rev 10.1 checkpoint docs + multi-AI dev infra | `a5039a8` |
| Unified Audio Mode; MusicMotorController promoted to production | `f628dcb` (tag `sunny-audio-mode-v1-physical-validation`) |
| Sunny speaker bring-up plan prepared | `a6a11db` |
| Power distribution documentation corrected (5V rail) | `8f0026a` |
| Speaker Stage S0-S3 bring-up, bench/format/multitone/volume-ladder diagnostics, buzz-diagnosis audit (uncommitted at V1 baseline) | working tree |
| **Sunny V1 documentation baseline** (this pass) | pending |

See `docs/V1/RELEASE_NOTES.md` for a narrative summary and
`docs/playbooks/`/`docs/lessons/` for the reusable engineering content
extracted from this history.
