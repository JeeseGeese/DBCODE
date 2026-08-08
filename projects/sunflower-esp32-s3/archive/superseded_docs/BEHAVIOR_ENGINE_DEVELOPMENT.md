# Behavior Engine Development

Development-branch feature (`feature/expressive-motion-v1`). Does not touch
the `v1.0.0`-tagged baseline's own commands or behavior. Disabled by default
(`BehaviorState::MANUAL` at boot) -- nothing about existing LED/motor/audio
behavior changes unless a `behavior` command or a future controller
explicitly selects a state.

## 1. What this is

The Behavior Engine is a high-level personality-state coordinator layered
*above* `ExpressiveMotion` (see `EXPRESSIVE_MOTION_DEVELOPMENT.md`). Where
`ExpressiveMotion` answers "how does one movement pattern actually execute
safely," the Behavior Engine answers "which movement, and when, given the
sunflower's current named state." It owns none of `MotorDriver`,
`MotorPowerGuard`, LED pixel rendering, microphone sampling, serial input, or
buttons -- it coordinates exclusively through `ExpressiveMotion`'s public
API. It is hardware-independent: nothing in `BehaviorEngine.cpp` reads a pin
or touches `strip`.

## 2. Architecture

```
Controls.cpp / main.cpp / (future) Raspberry Pi command
              |
              v
      BehaviorEngine (this module)
              |
   requestExpressivePattern() / setExpressiveMotionMode()
              v
        ExpressiveMotion
              |
   motorForward()/motorReverse()/motorStop() + MotorPowerGuard
              v
          MotorDriver (GPIO8/GPIO9)
```

`BehaviorState` (`include/BehaviorEngine.h`):

```
MANUAL, IDLE, CURIOUS, LISTENING, THINKING, EXCITED, SLEEPING
```

- **MANUAL** -- the Behavior Engine is inert. The user's own `motion`
  commands and Button4 long-press have direct, unmediated control of
  `ExpressiveMotion`, exactly as before this feature existed.
- **IDLE** -- delegates to `ExpressiveMotion`'s own native `IDLE_ALIVE`
  engine (`setExpressiveMotionMode(IDLE_ALIVE)`) rather than a second
  scheduler -- calm ambient presence, reusing the already-tuned weighted
  pattern selection.
- **CURIOUS / LISTENING / THINKING / EXCITED** -- each drives movement via
  its own small, independent scheduler in `BehaviorEngine.cpp`
  (`updateScheduledMovement()`), calling `requestExpressivePattern()` on a
  randomized interval from a small, hand-picked candidate list. While any of
  these four states is active, `ExpressiveMotionMode` is held at `OFF`, so
  `ExpressiveMotion`'s own idle-timer/audio-trigger selection stays
  completely inert and never competes with the Behavior Engine's own choices
  for the same pattern engine.
- **SLEEPING** -- no owned movement at all (`ExpressiveMotionMode::OFF`,
  no scheduler). LED presentation is left completely untouched (see
  section 6).

## 3. `requestExpressivePattern()` -- the key extension point

`ExpressiveMotion` did not previously expose a way to request a *specific*
pattern; `IDLE_ALIVE`/`AUDIO_REACTIVE` always chose internally. Adding a
second, independent scheduler on top without a shared arbitration point
would have meant either the Behavior Engine reimplementing motor safety
logic (a second motor owner -- explicitly disallowed) or two schedulers
silently fighting over the same `ExpressiveMotionPhase`.

The fix, in `ExpressiveMotion.h`/`.cpp`:

- **`bool requestExpressivePattern(ExpressivePattern pattern);`** -- a new
  public function that runs a caller-chosen pattern through the *exact
  same* safety-checked step engine every other pattern uses (MotorPowerGuard
  coordination, the `MOTION_MAX_ENERGIZED_MS` backstop, mandatory stops
  between direction changes). It refuses (returns `false`, never queues) if
  a pattern is already running, `motion demo` is active, a motor diagnostic
  is active, or `IDLE_SWAY` is selected.
- It works **independently of `ExpressiveMotionMode`**, including while
  `mode == OFF`. This required restructuring `updateExpressiveMotion()`:
  previously, `if (mode == OFF) return;` short-circuited the *entire*
  function, meaning an in-flight pattern could theoretically get stuck
  mid-pulse if `mode` changed out from under it. The `mode == OFF` check now
  applies only inside the `IDLE` case's own "start a new pattern
  automatically" decision -- `PREPARING`/`MOVING`/`STOPPING`/`RELEASING`
  always advance regardless of mode, a safety improvement independent of
  the Behavior Engine (an in-flight pulse can never be abandoned mid-pattern
  by an external mode change).

