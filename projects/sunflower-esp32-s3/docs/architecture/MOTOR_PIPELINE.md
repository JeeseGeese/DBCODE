# Architecture — Motor Pipeline

How motor control is layered and why. For current status/revision
detail, see `docs/current/MOTOR.md`/`EXPRESSIVE_MOTION.md`.

## The layered stack

```
MotorDriver              sole owner of GPIO8/GPIO9 (digitalWrite AND PWM/LEDC primitives)
    ^
    |  (every layer above calls ONLY MotorDriver's exported functions)
    |
MotorPwmCalibration   MotorBehavior   ExpressiveMotion --> BehaviorEngine
DanceEngine (superseded)              MusicMotorController
```

Multiple independent "behavior" layers sit on top of one shared
electrical-primitives layer. Exactly one behavior layer may own the
motor at a time — enforced by a bidirectional mutual-exclusion check
(`isAnyMotorDiagnosticActive()`) every layer both consults before
starting and is itself reflected by once active.

## Why a layered stack instead of one monolithic motor controller

Different behaviors (raw diagnostic PWM sweeps, simple idle sway,
music-reactive dancing, personality-driven idle motion) have very
different decision logic but must share identical safety guarantees
(no instantaneous reversal, a max-energized backstop, instant emergency
stop). Layering means every behavior module only has to implement its
own decision logic — the safety guarantees live once, in
`MotorDriver` and the shared conventions every layer follows, not
reimplemented per behavior. See `DESIGN_DECISIONS.md`'s "Why this
GPIO layout / why non-blocking architecture?" entries.

## Why `ExpressiveMotion` exposes `requestExpressivePattern()` as an extension point

Without a shared "run this exact pattern safely" entry point, a
higher-level coordinator (`BehaviorEngine`) would have had to either
duplicate `ExpressiveMotion`'s safety-checked step engine (a second
motor owner — explicitly disallowed) or fight it for control of the
same phase state. Exposing one function that runs any caller-chosen
pattern through the existing safety engine, independent of
`ExpressiveMotion`'s own mode, is what lets `BehaviorEngine` add
personality-state-driven movement without ever becoming a second
motor owner. This is the general pattern for any future coordinator
layer added on top: extend the existing owner's public API, never
reach for `MotorDriver` directly.

## Why the safety net is a defensive backstop, not the primary mechanism

Every behavior layer times its own movements deliberately (a pattern
table, a PWM ramp, a music-driven phrase). A generic max-energized-time
ceiling exists on top of all of that specifically to catch a *logic
bug* that fails to stop a state on its own — it is not meant to be
relied on as the normal stop condition for anything. See
`docs/lessons/non-blocking-firmware-architecture.md`.

## Why music-reactive dancing and idle/personality motion are separate systems

`MusicMotorController` (audio-energy-driven, no LED-power mitigation
by design) and `ExpressiveMotion`/`BehaviorEngine` (idle/personality-
driven, LED-power-mitigated via `MotorPowerGuard`) solve genuinely
different problems with different tradeoffs — merging them would force
one power-mitigation policy onto both, which was explicitly rejected
once already (see `docs/current/POWER.md`). They remain mutually
exclusive at the motor-ownership level, never merged.

## See also

`docs/current/MOTOR.md`, `docs/current/EXPRESSIVE_MOTION.md`,
`docs/playbooks/DRV8833_MOTOR_BRINGUP.md`,
`docs/playbooks/MUSIC_REACTIVE_MOTION_VALIDATION.md`.
