# Sunny V1 — Expressive Motion & Behavior Engine

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/EXPRESSIVE_MOTION.md`.

See `MOTOR.md` for the general motor stack. This file covers the
idle/personality movement layers specifically.

## Architecture

```
Controls.cpp / main.cpp / (future) Raspberry Pi command
              |
              v
      BehaviorEngine (personality-state coordinator)
              |
   requestExpressivePattern() / setExpressiveMotionMode()
              v
        ExpressiveMotion (pattern-step engine + safety)
              |
   motorForward()/motorReverse()/motorStop() + MotorPowerGuard
              v
          MotorDriver (GPIO8/GPIO9)
```

`BehaviorEngine` owns none of `MotorDriver`, `MotorPowerGuard`, LED
rendering, microphone sampling, serial input, or buttons — it
coordinates exclusively through `ExpressiveMotion`'s public API.

## `ExpressiveMotionMode`

```cpp
enum class ExpressiveMotionMode { OFF, IDLE_ALIVE, AUDIO_REACTIVE };
```

`IDLE_ALIVE` — weighted random selection among 7 named patterns
(`GENTLE_SWAY` through `DRAMATIC_SWEEP`), never more than 2 consecutive
same-direction moves. `AUDIO_REACTIVE` — reacts to `AudioActivityBand`
(`QUIET`/`ACTIVE`/`STRONG`) derived from `AudioFeatures`, with
hysteresis/cooldowns.

## The key extension point: `requestExpressivePattern()`

```cpp
bool requestExpressivePattern(ExpressivePattern pattern);
```

Runs a caller-chosen pattern through the **exact same safety-checked
step engine** every other pattern uses (`MotorPowerGuard` coordination,
the `MOTION_MAX_ENERGIZED_MS` backstop, mandatory stops between
direction changes). Works independently of `ExpressiveMotionMode`,
including while `mode == OFF`. This is why `BehaviorEngine` never
becomes a second motor owner — every movement request from every
higher-level state funnels through this one function.

## `BehaviorState` (`BehaviorEngine.h`)

```cpp
enum class BehaviorState {
  MANUAL, IDLE, CURIOUS, LISTENING, THINKING, EXCITED, SLEEPING,
};
```

| State | Movement source | Notes |
|---|---|---|
| MANUAL | user's own `motion`/Button4 commands, unmediated | default at boot |
| IDLE | native `IDLE_ALIVE` engine | delegated, not a second scheduler |
| CURIOUS | `requestExpressivePattern()` on 1.5-4s interval | `FORWARD_REVERSE_NOD`/`DOUBLE_TWITCH`/`GENTLE_SWAY` |
| LISTENING | one immediate nod, then 4-9s interval | clap pulls next nod in to ≤500ms |
| THINKING | 3-7s interval, slow/sparse | `GENTLE_SWAY`/`SETTLE` |
| EXCITED | 0.9-2.2s interval, bounded 6-12s episode | auto-returns to IDLE on expiry |
| SLEEPING | none | fully at rest, LEDs untouched |

**Why not `thinking` as the serial token**: the central dispatcher
intercepts `k`/`K` unconditionally, even mid-word — "thinking" contains
a `k`. The enum value stays `THINKING`; the serial token is
`pondering`. Any new serial token in this codebase must avoid
containing `k`.

## LED coordination — deliberately deferred

Neither `ExpressiveMotion` nor `BehaviorEngine` alters LED rendering,
base effect, overlay selection, brightness, or mute state, in any
mode/state. A "motion accent" (a subtle highlight pulse synchronized to
movement) was explicitly deferred because implementing it without a
real compositing/layering mechanism in the render pipeline would be a
larger change than justified — see `LED_ENGINE.md`. **Do not introduce
a new LED rendering pipeline to force this in** without that being a
deliberate, separately-scoped decision.

## Safety (verified by inspection, every revision)

No `delay()` anywhere in either module. No instantaneous polarity
reversal (a real `STOP` step between any two `MOVE_*` steps in every
pattern table). Every pulse tier well under the 2000ms max-energized
safeguard. `k` cancels both reliably and forces `OFF`/`MANUAL`.
Diagnostics `2`/`3`/`5`/`6` have priority — neither module can start or
continue while any is active (bidirectional, verified both directions).

## Physical validation status — NOT YET PERFORMED

Both modules are **software-validated only**. Detailed physical
tuning checklists exist (originally in
`archive/superseded_docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 12
and `archive/superseded_docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section
15) covering: whether each pattern's strength/timing reads correctly
against the real mechanism, whether reversal pauses are long enough,
whether LEDs restore correctly after every transition, and whether `k`
halts movement immediately from every mode/phase/pattern. See
`KNOWN_LIMITATIONS.md`. **Do not leave `motion idle`/`motion audio` or
any `behavior` movement state running unattended until this checklist
has been run.**

## Serial commands

`motion [next|off|idle|audio|status|demo]`, `behavior`/`beh
[next|manual|idle|curious|listening|pondering|excited|sleeping|status|demo]`.
See `README.md`'s "Expressive motion" / "Behavior Engine" sections for
the full current command reference.

## Future integration surface (design-only, not implemented)

`BehaviorEngine`'s public API (`setBehaviorState()`,
`behaviorStateName()`, etc.) is intended as the Raspberry Pi
integration surface — e.g. `"BEHAVIOR LISTENING"` →
`setBehaviorState(BehaviorState::LISTENING)`. Camera-driven mapping
(face detected → `CURIOUS`, etc.) and speech-state mapping are
documented as illustrative only. See `ROADMAP.md` and
`archive/superseded_docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` sections
12-14 for the full detail — none of this is implemented.