This is why the Behavior Engine never becomes a second motor owner: every
movement request, from every state, funnels through this one function,
which itself funnels through the one pattern-step engine `ExpressiveMotion`
already had.

## 4. State-by-state movement

| State | Candidate patterns | Interval (Config.h) | Notes |
|---|---|---|---|
| IDLE | (native `IDLE_ALIVE` engine) | n/a | unchanged weighted selection |
| CURIOUS | `FORWARD_REVERSE_NOD`, `DOUBLE_TWITCH`, `GENTLE_SWAY` | `BEHAVIOR_CURIOUS_ACTION_{MIN,MAX}_MS` (1.5-4s) | frequent small investigative movement |
| LISTENING | `GENTLE_SWAY`, `FORWARD_REVERSE_NOD` | `BEHAVIOR_LISTENING_NOD_{MIN,MAX}_MS` (4-9s) | one immediate lean/nod on entry, then mostly still |
| THINKING | `GENTLE_SWAY`, `SETTLE` | `BEHAVIOR_THINKING_ACTION_{MIN,MAX}_MS` (3-7s) | slow, sparse, gentle |
| EXCITED | `EXCITED_TRIPLE`, `AUDIO_STRONG_BURST`, `DRAMATIC_SWEEP`, `FORWARD_REVERSE_NOD` | `BEHAVIOR_EXCITED_ACTION_{MIN,MAX}_MS` (0.9-2.2s) | bounded episode, see below |
| SLEEPING | none | n/a | fully at rest |

**LISTENING's audio awareness**: a bounded, simple hook -- if
`AudioFeatures.clap` is observed while LISTENING, the next scheduled nod is
pulled in to no more than `BEHAVIOR_LISTENING_CLAP_NUDGE_MS` (500ms) away
(never instantly). The Behavior Engine does not read the microphone or
`AudioAnalyzer` itself; `AudioFeatures` is passed in by `main.cpp`'s
`loop()` (`updateBehaviorEngine(now, getAudioFeatures())`), the same values
every other consumer reads.

**EXCITED's bounded episode**: on entry, a random duration between
`BEHAVIOR_EXCITED_EPISODE_MIN_MS` and `_MAX_MS` (6-12s) is armed. When it
elapses, the state automatically transitions to IDLE (via the same
`setBehaviorState()` path, so all the normal cancellation/reset behavior
applies). The action interval is deliberately several times longer than any
single pattern's own duration (all well under `MOTION_MAX_ENERGIZED_MS`), so
a genuine rest/recovery gap always separates one excited movement from the
next -- "no state can continuously energize the motor" holds here exactly as
it does for `ExpressiveMotion`'s own patterns.

**Refused requests retry, not queue**: if `requestExpressivePattern()`
refuses (still finishing a previous pattern, or a diagnostic started
concurrently), the scheduler retries after `BEHAVIOR_MOVEMENT_RETRY_MS`
(300ms) rather than waiting out the full randomized interval again.

## 5. The single transition function

Every state change -- from a `behavior` command, `behavior demo`, EXCITED's
own auto-expiry, or emergency stop -- goes through one function
(`transitionTo()` internally, exposed publicly as `setBehaviorState()`),
which on every accepted transition:

1. Refuses outright (state unchanged, no side effects) if the target is one
   of the four movement-producing states and a motor/LED diagnostic
   (`2`/`3`/`5`/`6`) is currently active -- see section 7.
2. Calls `setExpressiveMotionMode(ExpressiveMotionMode::OFF)`, which
   cancels any in-flight pattern (regardless of who started it), releases
   `MotorPowerGuard`, restores `MotorLedPowerMode` to `FULL_MUTE`, and
   resets `ExpressiveMotion`'s own idle timing.
3. Resets every Behavior-Engine-owned timer/counter (movement deadline,
   excited-episode deadline).
4. Records the new state and its entry timestamp.
5. Runs state-specific entry behavior: IDLE selects `IDLE_ALIVE`; EXCITED
   arms its episode deadline; LISTENING requests its immediate first
   nod and arms its own interval.
6. Prints `[BEHAVIOR] OLD -> NEW`.

This is deliberately **not** `cancelExpressiveMotion()` (the emergency-stop
path, which prints "Emergency stop" and latches `ExpressiveMotion`'s own
emergency-stopped flag) -- reusing that for a routine personality change
would misreport it as an emergency. `setExpressiveMotionMode(OFF)` performs
the same safe cancellation without the emergency semantics.

## 6. LED coordination -- deferred by design

