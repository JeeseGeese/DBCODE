# DRV8833 Motor Bring-Up — Sunflower ESP32-S3

**STATUS: MOTOR STARTUP ROOT CAUSE CONFIRMED AND PHYSICALLY FIXED (MECHANICAL) — MOTOR BRING-UP: PHYSICAL PASS**

## 1. Objective

Bring up a DRV8833 H-bridge motor driver on the Do Better Sunflower ESP32-S3
project, validate the GPIO/wiring path from the ESP32 to the motor, and
establish a minimal production motor control module — without disturbing
the existing LED, microphone, button, or audio-overlay behavior.

## 2. Hardware wiring

Verified physical board: **J2-bridged DRV8833 board**.

| Signal | Connection |
|---|---|
| DRV8833 IN1 | ESP32 GPIO8 |
| DRV8833 IN2 | ESP32 GPIO9 |
| DRV8833 VCC | ESP32 3.3V |
| DRV8833 GND | ESP32 GND (common) |
| Motor | DRV8833 OUT1 / OUT2 |
| ULT/SLEEP | Not connected / not driven by firmware |

Control method: **digitalWrite only**. No PWM in production yet.

Verified motor commands:

| Command | IN1 | IN2 |
|---|---|---|
| Forward | HIGH | LOW |
| Reverse | LOW | HIGH |
| Coast stop | LOW | LOW |
| Brake | HIGH | HIGH |

## 3. Test history

Bring-up proceeded through several stages, each narrowing the problem space:

1. An initial simple digital HIGH/LOW bring-up test moved the motor
   successfully on the first board tested.
2. A 7-stage LEDC/PWM characterization test (18 kHz, 8-bit resolution,
   threshold sweeps up to 70% duty, ramps, direction-change and
   repeatability stages) was built and run — see Section 4.
3. Because the PWM test produced no visible/audible response, the
   investigation stepped back to a plain digital isolation test — see
   Section 5.
4. A slow, multimeter-friendly electrical diagnostic (five fixed
   `digitalWrite` states, 10s holds) was built to allow direct voltage
   measurement at GPIO8, GPIO9, OUT1, OUT2, and across OUT1–OUT2 — see
   Section 6.
5. Final validation, using live serial-triggered `MotorDriver` commands
   during normal firmware operation, confirmed correct forward, reverse,
   and stop behavior — see Section 7.

## 4. Failed PWM characterization summary

A 7-stage PWM test was implemented using the Arduino-ESP32 core 2.0.17
channel-based LEDC API (`ledcSetup` + `ledcAttachPin` + `ledcWrite`,
confirmed via `cores/esp32/esp_arduino_version.h` — this project's
installed framework is *not* the pin-based core-3.x LEDC API). Configuration:
18 kHz, 8-bit duty resolution, one LEDC channel per input, duty capped at
70%, forward/reverse threshold sweeps (10/15/20/25/30/40/50%), ramps, a
safe direction-change sequence, and a repeatability test.

**Result:** the test completed in software with no reported build, upload,
or runtime errors (no resets, brownouts, or panics), but produced **no
visible or audible motor response** — a regression from the earlier working
digital HIGH/LOW test on the same wiring.

A project-wide static search confirmed no GPIO8/GPIO9 or LEDC channel 0/1
conflicts anywhere else in the codebase (the `Adafruit_NeoPixel` library
uses the RMT peripheral, not LEDC), ruling out a pin or channel conflict as
the explanation.

## 5. Digital isolation test

To isolate whether the regression was specific to the PWM path, a temporary
test disabled all LEDC/PWM code and reproduced the original digital
HIGH/LOW sequence exactly, using plain `pinMode`/`digitalWrite` only.

**Result:** the digital isolation test reproduced the original working
behavior — the motor moved during the commanded forward and reverse
pulses. This ruled out wiring, GPIO pin-mapping, and driver/motor hardware
failure as causes of the PWM-era regression at that point in the
investigation. A `digitalRead()` readback taken immediately after each
`digitalWrite()` also confirmed the ESP32 itself reported the correct
commanded logic level on both pins in all states, ruling out a firmware
logic-level bug.

## 6. Electrical diagnostic

A simplified, non-PWM electrical diagnostic was built: five fixed states
(STATE 0 both LOW, STATE 1 forward, STATE 2 both HIGH/brake, STATE 3
reverse, STATE 4 park), each held for 10 seconds with a live countdown, to
make multimeter measurement of GPIO8, GPIO9, OUT1, OUT2, and OUT1–OUT2
practical.

