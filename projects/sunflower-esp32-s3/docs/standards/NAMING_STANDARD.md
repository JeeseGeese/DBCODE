# Standard — Naming

Prescriptive rule for this project.

## Rule

1. **`enum class` for every mode/state/phase type**, never a plain
   `enum`.
2. **Reserved single-character serial commands must never contain the
   letter `k`** (case-insensitive), since `k`/`K` is intercepted
   unconditionally, even mid-word, by the central serial dispatcher.
   This is why `BehaviorState::THINKING`'s serial token is `pondering`
   — check any new serial token against this before choosing it.
3. **Check every new serial command (single-char or word) against the
   existing command tables** (`README.md`'s "Serial commands"/"Serial
   controls" sections) for collisions before adding it. This project
   has hit and fixed real collisions before (`f` inside `effects`, `p`
   the direction command vs. `p` the base-effect command).
4. **Tunable constants live in `Config.h`**, grouped by feature, each
   with a comment describing what raising/lowering it does — never a
   magic number inline at the call site.
5. **Shared timing values are named tiers**, not repeated literals —
   retuning a tier retunes every consumer at once.
6. **Percent-based motor speed conversion happens in exactly one
   function** (currently `percentToMotorPwm()`) — never a second
   percent-to-duty formula elsewhere.
7. **File/module names describe the owned resource or behavior**, not
   an implementation detail (`MotorDriver`, not `Gpio8And9Controller`).
8. **Lesson files use kebab-case, playbook/architecture/standards
   files use SCREAMING_SNAKE_CASE** — matches this project's existing
   convention as of the V1 baseline; keep new files consistent with
   whichever directory they're added to.

## Rationale

Most of these are drawn directly from `docs/current/SOFTWARE_ARCHITECTURE.md`'s
"Current coding conventions" and this project's own collision history
— see `docs/lessons/serial-dispatch-single-owner.md` for the concrete
incident behind rule 2/3.
