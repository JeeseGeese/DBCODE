# Architecture — Software Architecture

The durable design patterns this codebase follows, independent of
which specific modules exist today. For the current module map, see
`docs/current/SOFTWARE_ARCHITECTURE.md`.

## Pattern 1: single-owner hardware resources

Every hardware resource (a GPIO pin, the I2S controller, the `Serial`
port, the LED strip object) has exactly one owning module. Every other
module reaches that resource only through the owner's public API. This
is the strongest, most consistently applied convention in the
codebase — see `docs/standards/GPIO_STANDARD.md` for the resulting
rule and `DESIGN_DECISIONS.md` for why.

## Pattern 2: single-owner I/O consumption

`Serial.read()`/`available()` are called from exactly one place in the
entire program; every other module receives already-read data via a
feed function. This project has direct, proven evidence of what
happens without this discipline (a ~50%-miss-rate emergency-stop race)
— see `docs/lessons/serial-dispatch-single-owner.md`.

## Pattern 3: non-blocking, `millis()`-driven state machines

No `delay()` in any behavior/controller module. Every multi-step
behavior is an explicit state enum plus stored timestamps, advanced
once per `loop()` call. See `docs/lessons/non-blocking-firmware-architecture.md`
and `docs/standards/TESTING_STANDARD.md`.

## Pattern 4: layered extension points, not duplicated safety logic

A higher-level coordinator extends an existing owner's public API
(e.g. `ExpressiveMotion::requestExpressivePattern()`) rather than
reimplementing that owner's safety guarantees itself. See
`MOTOR_PIPELINE.md` for the worked example.

## Pattern 5: pull-based, centrally-computed shared state

`AudioFeatures`/`AudioVisualState` are computed once per frame and read
(never independently re-derived) by every consumer. Avoids N different
modules each doing their own slightly-different signal processing on
the same raw input.

## Pattern 6: three-tier validation language

Host-validated / software-validated / physically-validated are never
used interchangeably in this project's documentation. See
`docs/standards/DOCUMENTATION_STANDARD.md` and
`docs/standards/TESTING_STANDARD.md`.

## Pattern 7: deterministic decision logic over `random()`

Wherever a decision needs to be reproducible for tuning/testing (beat-
action selection, drop-phrase choice), a bounded modular counter or
similar deterministic mechanism is used instead of raw randomness.
Makes host-testing possible for logic that would otherwise be
inherently non-reproducible.

## How this differs from `docs/current/SOFTWARE_ARCHITECTURE.md`

That file lists the *current* module map and resource-ownership table
— it will change as modules are added/renamed. This file describes the
*patterns* those modules follow — expected to stay true across many
future versions, until a deliberate architectural decision changes one
of them (which should then be recorded in `DESIGN_DECISIONS.md`).

## See also

`docs/current/SOFTWARE_ARCHITECTURE.md`,
`docs/architecture/DESIGN_DECISIONS.md`,
`docs/standards/`.