An intermediate run of this diagnostic showed anomalous behavior: ~0.2V
baseline on OUT1/OUT2 with intermittent, unprompted spikes to 2.2V and
3.3V accompanied by buzzing and brief movement — behavior not cleanly
tied to the commanded state transitions. A subsequent repeat of the same
diagnostic ran cleanly, with the motor reported to run correctly.

## 7. Final successful validation

Following removal of the temporary diagnostic and introduction of the
production `MotorDriver` module (Section 9), live testing via serial
commands (`f` = forward, `k` = stop) during normal firmware operation
confirmed:

- Motor runs forward correctly
- Motor runs reverse correctly
- Motor stops correctly
- DRV8833 output path works
- GPIO8/GPIO9 control path works
- No ESP32 resets
- No brownouts observed during the successful final test
- No watchdog failures
- Normal LED, microphone, audio-overlay, and button operation resumed
  afterward

One additional observation from live testing: the motor sometimes needs a
brief manual assist to start from a dead stop, then runs normally once
turning. This is consistent with the available starting torque/current
being close to the motor's static-friction threshold at the current supply
voltage, and does not indicate a wiring, GPIO, or driver-logic problem —
see Section 10.

## 8. Measured facts versus hypotheses

**Measured / observed facts:**

- The original digital HIGH/LOW test moved the motor.
- The 7-stage PWM test produced no visible/audible response.
- The digital isolation test (PWM disabled) reproduced correct forward/
  reverse movement.
- `digitalRead()` readback matched commanded GPIO state in all cases.
- No GPIO8/GPIO9 or LEDC channel conflicts exist anywhere else in the
  codebase.
- An intermediate electrical-diagnostic run showed ~0.2V baseline on
  OUT1/OUT2 with intermittent unprompted spikes to 2.2V/3.3V, buzzing, and
  brief movement.
- A repeat of the same diagnostic ran cleanly.
- Final validation showed correct forward/reverse/stop via live serial
  commands, with no resets/brownouts/watchdog failures.
- The motor sometimes needs a brief manual assist to start from a dead
  stop.

**Root-cause conclusion (hypothesis, not proven):**

> The immediate cause of the earlier intermittent motor failure remains
> unconfirmed. Final successful validation demonstrated that the ESP32,
> DRV8833, GPIO mapping, firmware, and motor are all capable of correct
> operation. Earlier intermittent behavior remains consistent with
> breadboard or jumper contact instability, marginal power integrity, or
> DRV8833 undervoltage behavior. Because supply voltage was not captured
> during the transient failure, UVLO remains a plausible hypothesis rather
> than a verified root cause.

This wording is deliberate: undervoltage lockout (UVLO) or insufficient
3.3V supply headroom must **not** be documented as a proven root cause
anywhere in this project. It remains one plausible explanation among
several (contact instability, marginal power integrity, DRV8833
undervoltage behavior) and should be treated as such in any future work
that references this bring-up.

## 9. Final MotorDriver architecture

`include/MotorDriver.h` / `src/MotorDriver.cpp` — production module,
`digitalWrite` only, no PWM/LEDC:

```cpp
void initMotor();                    // pins OUTPUT, forced LOW
void motorForward();                 // IN1=HIGH, IN2=LOW, until told otherwise
void motorReverse();                 // IN1=LOW, IN2=HIGH, until told otherwise
void motorStop();                    // IN1=LOW, IN2=LOW (coast)
void motorBrake();                   // IN1=HIGH, IN2=HIGH (brake)
void motorForwardMs(uint32_t ms);    // drives forward, then always stops
void motorReverseMs(uint32_t ms);    // drives reverse, then always stops
```

`initMotor()` is called as the very first line of `setup()`, before
`Serial`/LED/mic init, so the motor pins never float. A one-shot startup
verification (forward 250ms, stop 250ms, reverse 250ms) runs after the
rest of Sunflower has initialized, printing `[MOTOR] Initialization
successful.`, then leaves the motor stopped until commanded.

A live serial command interface (`f` = forward, `k` = stop) was added in
`main.cpp`, implemented via `Serial.peek()`/`Serial.read()` so it only
consumes bytes matching those two reserved keys — verified not to collide
with `Controls.cpp`'s existing command set (`n,p,o,x,+,-,m,d,h,g,r,b,a,c,v`
plus the word commands `effects`/`overlays`/`status`) and confirmed not to
interfere with it.

