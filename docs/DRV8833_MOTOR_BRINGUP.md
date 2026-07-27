# DRV8833 Motor Bring-Up — Sunflower ESP32-S3

**STATUS: IDLE_SWAY PHYSICALLY VALIDATED WITH LED POWER LIMITATION**

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

**500ms (current value, under physical test — intended final
pulse-duration calibration):** `IDLE_SWAY_FORWARD_MS` and
`IDLE_SWAY_REVERSE_MS` in `src/MotorBehavior.cpp` increased from 300 to
500. Rest intervals unchanged (`IDLE_SWAY_FORWARD_REST_MS`=700,
`IDLE_SWAY_REVERSE_REST_MS`=1200). The `MotorPowerGuard` 50ms preparation
delay and 100ms LED-restoration delay are both unchanged and are still
not counted as part of the pulse. Physical movement/reliability at 500ms
has not yet been confirmed -- pending observation. Per the engineering
interpretation above, if 500ms does not approach consistent starting,
further pulse-duration increases should stop here and the remaining
unreliability should be classified as a motor-power or
mechanical-starting limitation rather than a timing one.