The Behavior Engine does not alter LED rendering, base effect, overlay
selection, brightness, or mute state, in any state, including SLEEPING. This
was a deliberate scope decision, not an oversight: `VisualCue` (see
`VisualCue.h`) is a **full-strip, suppress-and-flash** mechanism -- while
active, it fully replaces the frame that would otherwise render, which is
exactly right for a brief, rare confirmation (overlay toggle, motor-mode
toggle) but not a good fit for a "subtle accent" meant to coexist with a
continuously-animating base effect without visibly interrupting it. Doing
that properly would need a real compositing/layering mechanism in the
render pipeline -- outside this feature's scope, and explicitly listed as
something to avoid ("do not introduce a new LED rendering pipeline").
Per the spec's own fallback ("if temporary LED accents cannot safely
coexist with the current rendering system, document and defer them rather
than modifying the LED pipeline"), all per-state LED accents (including
SLEEPING's optional dim/sleep presentation) are deferred to a future
generation. The base effect the user selected keeps running, completely
unaffected, through every Behavior Engine state.

## 7. Diagnostic mutual exclusion

- **Diagnostic -> Behavior Engine**: `requestExpressivePattern()` (called by
  every movement-producing state's scheduler) already refuses while any
  motor/LED diagnostic (`2`/`3`/`5`/`6`) is active -- inherited for free
  from `ExpressiveMotion`'s existing guard. Additionally,
  `setBehaviorState()` refuses the *transition itself* into a
  movement-producing state while a diagnostic is active (prints a refusal,
  state unchanged) -- matching Button4 long-press's existing "prefer
  refusal" precedent over silently entering a state that would sit idle
  until the diagnostic ends. `behavior demo` uses the same refusal for its
  own start.
- **Behavior Engine -> diagnostic**: no changes were needed. Every
  diagnostic's own start-guard already checks `isExpressiveMotionMoving()`,
  which reflects `ExpressiveMotion`'s shared phase state regardless of
  whether the in-flight pattern was started by its own idle timer, an audio
  trigger, or a Behavior-Engine `requestExpressivePattern()` call --
  Behavior-Engine-driven movement is therefore already transparently
  covered by the existing bidirectional exclusion, with no duplicated
  logic.

## 8. Manual control ownership

Any command that gives the user *direct* control of expressive movement
takes ownership back from the Behavior Engine first, via
`setBehaviorState(BehaviorState::MANUAL)`:

- `motion off` / `motion idle` / `motion audio` / `motion next` (bare
  `motion`) / `motion demo` -- **not** `motion status` (a read-only query).
- Button4 long-press, for both the "turn AUDIO_REACTIVE off" and "turn
  AUDIO_REACTIVE on" actions (the latter only once the diagnostic-active
  refusal has already been checked -- a refused action does not touch
  Behavior Engine state).