GPIO8/GPIO9 are touched nowhere else in the codebase (verified by static
search). No LEDC channel is active in production.

## 10. Known limitations

- No PWM / speed control yet — direction and stop/brake only.
- The motor sometimes needs a brief manual assist to start from a dead
  stop at the current 3.3V-only supply; this has not been resolved and is
  not attributed to a confirmed root cause (see Section 8).
- `motorBrake()` is implemented and was electrically exercised during
  diagnostics, but is not yet used by any application logic.
- Live `f`/`k` serial commands leave the motor running indefinitely once
  started (`f`) until explicitly stopped (`k`) or the board resets — by
  design for manual bring-up testing, not intended as the production
  control surface.
- No current-sensing or thermal-monitoring hardware exists; over-current,
  stall, or thermal conditions cannot be detected in software.

## 11. Production power recommendation

The current DRV8833 VCC supply — the ESP32's own 3.3V rail — is
**acceptable only for verified bench bring-up**. It must not be
automatically treated as the final production power architecture.

Before any sustained, loaded, or production use:

- Confirm the motor's rated voltage.
- Confirm the DRV8833 breakout's input voltage range.
- Evaluate and adopt an adequate external motor power supply, with a
  common ground shared with the ESP32, sized for the motor's actual
  current draw (including startup inrush).
- Re-validate motor behavior (including the dead-stop-assist symptom in
  Section 10) once an appropriate external supply is in place, since the
  underlying cause of that symptom was never conclusively isolated from
  supply headroom.

## 12. Git recovery instructions for commit 0005bd0

The final state of the temporary motor diagnostic (`include/MotorTest.h`,
`src/MotorTest.cpp` — the five-state electrical-walk diagnostic described
in Section 6) was committed for history before removal from the working
tree:

```
commit 0005bd0 — "Preserve final DRV8833 motor electrical diagnostic before removal"
```

To view the diagnostic source without altering the working tree:

```
git show 0005bd0:projects/sunflower-esp32-s3/src/MotorTest.cpp
git show 0005bd0:projects/sunflower-esp32-s3/include/MotorTest.h
```

To restore the files into the working tree (e.g. into a scratch branch, not
recommended directly on `main`):

```
git checkout 0005bd0 -- projects/sunflower-esp32-s3/include/MotorTest.h projects/sunflower-esp32-s3/src/MotorTest.cpp
```

## 13. Next-phase development plan

With hardware bring-up complete, the next phase moves from raw motor
control to programmed motor **behaviors**, layered strictly on top of
`MotorDriver` (see `include/MotorBehavior.h` / `src/MotorBehavior.cpp`):

- Non-blocking, `millis()`-based behavior engine (`MotorBehaviorMode`:
  `OFF`, `IDLE_SWAY`, `GENTLE_NOD`, `DANCE_BASIC`).
- Only `MotorDriver` may touch GPIO8/GPIO9; `MotorBehavior` calls only
  `MotorDriver` functions.
- `IDLE_SWAY` is the first implemented behavior (conservative timing, see
  `MotorBehavior.cpp`); `GENTLE_NOD` and `DANCE_BASIC` are placeholders
  pending future implementation.
- A temporary serial test interface (`ENABLE_MOTOR_BEHAVIOR_TEST`) allows
  exercising behaviors without touching the existing button state machine.
- Physical approval of `IDLE_SWAY` is required before further behaviors are
  built out or before any button/production trigger is wired to it.
- Audio-reactive motor behavior is an intended future direction; the
  `MotorBehaviorMode` enum and `updateMotorBehavior()` structure are
  designed to leave room for it without a redesign.

## 14. IDLE_SWAY physical validation

Timing tested: Forward 120ms → Stop 700ms → Reverse 120ms → Stop 1200ms.

**Observed:**

- Forward 120ms starts and moves successfully.
- Reverse 120ms starts and moves successfully.
- Visible movement occurs in both directions.
- The motor buzzes briefly when engaging.
- The motor stops cleanly.
- Serial emergency-stop command `k` stops it immediately.
- The motor causes the LEDs to visibly pulse when it engages.
- The LED pulse is not caused by microphone/audio pickup.
- Motor movement becomes inconsistent or weak while the LEDs are active.
- Motor movement is reliable when the LEDs are muted.

**Engineering conclusion:**

> IDLE_SWAY has passed physical direction, timing, stop, and
> emergency-stop validation. The motor, DRV8833 control path,
> MotorDriver, and MotorBehavior state machine are functioning
> correctly. The remaining instability is correlated with LED power
> consumption: the motor moves reliably with the LEDs muted but
> struggles while the LEDs are active, and motor engagement visibly
> disturbs LED output. This strongly indicates shared-supply power
> contention, voltage sag, or insufficient current headroom. It is not
> evidence of a MotorBehavior timing or direction-control defect.

No exact voltage sag was measured during this validation — the above is
based on the correlation between LED activity and motor reliability, not
an instrumented voltage reading.

**Recommendation:** the final hardware configuration should use a
dedicated motor power supply with a common ground, separate from the LED
strip's supply path, to eliminate this contention entirely. The
`MotorPowerGuard` module described in section 15 is a **temporary
bench-development workaround** — it lets software work on both LEDs and
motor behavior continue in the meantime by muting LEDs immediately before
the motor engages, but it does not increase available current and is not
a substitute for adequate, separate power.

## 15. MotorPowerGuard (temporary bench-development workaround)

`include/MotorPowerGuard.h` / `src/MotorPowerGuard.cpp` — coordinates LED
muting with motor engagement so the two stop fighting over the shared
power supply during continued software development. Non-blocking,
`millis()`-based, gated by `ENABLE_MOTOR_LED_POWER_GUARD` (set to 0 to
disable entirely).

```cpp
enum class MotorPowerGuardState { IDLE, PREPARING, READY, RELEASING };

void initMotorPowerGuard();
void requestMotorPower();          // saves + forces LED mute, starts 50ms settle
bool isMotorPowerReady();          // true once the 50ms delay has elapsed
void releaseMotorPower();          // starts 100ms settle, then restores LED state
void releaseMotorPowerImmediately(); // bypasses the 100ms delay (emergency stop, mode change)
void updateMotorPowerGuard();      // call every loop() iteration
```

Uses only `Controls.h`'s central `isMuted()`/`setMuted()` LED-mute API
(the latter added as a minimal, additive export alongside the existing
`isMuted()` — `toggleMute()` itself, the button state machine, and all
other `Controls.cpp` logic are unchanged). Never touches the NeoPixel
object, brightness, base effect, or audio-overlay state directly, and
duplicates no LED-control logic.

`IDLE_SWAY` was restructured to request power before each energized
segment and release it immediately after, without changing its rest
intervals: the power-request/ready wait (up to 50ms) happens *before*
each forward/reverse pulse starts, not counted as part of it. See
section 16 for pulse-duration calibration under this integration.

This module must not be treated as the final production power
architecture — see section 14's recommendation.

## 16. IDLE_SWAY pulse-duration calibration

**120ms (original value, tested with MotorPowerGuard active):** the
motor buzzed on each commanded pulse but did **not** produce reliable
visible motion, under the current shared-power bench configuration. This
differs from section 14's original digitalWrite-level validation (which
did observe movement at 120ms with LEDs manually muted) — with
`MotorPowerGuard` now auto-muting LEDs immediately before each pulse and
restoring them after, the pulse itself was too short to reliably move the
motor even with LED contention removed. This is consistent with the
shared-supply/current-headroom hypothesis in section 14: a short pulse
may not sustain long enough for the motor to reliably overcome static
friction, particularly at the current 3.3V-only supply.

**300ms (partial physical pass):** both directions capable of moving;
forward/reverse starts succeeded approximately 80% of the time. On
failed starts, the motor buzzed without turning until the gear was
lightly nudged by hand. `MotorPowerGuard` correctly muted the LEDs before
each engagement, and the inconsistent dead-stop starting behavior
persisted even with LEDs muted. Emergency stop remained functional
throughout.

> IDLE_SWAY at 300ms produced successful forward and reverse movement
> approximately 80% of the time. Failed starts presented as motor
> buzzing until the gear was lightly nudged. This is a partial physical
> pass but does not meet the reliability requirement for autonomous
> motion. Increasing pulse duration does not increase available starting
> torque, but one 500ms calibration test is justified to determine
> whether additional startup time materially improves reliability. If
> 500ms does not approach consistent starting, further pulse-duration
> increases should stop and the remaining issue should be classified as
> a motor-power or mechanical-starting limitation.

**500ms (failed):** `IDLE_SWAY_FORWARD_MS` and `IDLE_SWAY_REVERSE_MS` in
`src/MotorBehavior.cpp` were increased from 300 to 500 (rest intervals,
`MotorPowerGuard`'s 50ms preparation delay, and 100ms LED-restoration
delay all unchanged). Physical result: `MotorPowerGuard` muted the LEDs
correctly, but IDLE_SWAY still produced buzzing with no movement --
increasing the pulse from 300ms to 500ms did not improve startup. **500ms
is not an approved final IDLE_SWAY value.**