This call is guarded (`Controls.cpp`'s `takeManualMotionControl()`) so the
common case -- Behavior Engine untouched, already `MANUAL` -- does not print
a redundant "(unchanged)" line on every `motion` command.

## 9. Emergency stop

`main.cpp`'s `serviceEmergencyStop()` calls `stopBehaviorEngine()` alongside
its existing `cancelExpressiveMotion()` call. `stopBehaviorEngine()` forces
`BehaviorState` to `MANUAL`, cancels a running `behavior demo`, and resets
every Behavior-Engine-owned timer -- it does not itself touch the motor,
since `cancelExpressiveMotion()` (called separately, same function) already
stops/releases it regardless of which caller started the in-flight pattern.
Idempotent, like every other `k`-path function.

## 10. Serial commands

Word command `behavior` (or the shorter alias `beh`), routed from
`Controls.cpp`'s `dispatchCommand()` the same way `motion` already is:

```
behavior                -> help + status (no-arg)
behavior next            -> cycle MANUAL -> IDLE -> CURIOUS -> LISTENING -> THINKING -> EXCITED -> SLEEPING -> MANUAL
behavior manual          -> BehaviorState::MANUAL
behavior idle            -> BehaviorState::IDLE
behavior curious         -> BehaviorState::CURIOUS
behavior listening       -> BehaviorState::LISTENING
behavior pondering       -> BehaviorState::THINKING  (see note below on why the token isn't "thinking")
behavior excited         -> BehaviorState::EXCITED
behavior sleeping        -> BehaviorState::SLEEPING
behavior status          -> full Behavior Engine status line
behavior demo            -> 'behavior demo' (see below)
```

`?` also prints a `[BEHAVIOR]` status line (via `printBehaviorStatus()`),
alongside the existing `[MOTION]` line.

**Why `pondering`, not `thinking`**: `main.cpp`'s central serial dispatcher
intercepts `k`/`K` unconditionally, even mid-word (a deliberate design from
the original emergency-stop race investigation -- see that function's own
comment). The word "thinking" contains a `k`, so typing it as a serial
subcommand would always trigger an emergency stop partway through and
corrupt the rest of the line into a stray "ing" fragment -- found via serial
validation testing on this feature. `BehaviorState::THINKING` (the C++ enum
value, matching the spec verbatim) is unaffected; only the serial-facing
word-command token was renamed to avoid the reserved character. Any future
serial token in this codebase must avoid containing `k` for the same
reason.

## 11. `behavior demo`

Non-blocking, fixed dwell time per state
(`Config.h`'s `BEHAVIOR_DEMO_*_MS`), walking:

```
IDLE (5s) -> CURIOUS (6s) -> LISTENING (6s) -> THINKING (6s) -> EXCITED (8s) -> SLEEPING (4s) -> MANUAL
```

Total ~35s, inside the requested 25-45s range. Refuses to start while a
motor/LED diagnostic is active or a demo is already running. An explicit
`behavior <state>` command issued mid-demo cancels the demo (rather than
being silently overridden at the next step boundary) and takes effect
immediately. Cancelable at any point via `k` (which forces `MANUAL` and
clears `demoActive`, same as any other emergency stop). Restores `MANUAL`
and leaves base effect/overlay/brightness/mute completely unchanged on
completion.

## 12. Future Raspberry Pi integration (semantic mapping)

The public API (`setBehaviorState()`, `getBehaviorState()`,
`behaviorStateName()`, `startBehaviorDemo()`, `printBehaviorStatus()`) is
intended to be the integration surface for a future Raspberry Pi companion
sending high-level semantic requests over a yet-to-be-defined transport
(likely a simple line-oriented serial protocol, mirroring the existing
`behavior <state>` word command exactly -- no new protocol design is
proposed here). Example mappings:

```
"BEHAVIOR LISTENING" -> setBehaviorState(BehaviorState::LISTENING)
"BEHAVIOR THINKING"  -> setBehaviorState(BehaviorState::THINKING)
"BEHAVIOR EXCITED"   -> setBehaviorState(BehaviorState::EXCITED)
"BEHAVIOR IDLE"      -> setBehaviorState(BehaviorState::IDLE)
```

No networking, no Raspberry Pi communication transport, and no actual
listener for such a protocol is implemented in this generation -- this
section documents the intended shape only, per the explicit instruction not
to implement Pi comms in this pass.

## 13. Future camera event mapping -- document only, not implemented

Illustrative only; no camera integration exists in this firmware.

```
face detected nearby      -> BehaviorState::CURIOUS
face detected + speaking  -> BehaviorState::LISTENING
no motion for a while     -> BehaviorState::IDLE or BehaviorState::SLEEPING
sudden motion/loud event  -> BehaviorState::EXCITED (bounded episode, auto-returns to IDLE)
```

## 14. Future speech mapping -- document only, not implemented

Illustrative only. A dedicated `SPEAKING` `BehaviorState` was deliberately
**not** added in this generation -- `LISTENING` already covers "attentive,
mostly still, occasional acknowledgment movement," which is close enough to
a first-generation "the sunflower is being spoken to/is speaking" behavior
that a new enum value isn't yet justified. If a future generation needs
materially different movement while actively speaking (as opposed to
listening), add `SPEAKING` then, with its own profile in
`BehaviorEngine.cpp`, following the exact same pattern as the four existing
movement-producing states.

```
speech recognized, about to respond -> BehaviorState::THINKING (brief), then LISTENING or a future SPEAKING
LLM/response streaming               -> BehaviorState::THINKING or a future SPEAKING
response complete                    -> BehaviorState::IDLE
```

## 15. Physical tuning checklist

Mirrors `EXPRESSIVE_MOTION_DEVELOPMENT.md`'s own checklist -- none of this
has been physically validated from shell/serial testing alone:

- [ ] CURIOUS's action interval and pattern mix read as "investigative,"
      not manic or sluggish, against the real mechanism.
- [ ] LISTENING's single entry nod is visible but not startling; the long
      inter-nod interval genuinely reads as "mostly still."
- [ ] THINKING's movement is subtle enough to distinguish from CURIOUS.
- [ ] EXCITED's episode duration (6-12s) and action interval feel like a
      genuine burst of energy, with real recovery gaps between movements,
      not a blur.
- [ ] EXCITED's auto-return to IDLE is not jarring.
- [ ] `behavior demo`'s per-state dwell times give enough time to observe
      each personality distinctly.
- [ ] Confirm `k` stops movement immediately from every movement-producing
      state, physically, not just via serial log inspection.
- [ ] Confirm MotorPowerGuard/LED brightness return to normal (no residual
      dim/mute) after every Behavior Engine transition and after `behavior
      demo` completes or is cancelled.