**Pulse-duration tuning is now stopped.** Per the engineering conclusion
below, additional pulse duration cannot increase startup torque, since
`digitalWrite` HIGH/LOW is already a full-power command at every tested
duration -- this was never a "startup kick" timing problem.

**Boot vs. runtime discrepancy.** The motor starts cleanly forward
and reverse during the existing boot-time startup verification (the
blocking `motorForwardMs(250)`/`delay(250)`/`motorReverseMs(250)`
sequence in `setup()`), every time, but does not start reliably during
runtime IDLE_SWAY -- even after LED output is muted (via
`MotorPowerGuard`) and pulse duration is increased to 500ms.

> The motor starts cleanly during the boot-time verification but does
> not start during runtime IDLE_SWAY, even after LED output is muted and
> pulse duration is increased to 500 ms. Because both paths use the same
> full digital motor command, additional pulse duration cannot increase
> startup torque. The remaining difference is the runtime system state:
> LED updates, microphone/audio processing, and normal loop activity are
> active during IDLE_SWAY but not during the earliest boot-time
> verification. The next diagnostic should recreate boot-equivalent
> peripheral conditions during runtime before changing motor voltage,
> PWM, or mechanical hardware.

See section 17 for the exact boot-time ordering that makes the boot pulse
succeed, and section 18 for `MotorPriorityMode`, the temporary
diagnostic layer built to test this hypothesis directly.

## 17. Boot verification ordering

Exact `setup()` ordering (see `src/main.cpp`), and what's active at each
step:

1. `initMotor()` -- GPIO8/GPIO9 configured OUTPUT, forced LOW.
2. `Serial.begin()`, wait for host attach, `delay(300)`.
3. `initLedEffects()` -- state only, no strip I/O.
4. `strip.begin()`, `strip.setBrightness(255)`, `strip.clear()`,
   `strip.show()` -- one blank/off frame written; strip is now idle.
5. `initAudioAnalyzer()` -- installs the I2S driver. Nothing reads from it
   yet.
6. `initControls()` -- button `pinMode`s, initial effect/overlay/
   brightness state, prints help. No continuous polling started yet.
7. `runHardwareTestSequence(strip)` -- **actively drives the LED strip**
   (solid colors, rainbow, many `strip.show()` calls) for several
   seconds, then blanks it; **actively reads I2S** (`updateAudioAnalyzer()`
   in a loop) for a 20s mic-verification window. Both LED and audio
   activity happen here, but finish before the next step.
8. `runMicRetest()` -- another **actively reading I2S** diagnostic
   (5 phases, ~30s total), independent of `AudioAnalyzer`'s pipeline.
   Finishes before the next step.
9. **`motorForwardMs(250)` / `delay(250)` / `motorReverseMs(250)`** -- the
   boot-time motor verification itself. At this exact point: the LED
   strip has been blank/idle since the end of step 7 (no `strip.show()`
   calls happen during this window); no code is reading the I2S
   peripheral (steps 7 and 8's read loops have both already returned);
   `loop()` has not started yet, so there is no button polling, no serial
   command processing, and no `updateAudioAnalyzer()` calls happening
   concurrently. This is the quietest possible system state the firmware
   ever reaches.
10. `initMotorPowerGuard()`, `initMotorBehavior()`,
    `initMotorPriorityMode()` -- state only.
11. `loop()` begins -- from here on, `updateControls()` (buttons +
    serial), `updateAudioAnalyzer()` (continuous I2S reads + RMS/envelope
    computation), and LED rendering (`strip.show()` roughly every
    `FRAME_INTERVAL_MS`=20ms, whenever not muted) are all continuously
    and concurrently active for the remaining lifetime of the program.

**Conclusion:** the boot pulse succeeds in a system state with zero
concurrent LED rendering and zero concurrent audio/mic processing --
not just "LEDs happen to be off," but no other peripheral activity of any
kind competing for CPU time, bus access, or (potentially) shared power
headroom. Runtime `IDLE_SWAY`, even with `MotorPowerGuard` muting LEDs,
still runs inside `loop()`, where `updateAudioAnalyzer()` and
`updateControls()` remain continuously active throughout. The boot test
was not moved for this investigation -- only instrumented/reproduced via
`MotorPriorityMode` (section 18) to test the peripheral-suspension
hypothesis without disturbing the proven-working boot sequence.

## 18. MotorPriorityMode (temporary boot-equivalent runtime diagnostic)

`include/MotorPriorityMode.h` / `src/MotorPriorityMode.cpp` -- a
coordination layer that recreates the boot-time quiet state (section 17)
at runtime, to test whether that -- rather than pulse duration, voltage,
or mechanics -- explains the boot-vs-runtime discrepancy.

```cpp
enum class MotorPriorityState { IDLE, PREPARING, READY, RELEASING };

void initMotorPriorityMode();
void requestMotorPriority();            // delegates LED-mute to MotorPowerGuard; suspends audio; 150ms settle
bool isMotorPriorityReady();            // true once settled AND MotorPowerGuard reports ready
void releaseMotorPriority();            // ensures motor stopped; 100ms settle; then resumes audio + restores LEDs
void releaseMotorPriorityImmediately(); // same, but bypasses the 100ms settle (emergency stop)
void updateMotorPriorityMode();         // call every loop() iteration
bool isMotorPriorityActive();
```

- Delegates **all** LED-mute save/force/restore to `MotorPowerGuard`
  (`requestMotorPower()`/`releaseMotorPowerImmediately()` internally) --
  no duplicated mute-state ownership between the two modules.
- Adds one new capability `MotorPowerGuard` didn't have: suspending
  `updateAudioAnalyzer()`. No changes were made to `AudioAnalyzer.h`/
  `.cpp` to achieve this -- `main.cpp`'s `loop()` simply skips *calling*
  `updateAudioAnalyzer()` for the (short, bounded) duration
  `isAudioProcessingSuspended()` is true, since that function is only
  ever invoked from that one call site. `AudioFeatures` just holds its
  last-computed values during suspension, which is harmless because
  rendering/overlay processing is also suspended (LEDs muted) for the
  same window.
- Buttons, serial commands, `updateMotorPowerGuard()`, and emergency stop
  (`k`) all keep running unconditionally throughout -- only
  `updateAudioAnalyzer()` is conditionally skipped.
- A one-shot, non-blocking **boot-equivalent runtime test** (serial
  command `2` -- see the note on `p` below) reproduces the exact
  forward/stop/reverse timing already proven to work at boot
  (250ms/250ms/250ms), inside `MotorPriorityMode`, to test the hypothesis
  directly without yet wiring it into repeating `IDLE_SWAY`.
- **Command substitution:** the task that specified this diagnostic
  requested `p` for the test trigger. `p` is already Controls.cpp's
  "previous base effect" command (`case 'p': advanceBaseEffect(-1)`), and
  the peek-based serial interceptor pattern used throughout this project
  would have silently stolen that byte and broken it. Substituted `2`
  instead, extending the existing `0`/`1` `MotorBehavior`-test numeric
  convention (see section 13) rather than introducing a new letter.

This module is diagnostic-only.

**Physical result:** the `2` test still buzzes without moving from a
dead stop -- recreating the boot-time quiet state (LEDs
muted+suspended, audio suspended) did **not** restore reliable startup.
This rules out the peripheral-suspension hypothesis: it is not LED
rendering, audio processing, or general loop activity competing for
resources. However: if the output gear receives a small manual flick by
hand, the motor begins moving. This points toward a mechanical
explanation -- static friction, gearbox position sensitivity, mechanical
preload, or insufficient breakaway torque at the current drive -- rather
than a remaining electrical or software timing issue. See section 19 for
the diagnostic built to test this directly.

## 19. Aggressive breakaway test (`MOTOR BREAKAWAY`, serial command `3`)

Motivated by the `2` test's physical result above: the motor moves once
manually flicked, but not from a commanded dead stop. This test attempts
to reproduce that manual flick in software rather than electrically --
**it does not increase electrical stall torque**. `digitalWrite`
HIGH/LOW is already full-power drive, identical to every other motor
test in this project (the `2` test, `IDLE_SWAY`, the boot-time
verification); there is no higher electrical setting to reach for.

**Mechanism:** a brief opposite-direction "jolt" (150ms) immediately
before the main drive pulse, intended to take up gear lash or unseat a
sticky gearbox position -- approximating what a manual flick does --
followed by a long (1500ms) full-power drive pulse in the intended
direction, long enough that any resulting movement is easy to observe.
Uses `MotorPriorityMode` exactly as the `2` test does (LEDs muted +
suspended, audio suspended, buttons/serial/emergency-stop still live);
mutually exclusive with the `2` test at the code level (each refuses to
start while the other is active), since both drive the motor directly
through the same `MotorPriorityMode` request.

**Sequence** (repeated for 2 complete cycles):

```
Prepare MotorPriorityMode -> wait until READY
  Reverse 150ms (forward-cycle jolt) -> Stop 100ms
  Forward 1500ms (full drive)        -> Stop 500ms
  Forward 150ms (reverse-cycle jolt) -> Stop 100ms
  Reverse 1500ms (full drive)        -> Stop 500ms
[repeat once more for cycle 2]
Stop -> release MotorPriorityMode -> MotorBehavior back to OFF
```

`motorStop()` is called between every single direction change (no
instantaneous polarity reversal anywhere in the sequence), and the
1500ms main pulse stays well under the existing 2000ms max-energized
safeguard (a local defensive backstop mirrors that safeguard here too,
though it should never trigger given the timing above).

**Interpretation guidance:**

- Success (movement after the jolt+long-pulse combination) would
  indicate the issue is genuinely about overcoming static
  friction/breakaway resistance from a dead stop, not insufficient
  drive.
- **Repeated buzzing without movement, even with the jolt, remains a
  hardware/mechanical finding -- not a reason to lengthen the pulse
  further.** There is no electrical dial left to turn; digitalWrite
  HIGH/LOW has been full drive at 120ms, 300ms, 500ms, and now a
  150ms-jolt + 1500ms-drive combination.
- **The user must not touch or flick the gear while the motor is
  energized** -- this test exists specifically to determine whether
  *software* can reproduce the effect of a manual flick; manually
  assisting it during the test would defeat that purpose and risks
  injury/damage.

Physical result of this test has not yet been confirmed -- pending
observation.

## 20. Confirmed root cause: mechanical belt preload (CLOSED)

The motor startup failure was mechanically caused by excessive and uneven
belt loading. The original belt was oblong and/or overly tight, producing
excessive breakaway resistance. Replacing it with a more uniform belt with
slightly greater slack restored motor movement. The motor, DRV8833, GPIO
control, and non-blocking firmware were functional. Software timing
changes did not correct the mechanical preload problem.

This closes the investigation across sections 3-19 above: the
electrical/GPIO path, the DRV8833 driver, and the non-blocking firmware
were never the problem, and none of the pulse-duration increases (120ms,
300ms, 500ms), the boot-equivalent runtime priority test (`2`), or the
jolt+long-pulse breakaway test (`3`) were the fix. All of that work
remains useful diagnostic history and reusable test infrastructure (`2`
and `3` still function as bench diagnostics), but it should not be read as
having been the cause or the fix -- the belt was.

**Motor bring-up status: physical PASS.** The mechanical fix has been
verified to restore motor movement. Longer-duration reliability testing
(sustained/repeated operation over time, under normal LED/audio load) is
still recommended before treating the motor as fully production-validated
-- this closes the *startup* investigation specifically, not a full
reliability sign-off.

## 21. 42-LED assembly and experimental motor+LED coexistence testing

A 42-LED WS2812 assembly (3 daisy-chained rows: Row 1 = 10, Row 2 = 10,
Row 3 = 22) has been physically connected on the same GPIO4/strip object
as the existing `NUM_LEDS=58` strip. **It has been observed working
correctly with the existing firmware as-is** -- current modes, effects,
brightness, and mute behavior all display properly with no indexing
failure, missing section, corrupted color data, or effect malfunction.

Per that observation, `NUM_LEDS` was deliberately left at 58, unchanged.
Whether the physically connected chain is exactly 42, exactly 58, or
something else has not been independently confirmed from the repository
-- but driving more logical pixels than are physically present is
standard, harmless WS2812 behavior (surplus data simply has no LED left in
the chain to land on), so there is no evidence the existing configuration
is unsafe or needs to change.

**Row metadata (see `include/Config.h`):**

```cpp
struct LedRegion { uint16_t start; uint16_t count; };
constexpr LedRegion LED_ROW_1{0, 10};
constexpr LedRegion LED_ROW_2{10, 10};
constexpr LedRegion LED_ROW_3{20, 22};
constexpr uint16_t PHYSICAL_LED_COUNT = 42;
```

These are metadata only (plus three `static_assert`s validating the
boundaries and that `PHYSICAL_LED_COUNT <= NUM_LEDS`) -- they do not
create a second NeoPixel object, do not replace the existing LED driver,
and do not change any effect's output by themselves.

**Optional row-identification test (serial `6`):** lights Row 1 (dim red,
1s), Row 2 (dim green, 1s), Row 3 (dim blue, 1s), then all 42 (dim white,
500ms), each separated by an off gap, writing directly to the existing
`strip` object (never a second NeoPixel instance). Cancelable at any point
via `k`. This confirms only that each phase was *commanded* -- physical
row-to-LED mapping must be visually verified by the user, not assumed from
this test having run.

**Experimental motor+LED coexistence (`MotorLedPowerMode`, see
`include/MotorPowerGuard.h`):** `MotorPowerGuard`'s existing `FULL_MUTE`
behavior (mute LEDs entirely during motor engagement) remains the default
and is unchanged. A new `DIM_DURING_MOTION` mode is available, used only
by the dedicated `5` test command -- normal motor commands (`2`/`3`) never
select it. When active: the currently-selected base effect keeps
animating normally (not frozen, not replaced with a static color); only
the audio overlay is suspended (the highest-current, least predictable
component); overall brightness is ramped (300ms, non-blocking) down to a
low test level before the motor engages, held for the motor's duration,
then ramped back up after the existing release settle delay. Brightness
index, mute state, base effect, and overlay selection in `Controls.cpp`
are never touched, so they are preserved automatically -- there is no
separate restore step to get wrong. Emergency stop
(`releaseMotorPowerImmediately()`) skips the ramp and restores instantly.

Test brightness is selectable among a fixed, capped set --
`{0, 4, 8, 12, 16}` out of 255 -- cycled via serial `4`; the diagnostic
does not go above 16 until physical testing at these levels has been
reviewed. `5` runs one forward + one reverse movement (250ms each,
matching the proven boot/priority-test timing, well under the 2000ms
max-energized safeguard) at the currently selected level.

**Not yet physically validated:** whether LEDs stay visually stable at any
of these levels while the motor runs, whether the motor is affected by
running the LEDs concurrently, and whether any brightness level in the set
causes flicker, brownout, ESP32 reset, or audio instability. Sparse
colored effects and this diagnostic's low test levels are the
lowest-current-risk starting point; full-strip white and bright
audio-reactive flashes (lightning, claps) are the highest-risk condition
and are not exercised by this diagnostic. Success at a low brightness
during this test does not by itself prove any effect is safe at full
brightness while the motor runs.

**Known reliability finding -- intermittent `k` miss during `5` (unresolved):**
Serial validation found that `k` intermittently fails to cancel the `5`
motor+LED coexistence test specifically (~50% miss rate across isolated,
precisely-timed repeated trials, consistently during the `FORWARD` phase).
The same `k` interceptor plumbing cancelled the `6` row test reliably
(5/5) in the identical test methodology, and a temporary debug print
confirmed that in the failing `5` trials, the `k` byte was never even seen
by `pollMotorSerialCommands()` -- this is not a state-machine logic bug,
the byte itself is going missing before the interceptor ever runs.

The most likely explanation: `5` is the *only* test in this codebase that
keeps full-rate LED rendering running (`strip.show()` roughly every
`FRAME_INTERVAL_MS`=20ms) *while the motor is engaged* -- `DIM_DURING_MOTION`
was deliberately designed this way (keep the effect animating, don't
freeze it), unlike every other motor test (`1`/`2`/`3`), all of which mute
or suspend LED rendering entirely for the engaged window. The working
hypothesis is that repeated `strip.show()` calls (WS2812 timing via the
RMT peripheral) are intermittently narrowing the window in which USB-CDC
serial bytes get serviced, occasionally dropping one.

**This has not been root-caused or fixed.** It was deliberately left
as-is rather than papered over, per the instruction to avoid unnecessary
LED-driver refactoring -- fixing it properly likely means either changing
how/when rendering happens during `DIM_DURING_MOTION`, or a lower-level
serial-buffering change, both larger than this diagnostic's scope.
Reassuring bound: even when `k` is missed, `5`'s own per-phase timing
(250ms segments, well under the 2000ms safeguard) always self-terminates
and restores FULL_MUTE/IDLE on its own -- the motor was never observed to
run away or stay energized indefinitely in any trial. Until this is
understood, do not treat `k` as an instantaneous guarantee specifically
for the `5` test, and keep power/reset accessible during physical testing
of `DIM_DURING_MOTION`.
