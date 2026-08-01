# sunflower-esp32-s3

ESP32-S3 firmware project driving a 58-pixel WS2812B-compatible addressable
LED strip, controlled by four physical pushbuttons, with an INMP441 I2S
microphone driving optional audio-reactive overlays.

## v1.0.0 milestone

Tag `v1.0.0` marks the first physically-validated baseline: four-button
control, WS2812 LED effects, INMP441 audio input and audio-reactive
overlay, bidirectional DRV8833 motor control, the mechanical belt fix
(see below), motor+LED coexistence, the centralized serial dispatcher,
reliable emergency stop, the audio logging toggle, and the LED index
mapping diagnostic — all verified working together with no resets,
brownouts, watchdog failures, panics, or stuck motor states observed.

Active development beyond this point happens on
`feature/expressive-motion-v1` (branched from `v1.0.0`) — see
[`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`](../../docs/EXPRESSIVE_MOTION_DEVELOPMENT.md)
for the expressive-motion architecture built on top of this baseline, and
the "Expressive motion" section below for a summary.

## Hardware

- **MCU:** ESP32-S3-WROOM module, N16R8 variant (16 MB flash, 8 MB octal PSRAM)
- **Board:** generic ESP32-S3 devkit, USB-C, UART0 bridged to USB via an
  onboard WCH CH343 chip (not the ESP32-S3's native USB-OTG peripheral)
- **LEDs:** 58x WS2812B-compatible addressable LEDs, single data line
- **Buttons:** 4x momentary pushbuttons, each wired between its GPIO pin
  and GND, using the ESP32's internal pull-up (no external resistors)
- **Microphone:** 1x INMP441 I2S MEMS microphone
- **Power** (corrected 2026-08-01 — an earlier version of this line
  omitted the motor/amplifier and, elsewhere in this document,
  incorrectly stated they ran from the ESP32's 3.3V rail):

  ```text
  Sunny power distribution:
  - Shared 5V rail: WS2812B LEDs, MAX98357A amplifier, and DRV8833 motor driver
  - 3.3V rail: INMP441 microphone
  - Common ground across ESP32-S3 and all peripherals
  ```

  ESP32 GPIO logic signals remain 3.3V regardless of a peripheral's own
  power rail. See [Power](#power) below for the real electrical risk
  this shared 5V rail creates — this is not claimed to be a
  power-validated architecture.
- **Motor:** 1x DRV8833 H-bridge driver + brushed DC motor, bring-up
  complete — see [Motor driver (DRV8833)](#motor-driver-drv8833) below and
  [`docs/DRV8833_MOTOR_BRINGUP.md`](../../docs/DRV8833_MOTOR_BRINGUP.md) for
  the full bring-up history.

## GPIO assignments

| Signal | GPIO | Notes |
|---|---|---|
| LED data (DIN) | 4 | 3.3V logic-level data signal to LED strip |
| Mode button | 10 | `INPUT_PULLUP`, wired to GND — press: next base effect + next audio-overlay mode, double-press: previous base effect only |
| Mute button | 11 | `INPUT_PULLUP`, wired to GND — toggles LED output off/on |
| Brightness button | 17 | `INPUT_PULLUP`, wired to GND — cycles the 9-level brightness table |
| Button 4 | 5 | `INPUT_PULLUP`, wired to GND — short press: toggle audio overlay ON/OFF; long hold (900ms): toggle unified Audio Mode (LED overlay + MusicMotorController) — see [Button controls](#button-controls) below |
| INMP441 SCK/BCLK | 6 | I2S bit clock, driven by the ESP32 (I2S master) |
| INMP441 WS/LRCLK | 7 | I2S word select, driven by the ESP32 (I2S master) |
| INMP441 SD/DATA | 15 | I2S data input to the ESP32 |
| DRV8833 IN1 | 8 | `digitalWrite` for ordinary `MotorDriver` control; temporarily attached to an LEDC PWM channel while the manual PWM calibration test (`mf`/`mr`/`m##`/`mramp`/`mcycle`) is active — see [Motor PWM calibration test](#motor-pwm-calibration-test-development-branch) |
| DRV8833 IN2 | 9 | `digitalWrite` for ordinary `MotorDriver` control; temporarily attached to an LEDC PWM channel while the manual PWM calibration test is active — see [Motor PWM calibration test](#motor-pwm-calibration-test-development-branch) |
| MAX98357A BCLK | 6 | **shared with INMP441 SCK/BCLK above** — see Speaker hardware test section |
| MAX98357A LRC | 7 | **shared with INMP441 WS/LRCLK above** — see Speaker hardware test section |
| MAX98357A DIN | 16 | I2S data output from the ESP32 to the amplifier |

INMP441 `L/R` is tied to GND (selects the LEFT I2S channel) and `GND` is
tied to a common ground shared with the ESP32.

MAX98357A `VIN` is powered from the shared **5V** rail (corrected
2026-08-01 — previously and incorrectly stated as the ESP32's 3.3V pin;
see [Power](#power)); `GND` is the same common ground. `SD` is presently
a manually-controlled **logic** pin (GND = amplifier shutdown/silent,
3.3V = amplifier enabled — a logic-level connection, independent of
`VIN`'s power source) — see the Speaker
hardware test section below for the required startup sequence before
moving it. The speaker itself connects only between the amplifier's
speaker `+` and speaker `-` terminals — never to ground.

## Build instructions

```
cd ~/DOBETTERCODE/DBCODE/projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"   # if pio isn't already on PATH
pio run
```

## Upload instructions

Connect the board via USB-C, then confirm which port it actually is
(don't assume — if the drv8833-motor-test board is also plugged in, check
`pio device list`'s `SER=...` serial number against the board you mean to
flash):

```
pio device list                          # confirm the port, typically /dev/ttyACM0
pio run -t upload --upload-port /dev/ttyACM0
```

If your user account isn't in the `dialout` group, prefix the upload command
with `sg dialout -c "..."` or add yourself to the group and re-login:

```
sudo usermod -aG dialout $USER
```

## Serial monitor instructions

```
pio device monitor -p /dev/ttyACM0 -b 115200
```

(Ctrl+C to exit.)

## Motor driver (DRV8833)

**STATUS: MOTOR STARTUP ROOT CAUSE CONFIRMED AND PHYSICALLY FIXED (MECHANICAL) — PHYSICAL PASS.**
The motor startup failure was mechanically caused by excessive and uneven
belt loading. The original belt was oblong and/or overly tight, producing
excessive breakaway resistance. Replacing it with a more uniform belt with
slightly greater slack restored motor movement. The motor, DRV8833, GPIO
control, and non-blocking firmware were functional throughout — software
timing changes did not correct the mechanical preload problem. Longer-
duration reliability testing (sustained/repeated operation over time) is
still recommended. Full bring-up and validation history (failed PWM
characterization, digital isolation test, electrical diagnostic, final
hardware validation, IDLE_SWAY physical validation, measured facts vs.
hypotheses, and the confirmed root cause) is in
[`docs/DRV8833_MOTOR_BRINGUP.md`](../../docs/DRV8833_MOTOR_BRINGUP.md)
(see section 20 for the root-cause writeup).

**IDLE_SWAY physical validation:** forward/reverse 120ms pulses both move
the motor correctly, stop is clean, `k` emergency-stops immediately.
Motor engagement visibly disturbs LED output (not audio pickup), and motor
movement is reliable with LEDs muted but inconsistent/weak while LEDs are
active — this is correlated with shared-supply power contention, **not**
a MotorBehavior timing or direction-control defect (no exact voltage sag
was measured; see the doc above for the full writeup). A temporary
`MotorPowerGuard` bench-development workaround (below) mutes LEDs around
motor engagement so software work on both can continue — it is **not** a
substitute for a dedicated motor power supply.

**Pulse-duration calibration (stopped):** with `MotorPowerGuard`
auto-muting LEDs before each pulse, 120ms pulses buzzed but did not
produce reliable visible motion; 300ms was a **partial pass** (~80% start
reliability, failed starts buzzing until manually nudged); **500ms
failed** — LEDs muted correctly, but the motor still buzzed with no
movement, no better than 300ms. Since `digitalWrite` HIGH/LOW is already
a full-power command at every duration tested, **pulse-duration tuning is
now stopped** — increasing it further cannot increase starting torque.
500ms is **not** an approved final IDLE_SWAY value. See
`docs/DRV8833_MOTOR_BRINGUP.md` section 16.

**Boot vs. runtime discrepancy:** the motor starts cleanly, every time,
during the boot-time startup verification, but not reliably during
runtime IDLE_SWAY — even muted and at 500ms. Both paths issue the same
full digital motor command, so the difference isn't pulse duration; it's
that LED rendering, microphone/audio processing, and normal loop activity
are all continuously active during runtime but not during the boot pulse
(see `docs/DRV8833_MOTOR_BRINGUP.md` section 17 for the exact boot
ordering). `MotorPriorityMode`'s `2` test (below) recreates that quiet
state at runtime — **result: still buzzes without moving.** This rules
out the peripheral-suspension hypothesis. However, a small manual flick
of the output gear does let it start, pointing toward static friction,
gearbox position sensitivity, or insufficient breakaway torque rather
than remaining electrical/software causes. `MotorPriorityMode`'s `3`
test attempts to reproduce that flick in software — see below.

**Root cause (confirmed, closed):** the belt driving the mechanism was
oblong and/or overly tight, producing excessive and uneven breakaway
resistance — mechanical, not electrical or software. Replacing it with a
more uniform belt with slightly more slack restored motor movement. None
of the pulse-duration tuning, the `2` boot-equivalent test, or the `3`
breakaway test were the fix; they remain useful diagnostic history and
bench tooling. See `docs/DRV8833_MOTOR_BRINGUP.md` section 20.

**Verified wiring** (J2-bridged DRV8833 board):

| Signal | Connection |
|---|---|
| DRV8833 IN1 | ESP32 GPIO8 |
| DRV8833 IN2 | ESP32 GPIO9 |
| DRV8833 VCC | Shared 5V rail (corrected 2026-08-01 — see [Power](#power)) |
| DRV8833 GND | ESP32 GND (common) |
| Motor | DRV8833 OUT1 / OUT2 |
| ULT/SLEEP | Not connected / not driven by firmware |

**Control method:** `digitalWrite` only. **PWM is intentionally deferred**
— no speed control yet, direction and stop/brake only.

### MotorDriver API (`include/MotorDriver.h` / `src/MotorDriver.cpp`)

The only module allowed to touch GPIO8/GPIO9.

| Function | Behavior |
|---|---|
| `initMotor()` | Configures GPIO8/GPIO9 as outputs, forces both LOW. Called first in `setup()`, before Serial/LED/mic init. |
| `motorForward()` | IN1=HIGH, IN2=LOW — runs until told otherwise |
| `motorReverse()` | IN1=LOW, IN2=HIGH — runs until told otherwise |
| `motorStop()` | IN1=LOW, IN2=LOW (coast) |
| `motorBrake()` | IN1=HIGH, IN2=HIGH (DRV8833 brake mode) |
| `motorForwardMs(ms)` | Drives forward for `ms`, then always stops |
| `motorReverseMs(ms)` | Drives reverse for `ms`, then always stops |

### MotorBehavior architecture (`include/MotorBehavior.h` / `src/MotorBehavior.cpp`)

Non-blocking, `millis()`-based behavior layer built strictly on top of
`MotorDriver` — it never touches GPIO8/GPIO9 directly. Intended as the
foundation for future motor "personality" (idle motion now, audio-reactive
behavior later) without redesigning the driver layer.

```cpp
enum class MotorBehaviorMode { OFF, IDLE_SWAY, GENTLE_NOD, DANCE_BASIC };

void initMotorBehavior();                    // resets to OFF, motor stopped
void setMotorBehavior(MotorBehaviorMode m);   // always stops the motor first
MotorBehaviorMode getMotorBehavior();
void updateMotorBehavior();                   // call every loop() iteration
void stopMotorBehavior();                     // immediate stop, forces OFF
void printMotorBehaviorDebugState();          // mode/phase + MotorPowerGuard state, for '?'
```

- Starts in `OFF` after boot. **Not** auto-enabled — a mode must be
  explicitly selected.
- Switching modes always stops the motor first, so a previous behavior's
  drive state can never bleed into the next one.
- `GENTLE_NOD` and `DANCE_BASIC` are currently safe placeholders: selecting
  them keeps the motor stopped and prints a one-time "not implemented"
  message.
- A generic 2-second max-energized-runtime safety net applies regardless of
  which behavior is active or what its own timing logic does — a backstop,
  not the primary timing mechanism.

**IDLE_SWAY** (the only implemented behavior; pulse duration under
active physical calibration, see above) — conservative, fully
non-blocking, no `delay()`:

```
Request power -> [wait] -> Forward 500ms -> Stop -> Release power
  -> Stop 700ms
  -> Request power -> [wait] -> Reverse 500ms -> Stop -> Release power
  -> Stop 1200ms -> repeat
```

Pulse duration (`IDLE_SWAY_FORWARD_MS`/`IDLE_SWAY_REVERSE_MS` in
`src/MotorBehavior.cpp`) is currently 500ms — the intended final
pulse-duration calibration value, after 120ms buzzed without motion and
300ms was only a partial pass (~80% start reliability; see
`docs/DRV8833_MOTOR_BRINGUP.md` section 16). If 500ms doesn't approach
consistent starting, further pulse-duration increases should stop and
the remaining issue should be classified as a motor-power or
mechanical-starting limitation. Rest intervals
(`IDLE_SWAY_FORWARD_REST_MS`=700,
`IDLE_SWAY_REVERSE_REST_MS`=1200) are unchanged. Each pulse is preceded
by a `MotorPowerGuard` request/ready wait (up to 50ms, see below), which
is *not* counted as part of the pulse. The motor always passes through a
stop segment between forward and reverse — no direct forward-to-reverse
transition.

### MotorPowerGuard — temporary bench-development workaround (`include/MotorPowerGuard.h` / `src/MotorPowerGuard.cpp`)

**Not a production fix.** The DRV8833 currently shares the ESP32's power
supply with the LED strip, and motor engagement visibly disturbs LED
output (see the physical validation note above). This module coordinates
the two so software development on both can continue: it mutes the LEDs
immediately before the motor engages and restores them shortly after it
stops. Non-blocking, `millis()`-based, gated by
`ENABLE_MOTOR_LED_POWER_GUARD` (set to 0 to disable — `isMotorPowerReady()`
then always returns true immediately and no muting/restoring happens).

```cpp
enum class MotorPowerGuardState { IDLE, PREPARING, READY, RELEASING };

void initMotorPowerGuard();
void requestMotorPower();            // saves + forces LED mute, starts 50ms settle
bool isMotorPowerReady();            // true once the 50ms delay has elapsed
void releaseMotorPower();            // starts 100ms settle, then restores LED state
void releaseMotorPowerImmediately(); // bypasses the 100ms delay (emergency stop, mode change)
void updateMotorPowerGuard();        // call every loop() iteration, regardless of MotorBehavior mode
```

- Uses only `Controls.h`'s central `isMuted()`/`setMuted()` LED-mute API —
  never touches the NeoPixel object, brightness, base effect, or
  audio-overlay state directly, and duplicates no LED-control logic.
  `setMuted(bool)` was added as a minimal, additive export alongside the
  existing `isMuted()`; `toggleMute()`, the button state machine, and
  everything else in `Controls.cpp` are unchanged.
- The motor never energizes before `isMotorPowerReady()` returns true.
- Repeated `requestMotorPower()` calls within one PREPARING/READY cycle
  never re-save or overwrite the originally-captured mute state — so if
  LEDs were already manually muted before the motor requested power, they
  stay muted afterward; if they weren't, they're restored to unmuted.
- Emergency stop (`k`) and any `MotorBehavior` mode change call
  `releaseMotorPowerImmediately()`, so LEDs can never be left stuck muted.

### MotorPriorityMode — temporary boot-equivalent runtime diagnostic (`include/MotorPriorityMode.h` / `src/MotorPriorityMode.cpp`)

**Diagnostic only.** Built to test whether the boot-vs-runtime motor
discrepancy (above) is caused by concurrent LED/audio/loop activity —
recreates the quiet system state present during the boot-time motor
verification (see `docs/DRV8833_MOTOR_BRINGUP.md` section 17) at runtime.
Non-blocking, `millis()`-based, gated by `ENABLE_MOTOR_PRIORITY_MODE`.

```cpp
enum class MotorPriorityState { IDLE, PREPARING, READY, RELEASING };

void initMotorPriorityMode();
void requestMotorPriority();            // delegates LED-mute to MotorPowerGuard; suspends audio; 150ms settle
bool isMotorPriorityReady();            // true once settled AND MotorPowerGuard reports ready
void releaseMotorPriority();            // ensures motor stopped; 100ms settle; resumes audio + restores LEDs
void releaseMotorPriorityImmediately(); // same, but bypasses the 100ms settle (emergency stop)
void updateMotorPriorityMode();         // call every loop() iteration
bool isMotorPriorityActive();
```

- Delegates **all** LED-mute save/force/restore to `MotorPowerGuard` —
  no duplicated mute-state ownership between the two modules.
- The only new capability: suspending `updateAudioAnalyzer()`. No changes
  were made to `AudioAnalyzer.h`/`.cpp` — `main.cpp`'s `loop()` simply
  skips *calling* it while `isAudioProcessingSuspended()` is true, since
  that's its only call site. `AudioFeatures` just holds its last value
  for the (short, bounded) suspension window.
- Buttons, serial commands, and emergency stop (`k`) keep running
  unconditionally throughout — only `updateAudioAnalyzer()` is skipped.
- The one-shot **boot-equivalent runtime test** (`2`, see below)
  reproduces the exact 250ms/250ms/250ms forward/stop/reverse timing
  already proven to work at boot, inside `MotorPriorityMode`, without yet
  wiring it into repeating `IDLE_SWAY`. **Result: still buzzes, no
  movement** — rules out the peripheral-suspension hypothesis. See the
  boot-vs-runtime note above and `docs/DRV8833_MOTOR_BRINGUP.md` section
  18 for the full writeup.

### MOTOR BREAKAWAY — aggressive mechanical breakaway test (serial command `3`)

**Diagnostic only, does not replace `2`.** Motivated by `2`'s physical
result: the motor moves once the output gear is manually flicked, but
not from a commanded dead stop — pointing toward static friction,
gearbox position sensitivity, or insufficient breakaway torque rather
than a remaining electrical/software cause. This test attempts to
reproduce that manual flick in software: a brief opposite-direction
"jolt" to take up gear lash / unseat a sticky position, followed by a
long full-power drive pulse so any resulting movement is easy to see.
**This does not increase electrical stall torque** — `digitalWrite`
HIGH/LOW is already full-power drive at every duration tested (120ms,
300ms, 500ms, and now this jolt+long-pulse combination); there is no
higher electrical setting to reach for. See
`docs/DRV8833_MOTOR_BRINGUP.md` section 19.

Uses `MotorPriorityMode` exactly as `2` does (LEDs muted+suspended, audio
suspended, buttons/serial/`k` still live) — mutually exclusive with `2`
at the code level, since both drive the motor directly through the same
`MotorPriorityMode` request.

```
Prepare MotorPriorityMode -> wait until READY
  Reverse 150ms (forward-cycle jolt) -> Stop 100ms
  Forward 1500ms (full drive)        -> Stop 500ms
  Forward 150ms (reverse-cycle jolt) -> Stop 100ms
  Reverse 1500ms (full drive)        -> Stop 500ms
[repeat once more -- 2 complete cycles total]
Stop -> release MotorPriorityMode -> MotorBehavior back to OFF
```

`motorStop()` between every direction change (no instantaneous polarity
reversal), 1500ms main pulse well under the existing 2000ms
max-energized safeguard (a local defensive backstop mirrors that
safeguard here too). **Repeated buzzing without movement, even with the
jolt, is a hardware/mechanical finding — not a reason to lengthen the
pulse further.** Do not touch or flick the gear while the motor is
energized during this test — it exists specifically to determine whether
software alone can reproduce the effect of a manual flick.

### 42-LED assembly and experimental motor+LED coexistence testing

A 42-LED WS2812 assembly (3 daisy-chained rows: Row 1 = 10, Row 2 = 10,
Row 3 = 22) has been physically connected on the same GPIO4/strip object
as the existing `NUM_LEDS=58` strip, and **has been observed working
correctly with the existing firmware as-is** — no code changes were
required for current modes/effects/brightness/mute to display properly on
it. `NUM_LEDS` was therefore deliberately left at 58; see
`docs/DRV8833_MOTOR_BRINGUP.md` section 21 for the full writeup, including
why driving more logical pixels than are physically present is safe.

Row layout is recorded as lightweight metadata in `include/Config.h`
(`LedRegion LED_ROW_1/2/3`, `PHYSICAL_LED_COUNT = 42`, plus boundary
`static_assert`s) — it does not create a second NeoPixel object or alter
any effect's output by itself.

`MotorPowerGuard` gained an experimental `MotorLedPowerMode` alongside its
existing (default, unchanged) `FULL_MUTE` behavior:

```cpp
enum class MotorLedPowerMode { FULL_MUTE, DIM_DURING_MOTION };
```

`DIM_DURING_MOTION` is used only by the `5` test command below — `2`/`3`
always use `FULL_MUTE`. When active, the current base effect keeps
animating normally (not frozen, not replaced), the audio overlay is
suspended, and brightness ramps (300ms, non-blocking) down to a selectable
low test level for the motor's duration, then back up. `Controls.cpp`'s
brightness index, mute, base effect, and overlay selection are never
touched, so they're preserved automatically with no separate restore step.

### Motor PWM calibration test (development branch)

**Temporary, non-blocking calibration tool** (`include/MotorPwmCalibration.h`
/ `src/MotorPwmCalibration.cpp`) for finding usable PWM speeds on the
installed motor -- built on top of new `MotorDriver` PWM primitives
(`initMotorPWM()`/`deinitMotorPWM()`/`motorPWMForward()`/`motorPWMReverse()`/
`motorPWMCoast()`), which are the only functions in the whole project that
touch GPIO8/GPIO9 with PWM; everything above (`motorForward()`/
`motorReverse()`/`motorStop()`/`motorBrake()`, still `digitalWrite`-based) is
unchanged and works normally again as soon as this test ends (`deinitMotorPWM()`
detaches the LEDC channels and returns both pins to plain digital LOW).

- **PWM config:** ~19kHz, 8-bit resolution (duty 0-255), using the ESP32
  Arduino core's channel-based LEDC API (`ledcSetup`/`ledcAttachPin`/
  `ledcWrite(channel, duty)`) -- verified against the actually-installed
  `framework-arduinoespressif32` package rather than assumed; that package's
  `esp32-hal-ledc.h` does not expose the newer pin-based `ledcAttach(pin, ...)`
  API, so the channel-based calls are the correct ones for this build.
- **Exclusive ownership:** starting any calibration command (`mf`/`mr`/`m##`/
  `mramp`/`mcycle`) preempts `MotorBehavior` (forced to `OFF`) and is refused
  if another motor diagnostic (`2`/`3`/`5`/`6`) or expressive motion is
  currently active, exactly like those diagnostics refuse each other.
  `isAnyMotorDiagnosticActive()` (already consulted by expressive motion/the
  Behavior Engine/Button4) now also reports true while this test is active,
  and `2`/`3`/`5`/`6` each additionally refuse to start while it's active --
  so nothing can fight it for the motor. Ending the test (`mstop`, natural
  completion, or `k`) always leaves `MotorBehavior` at `OFF`; it never
  auto-resumes `IDLE_SWAY` or any previous motion.
- **Startup kick:** when starting from a dead stop at a requested duty below
  70%, applies 100% duty for 100ms (within the requested 80-120ms range),
  then settles to the requested duty -- prints `[MOTOR TEST] Startup kick
  applied: ...` when it fires. `mkick` toggles it on/off so kick-enabled vs.
  kick-disabled starts can be compared directly (default: enabled).
- **Safe direction changes:** never switches directly from powered forward
  to powered reverse. If a direction change is requested while the motor is
  actively driving, it ramps to 0, both GPIO8/GPIO9 go LOW, waits 70ms
  (within the requested 60-80ms range), then starts the new direction.
  `mramp`/`mcycle` additionally pass through their own explicit
  coast/neutral steps before ever reversing.
- **`mramp`:** forward 20%->100% in 10% steps, 2s per step, then coast
  500ms, then the same 20%->100% progression in reverse, then stop -- prints
  a `[MOTOR TEST] Direction/Requested/Duty/Startup kick/Step N/9` block at
  every transition.
- **`mcycle`:** a 13-step "dance" sequence (forward 35% -> ramp to 60% ->
  hold -> ramp to 100% -> hold -> ramp to 0 -> 75ms neutral -> reverse 100%
  -> ramp to 55% -> hold -> ramp to 85% -> hold -> ramp to 0 and stop),
  demonstrating slow/medium/full-energy dancing, sustained rotation, a quick
  electrically-safe reversal, and ramped (vs. instant) acceleration in one
  routine.
- **`mstatus`:** prints active/routine/direction, current + target PWM duty
  (raw and %), startup-kick enabled/active, current routine step + time
  remaining, pending-reversal state, whether the last stop was an emergency
  stop, and GPIO8/GPIO9's currently commanded state.
- **Safety:** `k` cancels every pending kick/ramp/hold/reversal/routine step
  immediately and commands GPIO8/GPIO9 LOW (via the existing emergency-stop
  latch in `main.cpp`, which now also calls this module's cancel function);
  `mstop` does the same (coast + full cancel), just without the
  emergency-stop-specific prints. No stale timer can restart the motor after
  either.
- **No delay() anywhere in this module** -- `updateMotorPwmCalibration()` is
  called every `loop()` iteration and drives everything from `millis()`,
  internally rate-limited to update the physical duty roughly every 15ms
  (within the requested 10-20ms range).
- Does not touch `SharedI2S`, `AudioAnalyzer`, speaker generation,
  microphone configuration, LEDs, buttons, or any GPIO assignment other than
  the existing GPIO8/GPIO9 motor pins.

### Audio Mode (unified)

**The normal, production, physical-button path to music-driven dancing.**
As of the Sunny Rev 10.1 architecture, `MusicMotorController` (see
[MusicMotorController](#musicmotorcontroller-development-branch) below) is
Sunny's sole production music-driven dancing engine; `DanceEngine` (below)
is superseded and disabled by default. Audio Mode is the one user-facing
state that coordinates both halves of "make Sunny dance to music":

```
Audio Mode OFF: LED audio overlay OFF, MusicMotorController OFF, motor STOPPED
Audio Mode ON:  LED audio overlay ON,  MusicMotorController ON
```

`DanceEngine` is never part of this state -- it stays disabled regardless
(`ENABLE_LEGACY_DANCE_ENGINE=0`, see below).

- **Button 4 long hold (900ms+)** is the only physical-button path;
  normal users never need the serial monitor to start Sunny dancing to
  music. See [Button controls](#button-controls) below for the exact
  press/hold/release mechanics.
- **On (successful):** enables the LED overlay and `MusicMotorController`
  together, one green confirmation flash, prints
  `[AUDIO MODE] ON | LED overlay=ON | MusicMotor=ON`.
- **Off:** always succeeds -- disables the LED overlay, safely
  stops/resets `MusicMotorController` (`musicMotorDisable()` -> its own
  `hardStop()`/`resetRuntimeState()`, same safe-shutdown path the
  `musicmotor off` serial command already used), two red confirmation
  flashes, prints
  `[AUDIO MODE] OFF | LED overlay=OFF | MusicMotor=OFF | Motor=STOPPED`.
- **Motor ownership conflicts:** if another motor diagnostic
  (`MotorPwmCalibration`, the priority/breakaway/motor+LED tests, the LED
  index mapper) currently owns the motor, an enable request is rejected --
  neither half is enabled (no silently-half-on state), three quick white
  flashes, e.g. `[AUDIO MODE] Enable rejected: motor owned by
  MotorPwmCalibration`. `MusicMotorController` already being active on its
  own (e.g. a prior standalone `musicmotor on`) is not treated as a
  conflict -- the enable call simply completes the LED-overlay half.
- **Serial equivalent (dev/test only):** `audiomode on` / `audiomode off`
  / `audiomode status` -- exactly the same coordinated path Button 4 uses.
  Normal users should never need this; it exists for testing without
  physical hardware.
- **Status reporting:** both `status` and `audiomode status` report
  `Audio Mode (unified): ON | OFF | PARTIAL`. `PARTIAL` means exactly one
  half is on -- this only happens after independently toggling the LED
  overlay (`x`) or `MusicMotorController` (`musicmotor on`/`off`) directly
  over serial; Button 4 and `audiomode on`/`off` always drive both halves
  together and can never leave a partial state on their own. The standalone
  `musicmotor`/`x` serial commands remain available for
  development/testing and are documented as creating partial states, not
  removed.

### Dance Engine V1 (superseded -- disabled by default)

**Superseded by `MusicMotorController` Revision 10.1** (see
[Audio Mode (unified)](#audio-mode-unified) above and
[MusicMotorController](#musicmotorcontroller-development-branch) below).
Disabled by default as of this task: gated behind
`ENABLE_LEGACY_DANCE_ENGINE` in `include/DanceEngine.h`, currently `0`. When
`0` (every normal build), `initDanceEngine()`/`updateDanceEngine()` are
never called, the `danceon`/`danceoff`/`dancestatus`/`dancetest*`/
`dance{quiet,mid,high,peak}` serial commands and their help-text entry are
unavailable, and every `DanceEngine` function becomes a cheap no-op/false
stub -- no initialization, no per-loop work, no motor-ownership claim.
Retained only for historical reference and rollback safety during the
Audio Mode physical-button integration's validation window -- see
`CURRENT_STATUS.md`'s "DanceEngine removal checklist" for what has to be
true before this file is deleted outright. The description below documents
the legacy engine as it exists when `ENABLE_LEGACY_DANCE_ENGINE=1`.

**Live microphone-driven PWM dancing** (`include/DanceEngine.h` /
`src/DanceEngine.cpp`), built as a dedicated choreography layer on top of
the physically-validated PWM range/kick/coast behavior from
`MotorPwmCalibration`:

```
AudioAnalyzer -> DanceEngine -> MotorDriver / PWM motor primitives
```

`DanceEngine` decides target speed, direction, hold duration, when to
reverse, ramp rate, and when to rest; `MotorDriver` remains responsible only
for executing electrical motor commands (`motorPWMForward()`/
`motorPWMReverse()`/`motorPWMCoast()`/`initMotorPWM()`/`deinitMotorPWM()` --
unchanged, reused as-is, no duplicated LEDC setup). Reads only
`AudioAnalyzer`'s existing `AudioFeatures` (`normalized`, `transientStrength`,
`clap`) -- no second microphone-analysis pipeline, and it never touches
I2S/`SharedI2S` directly, so it reacts identically whether the sound came
from the onboard speaker or the environment. Disabled by default; the user
must explicitly send `danceon`. **Not yet physically validated** -- V1
starting values only, per `docs/DRV8833_MOTOR_BRINGUP.md`'s physically-proven
80-100% active-speed range, 19kHz/8-bit PWM, and 75ms reversal-coast timing.

- **Active speed range:** only 80-100% duty is ever commanded while moving
  (`DANCE_MIN_SPEED_PERCENT`/`DANCE_MAX_SPEED_PERCENT` in `Config.h`) --
  the physically-validated useful-movement range. 0% (coast) remains valid
  for resting.
- **Energy mapping:** a normalized "dance energy" (0.0 = at/near the
  adaptive noise floor, 1.0 = very strong) is derived from
  `AudioFeatures.normalized` (already computed against the adaptive noise
  floor -- no separate floor tracking here), then smoothed with its own
  independent fast-attack/slow-release filter (`DANCE_ENERGY_ATTACK`/
  `DANCE_ENERGY_RELEASE`, distinct from the LED-facing envelope smoothing in
  the Audio section above) so the motor doesn't twitch on individual
  samples. Energy bands 0.00-0.15/0.15-0.35/0.35-0.60/0.60-0.82/0.82-1.00
  map to resting/80-85%/85-92%/92-97%/97-100%.
- **Direction changes are deliberately less frequent than beats:** reversals
  are decided only by `AudioFeatures.transientStrength`/`clap` plus timing,
  never a fixed per-beat cadence, so the motor commits to a direction for a
  sustained sweep instead of oscillating: a strong transient (or clap) can
  request a reversal once at least `DANCE_MIN_DIRECTION_HOLD_MS` (700ms) has
  elapsed in the current direction; a medium transient requires
  `DANCE_MEDIUM_DIRECTION_HOLD_MS` (1200ms); a `DANCE_REVERSAL_COOLDOWN_MS`
  (1000ms) cooldown after any reversal prevents rapid oscillation either
  way. Deterministic, not random, for repeatable V1 validation. A small
  transient does nothing special -- it already nudges target speed up via
  the attack-smoothed energy, satisfying "keep direction, optionally
  increase speed briefly" with no extra code path.
- **Safe reversal, reusing the validated sequence:** ramp current duty to 0
  -> both GPIO8/GPIO9 LOW -> coast `DANCE_REVERSE_COAST_MS` (75ms) -> drive
  the new direction -> ramp back toward the live energy-derived target.
  Ramp rates are asymmetric: moderate ramp-up, a slightly slower ramp-down
  during normal dancing, and a quicker ramp-down specifically when a
  reversal is imminent (`DANCE_RAMP_UP_PERCENT_PER_SEC`/
  `DANCE_RAMP_DOWN_PERCENT_PER_SEC`/`DANCE_RAMP_DOWN_FAST_PERCENT_PER_SEC`).
- **Startup kick:** reuses the validated kick concept -- starting movement
  from a dead stop (fresh start or right after a reversal coast) at a
  target below `DANCE_STARTUP_KICK_THRESHOLD_PERCENT` (90%) briefly commands
  100% for `DANCE_STARTUP_KICK_MS` (100ms), then settles to the requested
  speed. Never re-applied on ordinary in-flight speed updates.
- **Rest/silence hysteresis:** starts moving once smoothed energy exceeds
  `DANCE_START_ENERGY_THRESHOLD` (0.18); only ramps down to rest once it has
  stayed below the lower `DANCE_STOP_ENERGY_THRESHOLD` (0.10) for
  `DANCE_SILENCE_HOLD_MS` (500ms) -- prevents start/stop chatter right at
  one boundary. V1 defaults to true rest during quiet sections (no
  automatic idle sway) for easier evaluation.
- **Non-blocking:** no `delay()` anywhere in this module. `updateDanceEngine()`
  is called every `loop()` iteration; energy smoothing and the deterministic
  test-sequence timer update every call, while the motor-output/decision
  tick is internally rate-limited to `DANCE_PWM_UPDATE_MS` (~15ms).
- **Exclusive motor ownership:** `danceon` is refused while any motor
  diagnostic (`2`/`3`/`5`/`6`), `MotorPwmCalibration`, or expressive motion
  owns the motor, and preempts `IDLE_SWAY`
  (`setMotorBehavior(MotorBehaviorMode::OFF)`) -- the same bidirectional
  mutual-exclusion pattern already used among the existing diagnostics
  (`isAnyMotorDiagnosticActive()` now also reports true while `DanceEngine`
  is enabled, so nothing else can start while it owns the motor, and it
  cannot enable while anything else does). If `ExpressiveMotionMode::AUDIO_REACTIVE`
  is currently selected, `danceon` turns it off first, printing why --
  `DanceEngine` is the single audio-to-motor behavior path; the old
  audio-reactive motor pulses in `ExpressiveMotion` and this module are
  never allowed to drive the motor at the same time (`ExpressiveMotion`'s
  LED-coexistence/audio-band logic and the microphone pipeline itself are
  completely untouched -- only its own direct motor pulses stop firing
  while `DanceEngine` owns the motor, via the same mutual exclusion).
- **Safety:** `k` immediately cancels `DanceEngine` (coasts, detaches PWM,
  forces GPIO8/GPIO9 LOW, disables). `mstop` (the manual-motor-command
  system's universal stop) also cancels `DanceEngine` if it happened to own
  the motor. `danceoff` clears every pending kick/ramp/hold/reversal/test.
  `deinitMotorPWM()` always returns GPIO8/GPIO9 to plain digital LOW before
  `DanceEngine` releases ownership, so no stale `digitalWrite`-based command
  can unexpectedly resume. `DanceEngine` never automatically re-enables
  after `k`/`mstop`/`danceoff` -- only an explicit `danceon` resumes it. A
  `DANCE_MAX_SEGMENT_MS` (10000ms) backstop, mirroring
  `MOTION_MAX_ENERGIZED_MS`/the breakaway test's own backstop elsewhere in
  this codebase but deliberately much longer (sustained multi-second
  single-direction movement is this module's normal operation, not a
  fault -- an earlier, much shorter 2500ms value was found during
  `dancetest` validation to trip on ordinary no-transient sustained
  playback), forces a ramp-down if a single direction ever runs
  unexpectedly long regardless. All analyzer inputs are sanitized against
  NaN/Inf and clamped to their valid ranges before use.
- **LED power handling: none -- `DanceEngine` never touches LED mute
  state.** It does not call `MotorPowerGuard`'s `requestMotorPower()`/
  `releaseMotorPower()`, and (as of this revision) does not call
  `Controls.h`'s `isMuted()`/`setMuted()` either. LEDs are left exactly as
  the user set them through startup, ramping, sustained movement, and
  reversal alike -- if the user had LEDs active, they stay active the
  entire time the motor dances; if muted, they stay muted. An earlier
  revision added a brief (~150ms) `DanceEngine`-owned mute window around
  startup/reversal specifically to reduce simultaneous motor+LED current
  draw; it was removed by explicit request, so that mitigation no longer
  exists for `DanceEngine`. **Warning:** the DRV8833 and LED strip still
  share the 5V rail (corrected 2026-08-01 — previously misstated as the
  ESP32's 3.3V rail; see [Power](#power) below) -- without any
  suppression, sustained high-duty PWM movement and full LED rendering can
  now draw simultaneously for as long as a dance session lasts, not just
  a brief pulse. `MotorPowerGuard` itself is completely unmodified --
  `IDLE_SWAY`, `ExpressiveMotion`, and the motor+LED diagnostics (`2`/`3`/
  `5`) still get its protection exactly as before; only `DanceEngine` runs
  with no such mitigation. **Not physically validated** either way --
  whether this causes visible LED disturbance, brownout, or resets during
  real dancing has not been observed.
- **`dancetest`:** a deterministic, non-blocking simulated-energy sequence
  (silence -> low -> medium -> high -> peak+strong-transient -> medium ->
  silence, ~12.6s total) that exercises the exact same energy-smoothing/
  reversal-decision path live audio uses -- never a canned direct motor
  command -- so it demonstrates starting from rest, the startup kick, 80%
  slow movement, gradual speed increase, sustained movement, a peak near
  100%, one safe direction reversal, gradual slowdown, and a return to
  rest, without needing microphone input. Auto-enables `DanceEngine` first
  if it's currently off. `dancetestoff` cancels it (or a
  `dancequiet`/`dancemid`/`dancehigh`/`dancepeak` override) and returns to
  live microphone input if `DanceEngine` remains enabled.

### Serial commands

| Command | Action |
|---|---|
| `f` | `MotorDriver` forward (continuous, fires immediately, no Enter needed) |
| `k` | Immediate stop — raw `MotorDriver` hold, any active `MotorBehavior` (forces mode to `OFF`), and any active `MotorPriorityMode`/priority/breakaway/motor+LED/LED-map test; releases/restores `MotorPowerGuard` |
| `0` | `MotorBehavior` OFF *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`, on by default)* — also releases `MotorPowerGuard` immediately |
| `1` | `MotorBehavior` IDLE_SWAY *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `2` | Boot-equivalent `MotorPriorityMode` runtime test (Preparing → Forward 250ms → Stop 250ms → Reverse 250ms → Stop → release → back to `MotorBehavior` OFF) *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `3` | Aggressive breakaway test: jolt + 1500ms full drive, 2 cycles (see MOTOR BREAKAWAY above) *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `4` | Cycle the experimental `DIM_DURING_MOTION` test brightness level: `0, 4, 8, 12, 16` *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `5` | Experimental motor+dim-LED coexistence test: Preparing → dim LEDs active → Forward 250ms → Stop → Reverse 250ms → Stop → Restoring → Complete, at the level selected via `4`. `k` cancellation was found to be intermittent (~50%) here, root-caused to a two-consumer serial race, and fixed — see `docs/DRV8833_MOTOR_BRINGUP.md` section 21 for the full writeup and validation (10/10 FORWARD, 10/10 REVERSE, plus edge cases) *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `6` | LED index mapping tool (revised — see `docs/DRV8833_MOTOR_BRINGUP.md` section 21). Phase A: color test (red/green/blue, ~1s each, all 42 LEDs). Phase B: interactive single-index walk starting at 0 — `n`/`p` step, `j` jumps +10, `r` restarts at 0, `x` exits, `k` cancels. Phase C (`c` from within Phase B): optional candidate-row check, clearly labeled as unconfirmed. Neither the physical row mapping nor the color order is claimed as confirmed — the tool only makes them observable *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `7` | Toggle continuous audio serial output (the periodic `[AUDIO]` heartbeat and mic fault warnings) — default **off**. Does not affect microphone sampling, audio-reactive LEDs, or on-demand dumps (`d`/`status`) — see `docs/DRV8833_MOTOR_BRINGUP.md` section 22 *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `?` | Print `MotorBehavior` mode/phase, `MotorPowerGuard`/`MotorLedPowerMode` state, `MotorPriorityMode` state + LED/audio-suspended flags, the priority/breakaway/motor+LED/LED-map tests' active/phase state, and audio processing/overlay/log-enabled status *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |

These fire on the single byte, unlike the Enter-terminated commands in
[Serial controls](#serial-controls) below. They're implemented by
`pollSerialDispatcher()` in `main.cpp` — the single, central owner of
`Serial.read()`/`available()` for the whole program (see
`docs/DRV8833_MOTOR_BRINGUP.md` section 21 for why: an earlier
peek-based design left non-reserved bytes for `Controls.cpp`'s own
independent Serial-reading loop, which raced it and occasionally stole
`k`). Reserved bytes are handled directly; everything else is forwarded
exactly once to `Controls.cpp`'s `feedSerialByte()` — verified not to
collide with any existing single-char or word command
(`n,p,o,x,+,-,m,d,h,g,r,b,a,c,v`, `effects`/`overlays`/`status`). `k` is
checked first and unconditionally (even mid-word), acting through an
emergency-stop latch; every other reserved byte defers to `Controls.cpp`
while a word command is mid-type, so bytes like the `f` inside "effects"
aren't misread as motor commands. Two deliberate substitutions from what
was originally planned, both to avoid stealing bytes from existing
commands: `s` → `k` for motor-stop (`s` is the first letter of `status`),
and `p` → `2` for the boot-equivalent test (`p` is Controls.cpp's existing
"previous base effect" command). `3`/`4`/`5`/`6`/`7` were each checked
against the full command map and have no collision. While the LED index
mapping tool (`6`) is active, `n`/`p`/`j`/`r`/`c`/`x` are additionally
intercepted with mapping-tool-specific meanings (see `6`'s description
above) instead of their normal `Controls.cpp` meanings -- this is
context-sensitive (only while `isLedMapActive()`), so those keys behave
normally the rest of the time.

### Safety behavior

- No command leaves the motor energized indefinitely by design: timed
  helpers (`motorForwardMs`/`motorReverseMs`) always stop themselves;
  `IDLE_SWAY`'s longest energized segment is 500ms; a generic 2s
  max-runtime safety net backstops all behaviors regardless of mode. The
  MOTOR BREAKAWAY test's longest single energized segment is 1500ms
  (`3`'s main drive pulse), still well under that 2s ceiling; it carries
  its own local defensive backstop mirroring the same safeguard.
- `k` is a full emergency stop from any state (raw drive or any behavior,
  including the priority test and breakaway test).
- No current-sensing or thermal-monitoring hardware exists — over-current,
  stall, and thermal conditions cannot be detected in software.

### Early bring-up limitations (historical)

These predate PWM/speed control and the later `MotorPwmCalibration`/
`DanceEngine`/`MusicMotorController` work documented further below — kept
here as bring-up history, not current status. **PWM/percent-based speed
control (M0-M100) exists and is validated** — see `MotorPwmCalibration`,
`DanceEngine`, and `MusicMotorController` below for the current state. For
a current, non-historical limitations list, see "Known limitations and
deferred work" at the end of the MusicMotorController section.

- Runtime `IDLE_SWAY` does not start the motor reliably even at 500ms and
  with LEDs muted — pulse-duration tuning is stopped (see the
  boot-vs-runtime discrepancy note above and
  `docs/DRV8833_MOTOR_BRINGUP.md` section 16). The motor starts cleanly
  every time during the boot-time verification, which runs in a much
  quieter system state (section 17) — `MotorPriorityMode`'s `2` test
  exists to check whether recreating that state at runtime fixes it,
  before considering motor voltage, PWM, or mechanical changes. This
  supersedes the earlier, narrower "dead-stop-assist" observation below.
- The motor sometimes needs a brief manual assist to start from a dead
  stop; not attributed to a confirmed root cause (see
  `docs/DRV8833_MOTOR_BRINGUP.md` section 8 — UVLO is a plausible
  hypothesis, not a proven one).
- `motorBrake()` is implemented and was electrically validated during
  bring-up but is not yet used by any behavior.
- Motor engagement visibly disturbs LED output while both share the
  current power supply; `MotorPowerGuard` (above) is a bench-development
  workaround, not a fix — see Power below.

### Power

**Corrected 2026-08-01** — this section previously and incorrectly
stated the DRV8833 (and, elsewhere in this document, the MAX98357A) ran
from the ESP32's own 3.3V rail. The confirmed layout:

```text
Sunny power distribution:
- Shared 5V rail: WS2812B LEDs, MAX98357A amplifier, and DRV8833 motor driver
- 3.3V rail: INMP441 microphone
- Common ground across ESP32-S3 and all peripherals
```

ESP32 GPIO logic signals (I2S, buttons, LED data) remain 3.3V regardless
of a peripheral's own power rail — that's a separate, still-accurate
concern from the power rail itself.

The real electrical risk: the shared 5V supply, distribution wiring,
connectors, and grounding must support the combined peak load and noise
generated by the LEDs, amplifier, and motor. This is **not** claimed to
be a power-validated architecture — see
[Safety warnings](#safety-warnings) below and
`docs/SPEAKER_BRINGUP_PLAN.md`'s preflight list (supply type, rated
continuous/peak current, wire gauge, connector/current-path limits,
where the 5V rail branches to each load, bulk capacitance, local
decoupling near the amplifier and motor driver, motor-noise suppression,
and voltage measured at the amplifier during motor and LED activity) for
exactly what remains unmeasured before sustained or production use.

`MotorPowerGuard`'s LED muting is a **temporary software workaround** for
continued bench development, not a power fix — it reduces LED current
draw during motor engagement but does not increase available current or
address the underlying shared-supply contention. See
`docs/DRV8833_MOTOR_BRINGUP.md` section 14 for the physical validation
that identified this contention (documented there against the wiring
understood at the time; see the correction note above for the current
5V-rail understanding).

**`DanceEngine` (see below) does not use `MotorPowerGuard` and does not
mute LEDs at all** — by explicit design, LEDs stay exactly as the user set
them for the full duration of a dance session. This means sustained
80-100% PWM motor movement and full LED rendering can now draw from the
shared 5V rail simultaneously for as long as the dance continues, not
just a brief pulse. `IDLE_SWAY`, `ExpressiveMotion`, and the motor+LED
diagnostics are unaffected and still get `MotorPowerGuard`'s mitigation.
Whether this causes visible disturbance, brownout, or resets during real
dancing has not been physically observed.

### MusicMotorController (development branch)

**Revision 2** music-reactive motor behavior
(`include/MusicMotorController.h` / `src/MusicMotorController.cpp`), a
separate module from `DanceEngine` (per explicit instruction -- experimental
logic is not embedded into the choreographed dance engine):

```
AudioAnalyzer -> MusicMotorController -> MotorDriver / PWM motor primitives
```

Revision 1's first physical test swayed and reversed smoothly, but (a) did
not visibly respond to sustained song-intensity changes and (b) reversed
far too often instead of occasionally committing to one direction long
enough to look like a full rotation. Revision 2 separates three previously
conflated concepts: **sustained song intensity** (a slow EMA), **individual
beat/transient events** (a fast EMA compared to a baseline), and
**movement-phrase selection** (a deterministic, counter-based decision
among accent/reverse/hip-shake/spin).

```cpp
enum class MusicMotorState { OFF, SILENT, INTENSITY_SWAY, BASS_ACCENT, HIP_SHAKE, EXTENDED_SPIN, DECELERATING };
enum class MusicIntensityBand { QUIET, LOW, MEDIUM, HIGH, PEAK };
enum class MusicMotorBeatAction { NONE, ACCENT_CURRENT_DIRECTION, REVERSE_DIRECTION, START_HIP_SHAKE, START_EXTENDED_SPIN };
```

- **Speed scale:** expressed as 0-100 percent throughout (the same "M"
  scale as `MotorPwmCalibration`'s `m20`..`m100` and `DanceEngine`),
  converted to raw 8-bit PWM duty in exactly one place --
  `percentToMotorPwm()` in `MusicMotorController.cpp` -- never scattered.
- **Audio signal reused:** `AudioAnalyzer`'s existing
  `AudioFeatures.normalized` (already noise-floor-subtracted, 0..1) — no
  second microphone pipeline, no FFT/frequency-band analysis. `clap` is
  additionally treated as an automatic strong hit.
- **Triple independently-smoothed EMA** (every call to
  `updateMusicMotorController()`, not tick-gated): `fastEnergy`
  (`MUSIC_MOTOR_FAST_ATTACK`/`_RELEASE` -- fast attack, moderate release --
  tracks individual beats), `songEnergy` (`MUSIC_MOTOR_SONG_ATTACK`/
  `_RELEASE` -- slow attack, slow release -- tracks sustained section
  intensity), and `baselineEnergy` (a very slow unconditional EMA of
  `fastEnergy`, `MUSIC_MOTOR_BASELINE_ADAPT_RATE`, unchanged from
  revision 1 -- the "recent normal level" a transient stands out against).
  `transientDelta = max(0, fastEnergy - baselineEnergy)` feeds beat/strong-
  hit detection exactly as in revision 1.
- **Intensity bands** (`QUIET`/`LOW`/`MEDIUM`/`HIGH`/`PEAK`), classified
  from `songEnergy` with hysteresis (`MUSIC_MOTOR_INTENSITY_HYSTERESIS` --
  a band only drops out once energy falls that far below its own entry
  threshold, never merely at the threshold, so it doesn't flicker at a
  boundary). Each band maps to a motor-percent range (LOW 75-80, MEDIUM
  80-87, HIGH 88-95, PEAK 96-100) with **continuous linear interpolation**
  within the band based on where `songEnergy` sits between that band's own
  thresholds -- never an instant jump between bands.
- **Continuous speed response:** the live band-derived target
  (`intensityTargetPercent`) is recomputed every tick and approached via a
  non-blocking rate-based slew limiter
  (`MUSIC_MOTOR_SPEED_RISE_PERCENT_PER_SECOND` = 70,
  `MUSIC_MOTOR_SPEED_FALL_PERCENT_PER_SECOND` = 30) -- **except** it snaps
  instantly whenever starting from a dead stop (leaving `SILENT`, or
  resuming right after a reversal coast), matching the original
  physically-validated instant-start behavior and avoiding a ramp through
  weak, buzz-prone intermediate PWM values. `HIP_SHAKE`/`BASS_ACCENT`
  bursts and `DECELERATING`'s ramp all return to this **live** target, not
  a fixed value -- e.g. after a hip shake during a HIGH-intensity section,
  the motor settles back around M88-M95, not all the way down to slow
  sway. It only returns to M75-M80 once `songEnergy` has genuinely fallen
  into `LOW`.
- **Beat/strong-hit action selection** (`selectBeatAction()`) --
  **deterministic, never `random()`**: ordinary beats always
  `ACCENT_CURRENT_DIRECTION` (never reverse, never spin). Strong hits pick
  from `ACCENT_CURRENT_DIRECTION`/`REVERSE_DIRECTION`/`START_HIP_SHAKE`/
  `START_EXTENDED_SPIN` via a small per-band modular counter (incremented
  once per qualifying strong hit at that band), gated by the shared
  reversal gate and the spin cooldown/intensity-floor:
  - **LOW:** mostly accent; every 3rd qualifying strong hit attempts a
    safe reversal. Spin disabled (below the spin intensity floor).
  - **MEDIUM:** alternates reversal / hip-shake on successive strong hits.
    Spin still disabled (a deliberate reading of
    `MUSIC_MOTOR_SPIN_MIN_INTENSITY_LEVEL = HIGH`, see its `Config.h`
    comment).
  - **HIGH:** 3-way rotation among spin (when its cooldown allows) /
    reversal / hip-shake.
  - **PEAK:** **never reverses.** Alternates between an extended spin (when
    available) and a hip-shake burst, so consecutive strong hits reinforce
    the same direction with energetic movement instead of flipping back
    and forth -- this directly targets the "switched directions too much"
    physical-test finding.
- **`INTENSITY_SWAY`:** continuous movement tracking the live intensity
  target; only the `LOW` band retains revision 1's periodic, non-beat-
  driven direction change (`MUSIC_MOTOR_NORMAL_SWAY_MIN_MS`/`_MAX_MS`) --
  at `MEDIUM`+ periodic reversals stop firing on their own (direction
  changes there come from beat actions instead).
- **`BASS_ACCENT` → `HIP_SHAKE`:** unchanged shape from revision 1 --
  accelerate to the hip-shake target over `MUSIC_MOTOR_ACCEL_MS`, hold for
  a bounded-variation duration (`MUSIC_MOTOR_FAST_HOLD_MIN_MS`/`_MAX_MS`,
  capped overall by `MUSIC_MOTOR_HIP_SHAKE_MAX_TOTAL_MS`) -- strong hits
  mid-burst are now routed through the same `selectBeatAction()` used
  everywhere else instead of a separate ad hoc rule.
- **`EXTENDED_SPIN` (new):** a committed, **open-loop, time-based**
  one-direction rotation -- **there is no encoder or position sensor
  anywhere in this project**, so this is documented as an approximate
  extended rotation, never claimed as an exact 360°. Locks the *current*
  direction (does not force a fresh reversal first -- directly targets the
  "not enough sustained one-direction movement" finding), accelerates to
  an intensity-appropriate target (`MUSIC_MOTOR_SPIN_TARGET_MIN/MAX_PERCENT`,
  clamped to the live intensity target), ignores ordinary reversal
  requests for its duration (`MUSIC_MOTOR_SPIN_MIN_MS`..`_MAX_MS`, hard
  ceiling `MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS`), lets beats nudge the target
  slightly higher within that ceiling, and exits into `DECELERATING`
  (never instantly, never full-power-to-full-reverse). Gated by
  `MUSIC_MOTOR_SPIN_MIN_INTENSITY_LEVEL` and
  `MUSIC_MOTOR_SPIN_COOLDOWN_MS`; after any spin,
  `MUSIC_MOTOR_POST_SPIN_DIRECTION_HOLD_MS` blocks an immediate reversal.
  Obeys emergency stop and disable/takeover exactly like every other state.
- **`DECELERATING`:** ramps to whatever the *live* `intensityTargetPercent`
  is at the moment of entry (not a freshly-rolled slow-sway value), then
  hands off into `INTENSITY_SWAY`, which itself continuously re-derives
  its target every tick -- see the "continuous speed response" point above.
- **Reversal safety** (one shared gate, split into a read-only
  `checkReversalGate()` used by the beat-action selector and the mutating
  `tryRequestReversal()` used by every actual reversal, so nothing
  duplicates the DRV8833 coast-before-reversal sequence): refused unless
  `MUSIC_MOTOR_MIN_DIRECTION_HOLD_MS` has elapsed in the current direction,
  `MUSIC_MOTOR_REVERSAL_COOLDOWN_MS` has elapsed since the last accepted
  reversal, and `MUSIC_MOTOR_POST_SPIN_DIRECTION_HOLD_MS` has elapsed since
  the last spin ended. When granted: duty drops to 0, both GPIO8/GPIO9 go
  LOW, coasts `MUSIC_MOTOR_REVERSE_COAST_MS` (40ms), then the opposite
  direction drives. `REVERSAL_COAST` is **not** a distinct state value --
  it's reported as the visible movement-state name (via
  `reportedStateName()`) whenever a coast is in flight, regardless of which
  real state initiated it, to avoid re-deriving "which state to return to."
- **Non-blocking:** no `delay()` anywhere in this module; ramps are
  time-progress based (recomputed from a start time + duration every tick),
  so they're exact regardless of tick jitter; all timing uses `millis()`
  arithmetic that is wrap-safe.
- **LED power handling: none** -- like `DanceEngine`'s current
  (post-removal) design, this module never calls `MotorPowerGuard` and
  never reads or writes LED mute state at any point. LEDs and the audio
  LED overlay continue running completely normally.
- **Ownership/arbitration:** unchanged from revision 1 -- `musicmotor on`
  is refused while any motor diagnostic/`MotorPwmCalibration` test is
  active, preempts `IDLE_SWAY`, proactively stops `DanceEngine` (if active)
  and turns off `ExpressiveMotionMode::AUDIO_REACTIVE` *before* the
  diagnostic-refusal check, and `danceon` symmetrically stops
  `MusicMotorController` the same way.
- **Safety:** `k`/`mstop`/`musicmotor off` all coast the motor immediately,
  detach PWM, and cancel every pending ramp/hold/reversal/spin. Never
  automatically re-enables -- only an explicit `musicmotor on` resumes it.
- **Diagnostics:** a rate-limited (`MUSIC_MOTOR_DIAG_PRINT_INTERVAL_MS`)
  periodic line reporting `rawNormalized`/`fastEnergy`/`songEnergy`/
  `baselineEnergy`/`transientDelta`/`intensityBand`/`targetPercent`/
  `actualPercent`/`beat`/`strongHit`/`selectedAction`/`movementState`/
  `direction`/`spinRemainingMs`/`timeSinceLastSpin`/`timeSinceLastReverse`,
  plus immediate event logs for band transitions, beat-action selection,
  spin start/reinforcement/completion/rejection, and deceleration targets.
- **Temporary tuning commands** override the runtime copies of the
  corresponding `Config.h` defaults; values do not persist through reboot
  (see the serial-commands table below for the full list, including the
  new intensity-threshold and spin-timing commands).

**Not physically validated against the revision-2 changes** -- the
intensity-band mapping, beat-action selection, and extended-spin behavior
have been build/serial-smoke-tested but not yet confirmed to *look* right
(sustained-intensity response, spin duration/feel) on the real hardware;
that calibration is the next step.

### MusicMotorController revision 3 (physical calibration)

The motor has now been physically tested in **both directions**. Findings:

- **M80 is the validated minimum reliable movement command** -- below it,
  movement is not dependable on the real mechanism. Forward and reverse are
  effectively symmetric (same speed/timing either way).
- The mechanism does **not** wind up, naturally returns, and supports
  continuous rotation.
- Approximate observed rotation timing (current sunflower mechanical load
  -- **not** derivable from a linear PWM-percent formula; the response is
  nonlinear):

  | Command | Quarter turn | Half turn | Full turn |
  |---|---|---|---|
  | M80  | 1200ms | 2200ms | 3000ms |
  | M90  | 500ms  | 1000ms | 2000ms |
  | M100 | 250ms  | 500ms  | 1000ms |

**M80 active-movement floor** (`MUSIC_MOTOR_ACTIVE_MIN_PERCENT`): no active
LOW/MEDIUM/HIGH/PEAK/accent/reversal/hip-shake/spin action may intentionally
command below M80 -- only `BAND_QUIET` (M0) and deceleration genuinely
heading toward it may pass below the floor. Bands re-tiled contiguously
across M80-M100: `LOW` M80-83, `MEDIUM` M84-89, `HIGH` M90-96, `PEAK`
M97-100. Enforced by `clampTargetForBand()` (defensive clamp, unchanged
mechanism from the earlier target-invariant fix) plus the interpolation
itself, which is already bounded by construction.

**Sustained drop hold** (`dropHoldActive`/`dropHoldStartMs`/`dropHoldUntilMs`
in `MusicMotorController.cpp`) -- addresses movement weakening during the
actual bass drop because the adaptive transient baseline catches up to a
loud sustained section and `transientDelta` falls even though the drop is
still going. Starts/refreshes on a qualifying strong hit while the
*measured* band is `BAND_PEAK` and the song isn't quiet (initial duration
2200ms); a later qualifying hit extends it, capped at 4000ms total
continuous hold from when the session began. **A choreography-permission
signal only** -- `effectiveChoreographyBand()` is the one place it actually
changes a decision (lending `BAND_HIGH` eligibility to a `BAND_MEDIUM`
reading in `selectBeatAction()`); it never overwrites `intensityBand`
itself, so diagnostics/status always show the true measured band. Ends
immediately on silence, disable, emergency stop, or controller reset.

**Reverse hip shake** (`MusicMotorState::REVERSE_HIP_SHAKE`, a distinct
state from the original `HIP_SHAKE` burst) -- a fully non-blocking,
millis-based multi-phase action that locks in whatever direction was active
when it begins ("original direction") and always returns to it:

- **REGULAR**: opposite → original → opposite → original, M90, 200ms/phase,
  ~800ms total.
- **HEAVY** (major bass drops): 6 phases at M100, 180ms/phase, ~1080ms
  total.

A brief `MUSIC_MOTOR_REVERSE_COAST_MS` coast (same DRV8833 safety margin
every other reversal in this module uses -- both GPIO8/GPIO9 LOW) is
inserted between each phase, so actual total runtime is slightly longer
than the raw phase-count × phase-duration arithmetic ("approximately", as
specified). `currentDirection` itself is never reassigned mid-sequence; each
phase drives its own explicit direction, and status/diagnostics report the
*actually-driving* direction separately from the *original/resting*
direction during this state. Exits via the same shared `DECELERATING` ramp
every other action uses (no duplicated wind-down logic).

Integrated into `selectBeatAction()`: HIGH gets an occasional REGULAR
variant (1-in-4 modular slot); PEAK gets both variants (REGULAR and HEAVY,
each a 1-in-3 slot) and, per the existing PEAK design, still never returns
a plain `REVERSE_DIRECTION`. A shared hip-shake-start cooldown
(`MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MIN/MAX_MS`, randomized 800-1200ms)
covers both `HIP_SHAKE` and `REVERSE_HIP_SHAKE`, so neither can retrigger
the other back-to-back. Cannot interrupt an active `HIP_SHAKE`/
`REVERSE_HIP_SHAKE`, a reversal coast, or `EXTENDED_SPIN` (that state
never re-evaluates action selection at all, preserving its existing
"committed, not interruptible" behavior).

**Wobble cue** -- a lightweight modulation detector for sustained bass
tone-shifts that produce high `songEnergy` but low `transientDelta` (no
sharp attack to trigger the ordinary beat/strong-hit path). Tracks the
absolute tick-to-tick change in `fastEnergy`, thresholded
(`MUSIC_MOTOR_WOBBLE_DELTA_THRESHOLD`, default 0.10) and debounced
(`MUSIC_MOTOR_WOBBLE_REFRACTORY_MS`, default 300ms) -- **not** a new
frequency-analysis subsystem, just the existing processed signal. Only
evaluated while `dropHoldActive` and the measured band is MEDIUM/HIGH/PEAK.
Used purely as an eligibility signal for an occasional regular reverse hip
shake or accent (deterministic alternation, not random).

**Calibrated spin durations** -- replaces the earlier randomized
700-1800ms range (not tied to any physical measurement) with three named
profiles picked by context:

| Profile | Trigger | Speed | Duration | ~Rotations |
|---|---|---|---|---|
| NORMAL | HIGH-band spin | M90 | ~2000ms | 1 |
| FAST | PEAK-band spin | M100 | ~1000ms | 1 |
| EXTENDED_DROP | spin while `dropHoldActive` | M100 | 2000-3000ms | 2-3 |

Hard ceiling raised from 2200ms to `MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS =
4000ms` so it no longer clips `EXTENDED_DROP`. `musicmotor spintime <ms>`
still works as a manual override (forces M100 for the given duration, for
calibration) and takes priority over the automatic profile selection.

**New runtime command:** `musicmotor motion` -- prints the full physical-
calibration-derived tuning surface (active-movement floor, band ranges,
drop-hold durations, wobble threshold/refractory, hip-shake cooldown,
regular/heavy reverse-hip-shake PWM/timing/phase counts, spin profiles, and
the calibration table itself). See the serial-commands table below.

**New diagnostic fields** (periodic line): `dropHold=0/1`,
`dropHoldRemainingMs`, `wobbleCue=0/1`, `hipShakePhase=<n>/<total>`. New
event logs: `drop hold started/refreshed/expired/cancelled`, `reverse hip
shake started variant=REGULAR|HEAVY originalDirection=F|R`, `reverse hip
shake completed returnDirection=F|R`.

**Scope preserved unchanged:** MotorPowerGuard is never called, LED mute
state is never touched, the triple-EMA (`fastEnergy`/`songEnergy`/
`baselineEnergy`) structure is untouched, no `delay()` anywhere, no FFT/
frequency-analysis subsystem added, GPIO assignments and button mappings
untouched.

**Same-song tuning procedure:** run `musicmotor on`, then during a
listening pass, capture these serial lines at each phase:

1. **Buildup** -- watch `intensityBand=BAND_LOW`/`BAND_MEDIUM` lines and
   `intensityTarget=` climbing smoothly; confirm no `beat action=` spam
   (buildup should mostly show `ACCENT_CURRENT_DIRECTION`, not repeated
   reversals/hip-shakes).
2. **Initial bass drop** -- watch for `intensity ... -> BAND_PEAK`
   immediately followed by `[MUSIC MOTOR] drop hold started duration=2200ms`
   and a `beat action=` showing `HIP_SHAKE`/`REVERSE_HIP_SHAKE`/
   `EXTENDED_SPIN` (not a weak accent).
3. **Sustained wubby section** -- watch `dropHold=1` persist in the
   periodic line (refreshed via `drop hold refreshed remaining=...`) and
   `wobbleCue=1` ticks correlating with audible tone modulation, each
   occasionally producing `reverse hip shake started variant=REGULAR`.
4. **Quiet breakdown** -- watch `intensity ... -> BAND_LOW`/`BAND_QUIET`
   and `drop hold expired reason=timeout_expired` (or `cancelled
   reason=silence_cancel` if it goes fully quiet); confirm the motor
   visibly slows to M80-83 or stops rather than continuing to thrash.
5. **Next drop** -- repeat step 2; run `musicmotor status` and
   `musicmotor motion` between songs to confirm no state (drop hold,
   cooldowns, spin) is stuck from the previous section.

### MusicMotorController revision 4 (detailed decision diagnostics)

The first physical song test showed good back-and-forth movement but
**no observed `BAND_PEAK`, no `dropHold=1`, no `REVERSE_HIP_SHAKE`, and no
sustained/extended spin** -- the captured log showed `intensityBand`
staying at `BAND_LOW`/`BAND_MEDIUM` with `selectedAction=
ACCENT_CURRENT_DIRECTION` throughout. Revision 4 does **not** retune any
threshold to fix this -- it adds detailed, opt-in diagnostics that expose
*exactly* why the drop-hold/high-energy choreography conditions were being
rejected, so the next physical pass can pinpoint the real cause (most
likely: `songEnergy` for that recording never actually crossed
`PEAK_THRESHOLD`) before anything is retuned.

**Commands** (all under the existing `musicmotor` word command, no new
parser prefix, no single-character collisions):

```text
musicmotor debug on       enable detailed decision diagnostics
musicmotor debug off      disable them (ordinary status/event output is unaffected)
musicmotor debug status   report whether detailed diagnostics are currently enabled
```

Default is **OFF** at boot, matching this project's convention for
diagnostic-only features (`MotorPwmCalibration`, DanceEngine's `dancetest`,
etc.). Debug mode **persists across `musicmotor on`/`off`** within a
session (it is only reset to OFF at true boot in
`initMusicMotorController()`) so a listening pass doesn't require
re-enabling it after every stop/start. `musicmotor status` and `musicmotor
motion` both report the current debug state.

**This is diagnostic-only.** Every new log line is produced by a function
that takes no debug-flag input for its DECISION (`computeDropHoldDecision()`/
`computeStrongHitReason()`) -- `debugLoggingEnabled` only gates whether the
line is *printed*, never what the controller actually does. Enabling or
disabling `musicmotor debug` cannot change movement behavior.

**New diagnostic lines** (only printed while debug is ON):

- `dropHold evaluation` -- printed every tick with a rate-limited-unless-
  changed rejection reason, or immediately on every `STARTED`/`REFRESHED`:
  ```text
  [MUSIC MOTOR] dropHold evaluation measuredBand=BAND_MEDIUM effectiveBand=BAND_MEDIUM raw=0.31 fast=0.29 song=0.24 baseline=0.20 transient=0.09 strongHit=0 beat=1 active=0 remainingMs=0 qualifyingBand=0 qualifyingHit=0 result=REJECTED reason=band_not_peak
  ```
- `band evaluation` -- printed on an actual band transition, or when the
  hysteresis-free raw candidate band differs from the real hysteresis-aware
  result (rate-limited unless the specific candidate→resulting pair changes):
  ```text
  [MUSIC MOTOR] band evaluation previous=BAND_MEDIUM candidate=BAND_HIGH resulting=BAND_MEDIUM song=0.31 fast=0.42 baseline=0.29 transient=0.13 entryThreshold=0.48 exitThreshold=0.44 hysteresisBlocked=1
  ```
- `strongHit evaluation` -- printed on every qualified strong hit, rate-
  limited otherwise:
  ```text
  [MUSIC MOTOR] strongHit evaluation beat=1 raw=0.55 fast=0.58 previousFast=0.31 fastDelta=0.27 song=0.47 baseline=0.31 transient=0.27 strongHitThreshold=0.28 strongHit=1 reason=qualified
  ```
- `choreography` -- printed on every non-default action selection, rate-
  limited for the common ordinary-beat-accent case:
  ```text
  [MUSIC MOTOR] choreography measuredBand=BAND_HIGH effectiveBand=BAND_HIGH dropHold=0 wobbleCue=0 selectedAction=REVERSE_HIP_SHAKE reason=high_slot3_reverse_hipshake_regular spinCooldownReady=1 reverseCooldownReady=1 timeSinceLastSpin=8420 timeSinceLastReverse=1240
  ```

**Exact drop-hold gating** (as implemented, not as originally suggested --
see the final report for the two suggested-but-unused reason labels):
`intensityBand == BAND_PEAK && strongHitDetectedThisTick && state !=
SILENT`, then (if already active) a refresh only if the new deadline would
actually extend past the current one, capped at 4000ms total continuous
hold. Reason vocabulary: `band_not_peak`, `beat_not_detected`,
`strong_hit_false`, `start_qualified`, `refresh_qualified`,
`already_active_no_refresh`; cancellation reasons `silence_cancel`,
`disabled_cancel`, `emergency_stop_cancel`; expiry reason
`timeout_expired`.

**Recommended physical test sequence:**

```text
musicmotor on
musicmotor debug on
musicmotor motion
musicmotor status
```

Then play the fixed reference song from the beginning and capture at least:
buildup, first major drop, sustained bass section, quiet breakdown, second
major drop. The single most important thing to check: does `intensityBand`
(in the `dropHold evaluation`/periodic diagnostic lines) ever actually
reach `BAND_PEAK` during the drop? If not, `dropHold evaluation ...
reason=band_not_peak` will explain the entire chain of symptoms (no drop
hold, no `REVERSE_HIP_SHAKE`, no sustained spin) without needing to guess.

### MusicMotorController revision 8 (renewable performance phrases + lifelike silence)

Revision 7 added `SUSTAINED_DRIVE`: an occasional, weighted (not a rigid
timer), committed continuous FORWARD/REVERSE hold for a **randomized fixed
5-10 second duration**, so the sunflower would occasionally commit to a
direction instead of reacting to every beat. Revision 8 keeps that same
`MusicMotorState::SUSTAINED_DRIVE` state, entry-eligibility system, HIGH-tier
intensity floor, and interruption-resistance guarantees, and evolves the
**duration model** from one fixed roll into a **renewable, music-driven
performance phrase** -- plus adds a **lifelike silence/low-energy stopping**
system that replaces most abrupt stops with a gradual, natural-feeling
wind-down.

#### From a fixed duration to a renewable phrase

Instead of rolling one 5-10s duration at entry, a phrase now has:

1. **A minimum directional commitment** (~5000ms, `MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS`)
   -- during this window nothing (ordinary beats, the LOW-band periodic
   reversal timer, ordinary choreography, a brief band dip) can interrupt or
   switch the phrase; only emergency stop/disable/hardware safety/genuine
   silence can.
2. **An initial review point**, sized by the effective band at entry:

   | Effective band at entry | Review range |
   |---|---|
   | MEDIUM | 5,000-10,000ms |
   | HIGH | 8,000-18,000ms |
   | PEAK or active DropHold | 12,000-30,000ms |

   Reviews are **evaluation points, not forced exits**.
3. **Extensions** -- at each review, if the music still supports it (see
   below), the phrase schedules another review, sized by the effective band
   *at that review* (not the entry band, so a phrase can move between
   ranges as the song itself moves between sections):

   | Effective band at review | Extension range |
   |---|---|
   | MEDIUM | 3,000-7,000ms |
   | HIGH | 5,000-12,000ms |
   | PEAK or active DropHold | 8,000-18,000ms |

   There is **no arbitrary hard cap**. A phrase that keeps getting extended
   by sustained HIGH/PEAK energy can run past 30s, then past 60s, indefinitely
   -- it's the same mechanism the whole way, just kept alive by real,
   continuing energy. Once total elapsed time crosses 15s/30s it's reported
   as `EXTENDED`/`RENEWABLE` in diagnostics (label only, no behavior change).
4. **A single decision function**, `computeSustainedDriveContinuationDecision()`,
   decides what happens at each review (or on a qualifying strong-hit switch
   opportunity in between reviews): `EXTEND_SAME_DIRECTION`,
   `SWITCH_SUSTAINED_DIRECTION`, `EXIT_TO_NORMAL`, or the defensive
   `EXIT_FOR_SILENCE`/`EXIT_FOR_SAFETY` outcomes (the real silence/safety
   paths preempt the phrase directly and never actually reach this
   function). A review with real musical support **always** extends -- a
   phrase never exits "solely because the review timer expired."

#### Low-energy grace (short dips are tolerated)

A phrase does not exit the instant `effectiveBand()` dips below MEDIUM.
`sustainedDriveLowEnergySinceMs` starts tracking the dip; if it climbs back
to MEDIUM+ before `MUSIC_MOTOR_SUSTAINED_DRIVE_LOW_GRACE_MS` (~3.5s) elapses,
the grace timer simply resets. Only a genuinely *sustained* LOW ends the
phrase this way. Genuine QUIET/silence is a completely separate, shorter
path -- see "lifelike silence" below; the two never compete or double-count.

#### Direct sustained-direction switching

A phrase can switch FORWARD↔REVERSE **without ever leaving
`SUSTAINED_DRIVE`** -- "the phrase remains continuous even though its
direction changed." Evaluated only on a **qualified strong hit** (never an
ordinary beat) once past the current direction segment's own minimum
commitment, and requires a strong musical accent (real `BAND_PEAK` or an
active drop hold) plus a switch cooldown (~8-15s,
`MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MIN/MAX_MS`) being ready. An
**exceptional** path (real PEAK **and** an active drop hold together) may
bypass the ordinary cooldown, but never bypasses a ~5.5s floor
(`MUSIC_MOTOR_SUSTAINED_SWITCH_EXCEPTIONAL_MIN_MS`) measured from the
current direction segment's own start -- "shortly after the initial
commitment," never immediately at it. Even once qualified, only a 35% roll
(the same `MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT` revision 7
already used for entry-time flips) actually commits to the switch --
continuation stays the common outcome.

The switch itself uses the exact same safe coast→flip primitives as every
other reversal in this file (never an instantaneous polarity change). After
it completes: the phrase stays in `SUSTAINED_DRIVE`, the new direction gets
a **fresh** minimum commitment, total phrase elapsed time keeps counting
from the original entry (unaffected), the switch count increments, and a
**new** review point is scheduled -- but the phrase's own re-entry cooldown
(see below) is **not** touched, since this was never a phrase exit.

#### Persistent-energy entry (no fresh strong hit required)

Revision 7 could only start a phrase on a qualifying strong hit. Some
sustained HIGH/PEAK songs don't produce frequent sharp transients, so
revision 8 adds a second, independently rate-limited entry opportunity,
evaluated from `INTENSITY_SWAY` on ticks with no beat/strongHit/wobble event:
once `intensityBand` has stayed continuously HIGH/PEAK for at least
`MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_DWELL_MS` (~4s -- reusing
revision 5's existing `sustainedHighSinceMs` dwell timer, not a new one),
the *same* entry-weight table (MEDIUM 3% / HIGH 12% / PEAK 22%) is rolled
against, but **at most once every**
`MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_REVIEW_MS` (~2.5s) -- "a
percentage is meaningless if rolled hundreds of times per second." A failed
roll never touches the ordinary modular choreography counters.

#### Phrase tiers -- SHORT, STANDARD, EXTENDED, RENEWABLE

Every new phrase first picks a **tier**. Most of the description above is
the "long-form" path (labeled `STANDARD`, then `EXTENDED` past 15s elapsed,
then `RENEWABLE` past 30s elapsed -- reporting labels only). The other
option is **`SHORT`**: a brief, *intentional* 1,000-5,000ms committed burst
(`MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MIN/MAX_MS`) -- a quick directional
accent, not a failed long attempt and not an ordinary beat pulse. A SHORT
phrase uses **its own** commitment instead of the 5s floor, and its one
review coincides with that commitment ending.

Tier is chosen by a weighted roll (`chooseSustainedDriveEntryTier()`):
MEDIUM favors SHORT (70%), HIGH mixes (40%), PEAK favors long-form (20%
short) -- halved further when a drop hold is active (favor long-form even
more during a confirmed drop), and boosted +25% on a major transient (a
transient at least 1.5x the ordinary strong-hit threshold), so "a major
transient may trigger a 1-5 second directional burst even when the
surrounding energy does not justify a long phrase."

When a SHORT phrase's commitment ends, if the music still qualifies for
extension or a switch, an **additional** promotion roll
(`MUSIC_MOTOR_SUSTAINED_DRIVE_PROMOTION_PERCENT`, 30%) decides whether it
actually promotes into a longer phrase (tier becomes `STANDARD` from that
point on) or exits normally -- "avoid turning every short phrase into a
long phrase" / "short phrases do not always promote."

#### Lifelike silence and low-energy stopping

Most genuine musical endings should look like the sunflower naturally
running out of energy, not an abrupt cut. `checkAndHandleSilenceTimeout()`
-- the same shared, unchanged 7-second silence hysteresis/timeout every
prior revision already used, still the sole authority on "is this genuinely
silence" -- now calls `beginMusicalSilenceStop()` instead of stopping
instantly. That function chooses a **stop style**:

- **`GRADUAL_RAMP_DOWN`** (the normal case, ~75-90% of the time) -- a new
  `MusicMotorState::MUSICAL_RAMP_DOWN` state ramps `currentSpeedPercent`
  toward exactly 0 over 1-2s (movement was already modest) or 2-4s (it was
  high-energy, e.g. mid-`SUSTAINED_DRIVE`), reusing the same shared ramp
  fields/`applyRampTick()` every other ramp in this file already uses.
  Direction is never touched. If real music returns before the ramp
  finishes, it's cancelled and normal choreography resumes smoothly from
  wherever the speed had already fallen to -- no stop-and-restart. Once the
  ramp completes, it finishes through the same `stopCleanly()` every other
  stop path uses, guaranteeing a real electrical stop.
- **`DRAMATIC_ABRUPT_STOP`** (occasional, ~10-25%) -- an immediate stop, no
  ramp. The chance depends on real musical evidence, not a flat coin flip: a
  drop hold active within the last 3 seconds
  (`MUSIC_MOTOR_SHARP_CUTOFF_DROPHOLD_RECENCY_MS`) -- "a major drop followed
  by immediate silence" -- raises the abrupt chance from 12% to 25%
  (`MUSIC_MOTOR_ABRUPT_STOP_NORMAL/SHARP_CUTOFF_PERCENT`). An ordinary slow
  fade strongly favors gradual either way.

**This never weakens safety.** Emergency stop, the `k` command, `musicmotor
off`, and any hardware-safety path all call `hardStop()` directly and
completely bypass `beginMusicalSilenceStop()`/`MUSICAL_RAMP_DOWN` --
identical to how they already bypassed every other choreography state.
`SUSTAINED_DRIVE`'s own confirmed-silence exit uses this exact same shared
path (still called first, unconditionally, in `updateSustainedDrive()`) --
it never waits for its own review/extension timer, and the HIGH-tier
intensity floor stops applying the instant the state changes away from
`SUSTAINED_DRIVE`, exactly like a normal phrase exit.

#### New/changed configuration (`include/Config.h`)

All under the existing "Revision 7"/"Revision 8" `MUSIC_MOTOR_SUSTAINED_*`
naming; every constant has an in-code rationale comment.
`MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_MS`/`_MAX_MS` (the old fixed 5-10s roll)
were removed -- replaced by the tier/review/extension ranges above. Entry
weights (`_WEIGHT_MEDIUM/HIGH/PEAK_PERCENT`), the accent-flip percent, the
re-entry cooldown range, and the cross-phrase consecutive-same-direction
cap are all unchanged from revision 7.

#### Diagnostics and commands

`musicmotor status` now additionally reports: phrase tier, phrase elapsed
time, minimum-commitment remaining, next-review remaining, extension
count/last extension reason, low-energy grace state, sustained-switch
eligibility/cooldown/count, last phrase exit reason, re-entry cooldown
remaining, and (while ramping) the active stop style/progress/remaining
time. `musicmotor motion` documents every phrase-tier/review/extension/
switch/ramp-down range and weight. With `musicmotor debug on`, new
rate-limited lines cover the review/continuation decision, switch
qualification, SHORT-phrase promotion rolls, and stop-style selection --
plus unconditional lines for phrase start/extend/switch/exit and ramp-down
start/cancel/complete.

**Recommended physical test sequence:**

```text
musicmotor on
musicmotor debug on
musicmotor motion
musicmotor status
```

Then play a song with a long sustained high-energy section (a chorus or
extended drop) and confirm: a `SUSTAINED_DRIVE` phrase can visibly run well
past 30 seconds via repeated `sustained drive extended ...` log lines
(`extensionCount` climbing in `musicmotor status`); an occasional direct
`sustained ... -> sustained ...` direction switch appears during a strongly
energetic passage without ever leaving `SUSTAINED_DRIVE`; a brief quiet
passage (a few seconds) does *not* end an active phrase; letting the song
actually end shows a gradual `musical ramp-down started ...` rather than an
instant stop in most cases, with direction unchanged throughout and a clean
stop at the end; and `k`/`musicmotor off` still stop instantly regardless of
what state the controller was in.

### MusicMotorController revision 9 (relative/song-adaptive EDM/dubstep drop detection)

Revisions 5-8's `MusicIntensityBand` (QUIET/LOW/MEDIUM/HIGH/PEAK) is an
**absolute** classifier: it compares the current instant against fixed,
song-independent thresholds. That is by design, but it means a section that
is dramatically more intense than the *rest of the same song* can still only
ever read as MEDIUM if it never crosses the fixed HIGH threshold -- which is
exactly how EDM/dubstep drops are often shaped (a filtered/quiet buildup
resolving into a full-spectrum section that is loud *for that song* without
necessarily being absolutely loud). Revision 9 adds a second, **relative**
layer alongside (never replacing) the absolute one: `MusicalSectionPhase`
(`NEUTRAL -> BUILDUP -> DROP_ARMED -> DROP_IMPACT -> DROP_ACTIVE ->
DROP_RELEASE`), driven by a multi-signal `dropConfidence` score (energy rise
and section contrast vs. rolling song references, bass-impact/density,
beat/transient density, buildup-resolution, and sustained `performanceEnergy`
-- see `MUSIC_MOTOR_EDM_WEIGHT_*` in `Config.h`) classified into
`POSSIBLE_DROP`/`CONFIRMED_DROP`/`MAJOR_DROP` tiers. A `CONFIRMED_DROP`/
`MAJOR_DROP` that stays `DROP_ACTIVE` gets an escalating (40% -> 70% ->
guaranteed) `SUSTAINED_DRIVE` entry opportunity even with no fresh absolute
strong hit, and selects one of three centrally-configured speed floors
(`NORMAL`=M90, `PERFORMANCE`=M97, `PEAK`=M100 -- reusing the already-validated
HIGH/PEAK breakpoints, not new speed values). Entirely additive and
toggleable at runtime for A/B comparison: `musicmotor dropdetect on|off`.
See `musicmotor status`/`musicmotor summary` for live phase/confidence/tier
diagnostics and session statistics, and `musicmotor motion` for the full
config surface. Bass-impact scoring reuses `AudioFeatures.lowFrequencyEnergy`
(a single-pole low-pass RMS proxy, **not** true FFT bass); no tone-shift/
spectral-flux signal exists yet in this firmware, so no tone-change weight is
included in the confidence model (documented as future work, not
approximated). A firmware identity banner (`FIRMWARE_REVISION_TAG`/build
timestamp) now prints once at boot and appears in `musicmotor status`/
`musicmotor summary`, so a captured serial log can always be matched back to
the exact firmware that produced it.

### MusicMotorController revision 10 (physical choreography and dynamic-range refinement)

The first successful Revision 9 physical drop test surfaced two problems:
mellow/quiet sections kept moving too fast (measured intensity fell to
QUIET/LOW/MEDIUM but `performanceEnergy` decayed slowly, so the *lent*
effective band kept the motor target near M88-M93), and major drops mostly
showed as repeated `ACCENT_CURRENT_DIRECTION` inside plain
`INTENSITY_SWAY` rather than decisive `SUSTAINED_DRIVE` choreography.
Revision 10 fixes both structurally, without touching Revision 9's drop
*detection* (confidence scoring/phase machine are unmodified):

- **Speed-authority cap ("bounded lending")** -- a new
  `computeSpeedAuthorityCap()` bounds how far historical
  performanceEnergy/dropHold/relative-drop memory may raise the *commanded
  speed*, keyed to the CURRENT MEASURED band (QUIET gets a brief grace
  window then caps hard; LOW caps immediately; MEDIUM may only be raised by
  one bounded amount; HIGH/PEAK keep full authority). Applied both to the
  live `intensityTargetPercent` and to the `SUSTAINED_DRIVE` speed floor, so
  even an actively-DROP_ACTIVE phrase now visibly dips when the *current*
  audio genuinely collapses, while the drop's logical state can still
  survive a short gap.
- **Motion palette + duty cycle** -- a `MotionTier` (REST/QUIET_BUILDUP/
  MELLOW/GROOVE/HIGH_ENERGY/CONFIRMED_DROP_DRIVE/MAJOR_DROP_DRIVE)
  classifies each tick's choreography role. Since this hardware has never
  validated reliable continuous rotation below M80, "much slower" MELLOW/
  QUIET_BUILDUP motion comes from a pulse/rest **duty cycle** (brief drive,
  longer coast) rather than any new low-PWM value.
- **QUIET_BUILDUP "alive" behavior** -- a restrained subtle sway during a
  genuine quiet musical buildup (Revision 9's own BUILDUP/DROP_ARMED phase
  + audio above a calibrated room-noise floor), explicitly distinct from an
  empty silent room, which still rests.
- **Faster, more decisive drop entry** -- `MAJOR_DROP` gets a near-immediate
  high entry chance (guaranteed once effectiveBand is already >=MEDIUM);
  `CONFIRMED_DROP`'s escalation ladder now guarantees sooner.
- **Drop choreography phrase vocabulary** -- `SUSTAINED_DRIVE` no longer
  always means one committed direction: `FULL_SUSTAIN`,
  `SUSTAINED_REVERSAL`, `DROP_BOOTY_SHAKE`, `DROP_PUNCH_AND_HOLD`,
  `DOUBLE_PUNCH`, and `SUSTAIN_WITH_ACCENTS` are selected by an
  evidence-weighted (not random-first), per-drop-limited, non-blocking step
  sequencer (`selectDropPhraseType()`/`buildDropPhraseSteps()`/
  `advanceDropPhraseStepSequencer()`), with every direction change going
  through a ramp-down/coast/restart/ramp-up sequence -- never an
  instantaneous full-forward-to-full-reverse command.

New commands: `musicmotor dynamics status`, `musicmotor quietmotion on|off`,
`musicmotor switchchance/switchcooldown/switchlimit <value>`. See
`musicmotor status`/`musicmotor summary` for live motion-tier/speed-authority/
drop-phrase diagnostics and session statistics.

#### Revision 10.1 -- frozen-state regression fix

The first physical test of Revision 10's drop-phrase vocabulary found a
genuine deadlock: `SUSTAINED_DRIVE` could get permanently stuck at M0 while
repeatedly logging a punch step it never actually applied. Root cause:
`advanceDropPhraseStepSequencer()`'s direction-change check compared the
current step against the *previous step's fixed array value* instead of the
live `currentDirection` -- for any step index beyond the first, that
comparison can never resolve once the motor has already flipped to match
it, so the sequencer loops `DECEL -> COAST -> DECEL` forever. Fixed by
comparing against `currentDirection` directly (self-correcting: once the
motor matches the step's direction, the check naturally passes). A
defense-in-depth invariant (`checkSustainedDriveInvariant()`) now also
detects and recovers from this *class* of stuck state (falls back to
`FULL_SUSTAIN`, or exits `SUSTAINED_DRIVE` if a restart isn't currently
safe) should anything similar ever regress. Separately, quiet-buildup
motion was found to be silently inert: `updateSilent()`'s early-return only
checked the absolute QUIET band (never the buildup qualification), and even
outside `SILENT`, the pulse window never supplied a nonzero target -- both
fixed. New command: `musicmotor test` (one-command physical-validation
setup: enables MusicMotor + drop detection + debug logging + quiet-buildup
motion, resets summary counters, prints current config) and
`musicmotor test stop` (prints final summary, disables debug logging, stops
safely).

#### Diagnostic-label follow-up (post-10.1 validation)

Physical validation of Revision 10.1 surfaced one further diagnostic-only
issue (no motor-control logic was affected): a single-tick logging-order
artifact where a legitimate zero-speed direction handoff (the tick a
reversal coast resolves, or a drop-phrase coast resolves) was mislabeled
`intentionalStopReason=UNKNOWN_POSSIBLE_INVARIANT_VIOLATION`, because the
direction/phase-clearing mutation happens one tick before the resulting
drive is actually applied. Root cause traced and fixed:

- Two new precise labels: `deceleration_handoff` (the general
  `MusicMotorState::DECELERATING` ramp-toward-live-target state) and
  `direction_change_handoff` (`directionStartMs == now` -- the exact tick a
  coast resolves). `UNKNOWN_POSSIBLE_INVARIANT_VIOLATION` remains available
  and unsuppressed for genuinely unexplained stalls.
- A separately-confirmed real bug: `exitSustainedDrive()` never reset
  `sustainedDriveLowEnergySinceMs`, so `musicmotor status`'s "Low-energy
  grace" could show stale `ACTIVE` (with an ever-growing elapsed time)
  indefinitely after a phrase had already exited. Fixed by resetting it on
  exit.

Covered by `test_host/music_motor_diagnostic_handoff.cpp`.

### Known limitations and deferred work (as of Revision 10.1)

- **Boot takes ~30-40s before `musicmotor`/other serial commands are
  processed.** `HardwareTest` (LED sequence + ~20s of mic Peak/RMS
  printing) and `MicRetest` (5 phases x 6s = 30s of mic diagnostics) both
  run unconditionally and **block** at the end of `setup()`, before
  `loop()` (and therefore the serial command parser) ever starts -- see
  "BEGIN/END HARDWARE TEST" and "BEGIN/END MIC RETEST" in `src/main.cpp`.
  This is intentional bring-up tooling, not a bug, but it directly affects
  physical testing: use `musicmotor test` once the prompt is responsive
  rather than assuming commands sent immediately after reset were
  received. `MicRetest` can be disabled without deleting anything by
  flipping `ENABLE_MIC_RETEST` to `0` in `include/MicRetest.h`; both
  modules document their own full removal instructions in their headers.
- No true FFT/spectral bass or tone-shift detection -- the bass-impact
  signal is a single-pole low-pass RMS proxy (`AudioFeatures.lowFrequencyEnergy`),
  not a real frequency-band split. Documented inline in `Config.h`/the
  Revision 9 section above.
- No thermal or continuous-run current limit for the motor -- this
  hardware (DRV8833, brushed DC motor) has no temperature sensor to
  enforce one against.
- `DanceEngine` (the earlier V1 mic-driven choreography engine) and
  `MusicMotorController` are both still present and both user-selectable
  (`danceon` / `musicmotor on`) -- they are mutually exclusive at runtime
  via the shared `isAnyMotorDiagnosticActive()` ownership arbitration
  (`src/main.cpp`), not merged into one engine. `MusicMotorController` is
  the actively-developed, physically-validated system as of this
  milestone.
- The Revision 10 drop-phrase vocabulary (`FULL_SUSTAIN`/`SUSTAINED_REVERSAL`/
  `DROP_BOOTY_SHAKE`/`DROP_PUNCH_AND_HOLD`/`DOUBLE_PUNCH`/`SUSTAIN_WITH_ACCENTS`)
  and the Revision 9 relative-drop detector are host-tested and were
  physically validated in the Revision 10.1 pass described above; further
  physical A/B tuning against a wider range of songs/genres remains future
  work (the genre-profile architecture supports adding profiles beyond the
  current `EDM_DUBSTEP` default without restructuring the detector).

## Expressive motion (development branch)

**Development-branch feature** (`feature/expressive-motion-v1`, branched
from `v1.0.0`) — coordinates gentle motor movement with the existing LED
effects and audio-reactive overlay. Disabled by default; does not alter
any `v1.0.0` behavior when off. Full architecture, timing, and physical
validation checklist: `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`.

```cpp
enum class ExpressiveMotionMode { OFF, IDLE_ALIVE, AUDIO_REACTIVE };
```

Movement is built from eleven named, non-blocking **patterns**
(`GENTLE_SWAY`, `MEDIUM_SWAY`, `LONG_LEAN`, `DOUBLE_TWITCH`,
`FORWARD_REVERSE_NOD`, `EXCITED_TRIPLE`, `DRAMATIC_SWEEP`, plus
audio-specific `AUDIO_ACTIVE_PULSE`/`AUDIO_STRONG_BURST`/
`AUDIO_CLAP_RECOIL`/`SETTLE`) rather than a single repeating pulse — see
`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 4 for the full
architecture and every pattern's exact step sequence.

- **IDLE_ALIVE:** weighted-random selection among the seven idle patterns
  (25% gentle sway down to 7% dramatic sweep — the full weighting table is
  in the doc above), separated by a randomized rest (600–2200ms, or
  occasionally 2500–5000ms); never more than 2 consecutive same-direction
  pulses, enforced globally.
- **AUDIO_REACTIVE:** the same idle-pattern behavior while quiet
  (including an occasional slow `SETTLE` movement after recent activity);
  a rising edge into the ACTIVE or STRONG band, or a clap, each
  independently cooldown-gated, triggers a matching reaction (a single
  pulse or two-pulse "nod" for ACTIVE, a two/three-pulse burst for STRONG,
  a sharp pulse + recoil for a clap); several ACTIVE events in a bounded
  window occasionally get a livelier grouped reaction instead of identical
  single pulses. A sustained loud sound produces intermittent movement,
  never continuous motor power.
- Both modes reuse `MotorPowerGuard`'s existing `DIM_DURING_MOTION` mode
  (same one the `5` diagnostic uses) for LED coexistence — the current
  base effect keeps animating at the selected motion brightness (`4`),
  never frozen or replaced.
- **`AUDIO_REACTIVE` vs. `DanceEngine`:** these are two independent
  audio-to-motor implementations and are never allowed to drive the motor
  at the same time — see
  [Dance Engine V1](#dance-engine-v1-superseded----disabled-by-default)
  (disabled by default; this mutual-exclusion logic only matters in a
  build with `ENABLE_LEGACY_DANCE_ENGINE=1`). Note: Button 4's long-hold
  gesture no longer drives `AUDIO_REACTIVE` — it drives the unified
  [Audio Mode](#audio-mode-unified) instead (LED overlay +
  `MusicMotorController`). `AUDIO_REACTIVE` remains fully available via
  `motion audio`, just without a physical-button binding.
  `danceon` turns `AUDIO_REACTIVE` off first if it was selected, and
  `isAnyMotorDiagnosticActive()` (which `AUDIO_REACTIVE`'s own pattern
  selection already checks before starting a movement) reports true the
  whole time `DanceEngine` is enabled, so `AUDIO_REACTIVE`'s direct motor
  pulses simply never fire while `DanceEngine` owns the motor. Nothing
  about `AUDIO_REACTIVE`'s LED coexistence, pattern selection, or the
  underlying microphone pipeline was changed or removed — only its motor
  pulses are preempted, and only while `DanceEngine` is active.

**Commands** (word command, Enter-terminated — see Serial controls below):

| Command | Effect |
|---|---|
| `motion` / `motion next` | cycle OFF → IDLE_ALIVE → AUDIO_REACTIVE → OFF |
| `motion off` / `motion idle` / `motion audio` | select a mode directly |
| `motion status` | print current expressive-motion state (also included in `?` — mode, active pattern, step, direction, time remaining, all cooldowns) |
| `motion demo` | demonstrates seven pattern families in order (gentle sway → medium sway → double twitch → forward/reverse nod → excited triple → dramatic sweep → clap-style recoil), ~10-11s total |

Mutually exclusive with diagnostics `2`/`3`/`5`/`6` in both directions;
`k` cancels expressive motion and `motion demo` immediately (from any
pattern or step) and forces the mode back to `OFF` (must be explicitly
re-enabled afterward — unlike the diagnostics, this is a continuous
autonomous behavior, so simply stopping the current pulse isn't enough).
Switching directly between `IDLE_ALIVE` and `AUDIO_REACTIVE` also cancels
any in-flight pattern safely rather than letting it finish under the old
mode.

**Initial physical test procedure:** run `motion demo` first and observe
before enabling continuous movement. See
`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 12 for the full checklist
(per-pattern strength/timing, idle "aliveness", audio-band
responsiveness, LED restoration, `k` reliability, audible motor strain).
**Physical validation has not yet been performed** — do not leave `motion
idle`/`motion audio` running unattended before reviewing that checklist.

## Behavior Engine (development branch)

**Development-branch feature** (`feature/expressive-motion-v1`) — a
high-level personality-state coordinator layered above expressive motion
above. Disabled by default (`MANUAL`); does not alter any existing
behavior until a `behavior` command selects a state. Coordinates
exclusively through expressive motion's public API (never touches
MotorDriver/GPIO/LED pixels/the microphone/serial/buttons directly). Full
architecture and design rationale: `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md`.

```cpp
enum class BehaviorState { MANUAL, IDLE, CURIOUS, LISTENING, THINKING, EXCITED, SLEEPING };
```

- **MANUAL:** inert — the user's own `motion` commands / Button4 long-press
  have direct control, exactly as if this feature didn't exist.
- **IDLE:** delegates to expressive motion's own `IDLE_ALIVE` engine.
- **CURIOUS / LISTENING / THINKING / EXCITED:** each requests a small,
  hand-picked set of named patterns on its own randomized interval —
  investigative and frequent for CURIOUS, one gentle nod then mostly still
  for LISTENING, slow and sparse for THINKING, a finite ~6–12s energetic
  episode (auto-returns to IDLE) for EXCITED. See the doc above for the
  exact pattern lists and intervals.
- **SLEEPING:** movement fully at rest; no owned motor activity.

LED presentation (base effect, overlay, brightness, mute) is left
completely untouched by every Behavior Engine state — deliberately deferred
rather than adding a second rendering layer; see the doc above, section 6.

**Commands** (word command, Enter-terminated — see Serial controls below):

| Command | Effect |
|---|---|
| `behavior` / `beh` | help + current status (no-arg) |
| `behavior next` | cycle MANUAL → IDLE → CURIOUS → LISTENING → THINKING → EXCITED → SLEEPING → MANUAL |
| `behavior manual/idle/curious/listening/pondering/excited/sleeping` | select a state directly (`pondering` selects `BehaviorState::THINKING` — see `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` on why "thinking" can't be typed as a serial token) |
| `behavior status` | print current Behavior Engine state (also included in `?`) |
| `behavior demo` | walks all six non-MANUAL states in order with a fixed dwell each, ~35s total |

Mutually exclusive with diagnostics `2`/`3`/`5`/`6`: entering a
movement-producing state (CURIOUS/LISTENING/THINKING/EXCITED) is refused
while one is active. `k` forces `MANUAL` immediately and cancels any
in-flight movement, from any state. Issuing a `motion` command (other than
`motion status`) or a Button4 long-press action hands movement ownership
back to `MANUAL` first.

**Physical validation has not yet been performed** — see
`docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 15 for the tuning checklist.

## Speaker hardware test (development branch)

**Temporary, isolated MAX98357A bring-up feature** (`include/SpeakerTest.h` /
`src/SpeakerTest.cpp`) — generates digital silence continuously and a
low-volume 440Hz test tone on request. Does **not** route live microphone
audio to the speaker (no acoustic-feedback risk); this is a wiring/output
verification tool only, not a playback system.

**I2S architecture: one shared full-duplex master port** (verified against
this project's installed `driver/i2s.h` headers before implementation, not
guessed — see `SharedI2S.cpp`'s own top-of-file comment for the full
reasoning). This replaced an earlier two-controller design (`I2S_NUM_0`
master RX + `I2S_NUM_1` slave TX sharing BCLK/WS) that **failed
conclusively** on real hardware: `i2s_write()` on the slave TX port always
returned `ESP_OK` with `bytesWritten=0` at every bounded wait tried (20ms,
100ms), and an isolated unbounded (`portMAX_DELAY`) wait froze the entire
application — not just the write — requiring a hardware reset to recover.
That diagnostic has been permanently removed, not just disabled.

- `include/SharedI2S.h` / `src/SharedI2S.cpp` is now the **sole owner** of
  `I2S_NUM_0`'s configuration — the only file that calls
  `i2s_driver_install()`/`i2s_set_pin()`/`i2s_driver_uninstall()` anywhere
  in the firmware. It configures **one** full-duplex master port:
  `I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX`, 16kHz, 32-bit-per-slot,
  `I2S_CHANNEL_FMT_RIGHT_LEFT` (true stereo), standard I2S framing, BCLK=6,
  WS=7, RX data-in=15 (INMP441), TX data-out=16 (MAX98357A).
- `AudioAnalyzer.cpp` only calls `i2s_read()` on that port; `SpeakerTest.cpp`
  only calls `i2s_write()`/`i2s_zero_dma_buffer()` on it. Neither
  reconfigures, reinstalls, or uninstalls the driver — there is exactly one
  `i2s_driver_install()` call in the whole firmware.
- Because the bus is now stereo (both microphone and speaker share one
  physical frame layout — the old mic-only `I2S_CHANNEL_FMT_ONLY_LEFT`
  cannot coexist with stereo TX), the microphone extracts only its own
  active 32-bit slot from each RX frame in software and ignores the other.
  Which slot is active (`Config.h`'s `MIC_I2S_SLOT_INDEX`, currently `0` —
  the first word of each pair) was **empirically confirmed** against real
  captured hardware data (a temporary boot-time RX trace showing one slot
  full of varying real audio data and the other constantly `0x00000000`),
  not assumed from the INMP441's L/R-tied-to-GND wiring alone. The original
  24-bit-left-justified sample interpretation (`sample >> 8`) is unchanged.
- Generated speaker samples are still 16-bit resolution, left-shifted into
  the upper 16 bits of each 32-bit TX slot, duplicated identically into
  both L and R slots.

**Startup safety sequence (required — read before connecting anything):**

1. Keep MAX98357A `SD` connected to **GND** (amplifier disabled/silent)
   before and during boot.
2. Power on / reset the board and watch the serial monitor (115200 baud).
   Very early in boot (before the microphone hardware-test sequences run)
   you will see:
   ```
   [SPEAKER] Initializing MAX98357A output
   [SPEAKER] BCLK=6 LRC=7 DIN=16
   [SPEAKER] I2S TX initialization: SUCCESS
   [SPEAKER] Digital silence active
   [SPEAKER] Connect MAX98357A SD to 3.3V now
   ```
3. **Only after** `Digital silence active` has printed, move MAX98357A
   `SD` from GND to **3.3V**. Digital silence (not an idle/undriven line)
   is already being transmitted continuously at this point, so DIN never
   floats and the amplifier should stay quiet.
4. Roughly 5 seconds after `loop()` begins running (i.e. after
   `[SYSTEM] Ready`, since the lengthy pre-existing hardware bring-up
   sequences run between the message above and `[SYSTEM] Ready`), one
   automatic low-volume 2-second 440Hz test tone plays once, to confirm
   output is working. It does not repeat automatically afterward.

**Commands** (all Enter-terminated word commands — `t`/`s` used to be
immediate no-Enter bytes in an earlier revision, but that collided with
any word command starting with the same letter, including the pre-existing
`status`; see `Controls.cpp`'s comment at those two `case` labels):

| Command | Effect |
|---|---|
| `t` | 440Hz sine, ~2s, ramped, ~7.5% amplitude (the original test tone) |
| `s` | **[temporary diagnostic]** 440Hz square wave, ~2s, ramped, ~5% amplitude |
| `low` / `mid` / `high` | sine at 150Hz / 440Hz / 1500Hz, 2s, ramped, ~10% amplitude |
| `sweep` | logarithmic (exponential) chirp, 150Hz → 3000Hz over 4s, ramped, ~10% amplitude |
| `melody` | C5 E5 G5 C6, 350ms/note + 100ms gap, played twice (~3.6s total) |
| `beep` | 1000Hz, 150ms on / 150ms off, 5 repeats (~1.5s total) |
| `noise` | white noise, 1s, ~5% amplitude |
| `loud` | **[temporary diagnostic]** 1000Hz, 500ms, capped at 20% amplitude (never exceeded) — prints an explicit warning |
| `?` | includes `[I2S]` (shared-bus readiness, mode, pins, GPIO16 routing check) and `[SPEAKER]` status (ready state, phase, sample position, write-outcome counters) |

**Procedural music player** (`music1`-`music4`, `stopmusic`) — generated
entirely from note frequencies (sine waves, no samples/files), built on a
reusable `Note{frequencyHz, length, amplitudeFraction}` / `Song{notes,
noteCount, bpm, name}` data model and ONE generic playback engine
(`SpeakerTest.cpp`'s `songSample()`) that works identically for any song —
nothing is hardcoded per tune. `NoteLength` supports `EIGHTH`/`QUARTER`/
`HALF`, resolved to milliseconds via each song's own tempo. Unlike every
test above, these **loop continuously** (printing `[SPEAKER] Loop N` on
each repeat) until interrupted:

| Command | Plays |
|---|---|
| `music1` | "Twinkle Twinkle Little Star" — full traditional melody, 120bpm |
| `music2` | "Mary Had a Little Lamb" — full traditional melody, 120bpm |
| `music3` | "Ode to Joy" (Beethoven — public-domain 19th-century melody) — first two phrases, 120bpm |
| `music4` | Super Mario Bros. overworld theme — **only the opening flourish** (~2s), not the full copyrighted song, 200bpm |
| `stopmusic` | immediately stops music and returns to silence, printing `[SPEAKER] Stopped` |

Each note gets its own smooth attack/release envelope (capped at 40% of
that note's duration, so short eighth notes still have an audible
sustained portion) — this doubles as note separation, since consecutive
notes both approach silence at their shared boundary. Music playback goes
through the exact same chunked, bounded-wait, partial-write-safe write
path as every other test (see below), so the microphone, LEDs, motor, and
serial commands all keep working normally while a song plays, and `k`
stops it immediately like anything else.

**Designed for future expansion**: `sampleForIndex()`'s dispatch is a
single `switch` over a `TestKind` enum (`SINE`/`SQUARE`/.../`SONG`/...) —
adding a future `playwav`/`playpcm`/`playtts` source means adding one enum
value and one sample-generator function; the scheduler itself
(`generateChunk()`/`feedSpeakerChunk()`/the write path) never needs to
change, since it only ever calls `sampleForIndex()` and has no knowledge of
what kind of source is active.

Every command above **interrupts whatever is currently playing** and
starts immediately — there is no "already playing, refused" case for this
suite (a deliberate choice so testing through commands quickly never
requires waiting one out). `k` (emergency stop) always wins immediately
too, via `stopSpeakerTest()`, returning to silence regardless of what was
playing. All tones/tests use the same 30ms linear fade-in/fade-out
convention (shorter for the very short `melody`/`beep` segments — 15ms and
10ms respectively, so they aren't mostly-ramp) to avoid pops, and share the
same conservative-amplitude philosophy: the 8Ω/0.5W speaker is driven by
the MAX98357A amplifier (5V-supplied — see [Power](#power)), so nothing
here exceeds 20% of full scale.
Sample rate is 16kHz throughout, matching the microphone. No test
auto-repeats; only its own command (or the one automatic `t`-equivalent
demonstration play after boot) triggers it. Silence is transmitted
continuously the rest of the time — the I2S peripheral is never stopped —
and `tx_desc_auto_clear` provides a hardware-level safety net (auto-fills
zeros on any DMA underflow) so DIN can never carry garbage.

The speaker side remains a clearly isolated bring-up feature — removing
`SpeakerTest.h`/`.cpp`, the `initSpeakerTest()`/`updateSpeakerTest()` calls
in `main.cpp`, and the `SPEAKER_*` constants in `Config.h` removes it
without touching LED, motor, or button subsystems. The microphone,
however, is no longer independent of this change: it now shares
`SharedI2S.h`/`.cpp`'s single full-duplex bus with the speaker, and
`AudioAnalyzer.cpp`'s capture code extracts one stereo slot in software
(see above) rather than owning a mono-only port. Reverting the microphone
to a fully standalone I2S port would require restoring its own
`i2s_driver_install()` call and `I2S_CHANNEL_FMT_ONLY_LEFT` format.
**Physical validation (does the speaker actually sound clean, at what real
volume) has not yet been performed beyond serial-log verification — but
the write path itself is now confirmed working end-to-end (100% write
success observed in testing, 0 zero-byte/partial/error writes).**

## Architecture: base effects vs. audio overlays

The firmware composes each frame in four stages:

```
final LED frame =
    base effect            (one of 9 -- 8 real effects + AUTO_SHOWCASE, always running)
  + audio overlay           (optional, blended on top -- OFF by default)
  + global brightness       (9-level table, GPIO17)
  + power-limit scaling     (software current estimate/cap)
```

A **base effect** is the continuous background animation. An **audio
overlay** is an independent, optional layer that blends on top of
*whichever* base effect is currently active (including whatever
`AUTO_SHOWCASE` happens to be showing at that moment) — it does not
replace it, and switching overlays never alters the base effect's own
animation state.

Files:

```
include/Config.h              all tunable constants, with comments
include/LedEffects.h/.cpp       8 real base effects + shared render helpers
include/AudioAnalyzer.h/.cpp    I2S capture, AudioFeatures (rms/envelope/bass/clap/transient)
include/AudioVisualState.h/.cpp  richer per-frame control signals derived from AudioFeatures
include/AudioPalette.h/.cpp     audio-reactive color helpers, shared by every overlay
include/AudioOverlays.h/.cpp    8 overlays, blended onto the frame buffer
include/AutoShowcase.h/.cpp     automatic base-effect rotation + crossfade
include/Controls.h/.cpp        buttons, serial commands, selection state
include/VisualCue.h/.cpp       overlay enable/disable flash, non-blocking cue state machine
src/main.cpp                 setup()/loop() wiring, frame composition, power limit
```

### Base effects (Mode button / `n`,`p`,`effects`)

1. **PETAL_BREATHE** — warm sunflower palette (golden/amber/orange),
   breathing intensity with a slight spatial phase spread across the
   strip, slow hue drift between the three warm tones.
2. **COLOR_WAVE** — a moving gradient wave (blue → violet → magenta →
   pink) traveling along the strip.
3. **SUNSET_SPIN** — slow rotating warm gradient (orange/gold/red/purple).
4. **RAINBOW_FLOW** — full-saturation hue rotation plus a secondary,
   independent brightness wave; the hue algorithm always keeps one
   channel at 0, so adjacent colors never wash out toward white.
5. **SPARKLE_BLOOM** — dim animated base glow with warm (not harsh-white)
   sparkles that rise and decay smoothly; sparkle state persists frame to
   frame.
6. **FIREFLY_GARDEN** — dark ambient base with several independent
   yellow-green/green points that drift, brighten and fade on their own
   timelines.
7. **AURORA** — two layered cyan/blue/purple bands moving at different
   speeds, spatially varied brightness.
8. **SOLAR_FLARE** — flowing warm background with an occasional brighter
   flare traveling along the strip.
9. **AUTO_SHOWCASE** — not a real effect in its own right; automatically
   rotates through effects 1-8 above. See "Automatic effect rotation"
   below for exact timing/transition/exclusion behavior.

### Audio overlays (Mode button / Button 4 / `o`,`x`,`overlays`)

Mode selection and enabled/disabled are independent -- see "Button
controls" below. `selectedOverlayMode` is always one of the eight real
overlays listed here (never "OFF"); OFF just means `audioOverlayEnabled`
is false, and the previously selected mode is preserved, not erased. The
overlay is disabled by default at boot, with PULSE pre-selected.

All eight now draw from the shared `AudioVisualState` (level/bass/
transient/derived low-mid-high control bands -- see "Audio visual-control
signals" below) and the shared `AudioPalette` color helpers, so they use
several independently-varying signals and audio-reactive colors instead
of one scalar driving a single fixed color.

1. **PULSE** — full-strip breathing/traveling energy: a dim slowly-moving
   background gradient at rest; a colored ring pulses outward from center
   as level rises; bass thickens the ring and warms its color; transient
   energy adds bright leading-edge accents on the ring; claps launch a
   separate brief full-range impact wave rather than a solid flash.
2. **RIPPLE** — multiple expanding rings (up to 6 concurrent, fixed pool)
   originating from the center or either end (alternating); bass produces
   wider/slower ripples, transients produce narrower/faster ones; a clap
   launches a pair of high-energy ripples from both ends at once; ripple
   color follows the audio palette.
3. **SPARK** — a richer particle system (up to 10 concurrent, fixed pool):
   low audio spawns occasional dim particles, medium energy spawns more
   with random drift, transients spawn bright directional sparks, bass
   spawns larger glowing "ember" particles with a wider glow and longer
   life; every particle has a fading tail. A clap spawns a bounded 5-spark
   burst across random positions.
4. **LIGHTNING** — up to 3 simultaneous branching bolt segments (fixed
   pool) at varied strip regions/directions/lengths, with a pale core and
   a blue/violet glow, instead of one full-strip flash. Transient strength
   controls branch count and bolt length; sustained bass adds a low
   background "storm glow" between strikes; short bright/dim afterglow
   decay; cooldown-gated so it can't continuously strobe; peak intensity
   capped well below full white.
5. **BASS_BLOOM** — a large expanding bloom from center, gaining a
   mirrored secondary bloom anchor once bass is strong; a complementary
   outer-color ring at medium energy; a bright transient-driven outline
   right at the bloom's edge; a per-pixel smoothly decaying color trail
   (not an instant snap between frames); a subtle atmospheric background
   color even at rest.
6. **SPECTRUM_WAVE** *(new)* — a multicolor hue wave traveling across the
   full strip; level sets wave amplitude, bass changes thickness/speed,
   claps reverse travel direction, loud peaks produce bright near-white
   crests; still subtly animated at silence.
7. **COLOR_FLOOD** *(new)* — layered fill: the low/bass control fills from
   the left, the derived mid control fills from the right (two colored
   layers, not one flat bar), the high/transient control sparkles the
   leading edges, and a clap briefly floods the whole strip with a
   multicolor impact before fading out.
8. **COMET_BURST** *(new)* — directional comets with smoothly fading tails
   (up to 6 concurrent, fixed pool), launched from either end: normal
   peaks launch mid-speed comets, bass launches slower/wider ones,
   transients launch fast/narrow ones, and a clap launches a pair of
   opposing comets that cross the strip. Colors from the audio palette.

`BASS_BLOOM` (and every overlay's `bass` input) reads a **lightweight
low-frequency proxy**, not true FFT-based bass extraction -- see "Audio
visual-control signals" below for exactly what this is and its limits.

### Visual cue: overlay enable/disable flash

Turning the audio overlay off or back on (Button4 press, or the
serial `x` command) flashes the whole strip, independent of whatever base
effect/overlay is currently selected. Revised after physical testing
showed the first version was too dim/desaturated to read clearly and
blended into whatever the base effect was already showing:

- 🟢 **One green flash** = audio overlay enabled/restored. Pattern:
  black 60ms → pure green (`{0,255,0}`) 300ms → black 120ms, then resumes
  the base effect + overlay.
- 🔴 **Two red flashes** = audio overlay disabled. Pattern: black 60ms →
  pure red (`{255,0,0}`) 180ms → black 140ms → red 180ms → black 120ms,
  then resumes the base effect with the overlay OFF.

The leading black phase is deliberate: it blanks whatever the base effect
was showing *before* the confirmation color appears, so the cue reads as
a clean, separate event instead of blending into the prior frame.

This cue does **not** appear when merely selecting a different overlay
mode (Mode button / serial `o`) — only on the explicit off/on toggle
(Button4 / serial `x`).
It's suppressed entirely while muted (the overlay state still changes and
the `[CUE]`/`[AUDIO]` serial lines still print — only the LEDs stay dark;
if the cue's ~260-680ms total duration fully elapses while still muted,
it completes silently and does not replay after unmuting). The flash
always renders at a fixed ~45% brightness cap (`VISUAL_CUE_BRIGHTNESS_RAW
= 115` in `Config.h`), regardless of your selected brightness level, and
still passes through the same power limiter as every other frame.

Retriggering (e.g. mashing `x`) always cancels whatever cue is in
progress and starts the new one from scratch — there's no queue, the
newest toggle always wins.

**Diagnostic-only serial commands** (not for normal use — isolate cue
rendering from button/overlay logic when debugging): `g` forces the
green/enabled cue, `r` forces the double-red/disabled cue, neither
changes the actual overlay selection. Both also print a `[CUE] Start /
Phase N / Complete` trace of every phase transition (start/completion
only, not every render frame).

## Automatic effect rotation (AUTO_SHOWCASE)

`AUTO_SHOWCASE` is a base effect like any other -- reachable via Mode
button presses, `n`/`p`, or directly via serial `a` -- that, while active,
automatically rotates through the other 8 real base effects for you.

- **Timing:** each real effect runs for `AUTO_SHOWCASE_EFFECT_DURATION_MS`
  (default **15000ms / 15s**), then crossfades to the next over
  `AUTO_SHOWCASE_TRANSITION_MS` (default **1500ms / 1.5s**), both in
  `Config.h`. After the last effect it wraps back to the first. Entirely
  `millis()`-driven, no `delay()`.
- **Transition:** a true dual-buffer crossfade -- the outgoing effect is
  rendered into one buffer, the incoming effect into a second buffer, and
  the two are blended per-pixel over the transition window (not a
  simpler fade-to-black-and-back; the project's RAM budget comfortably
  supports two extra 58-LED buffers).
- **Rotation list:** derived automatically from effect ordering
  (`NUM_REAL_BASE_EFFECTS` in `LedEffects.h` = every enum value before
  `AUTO_SHOWCASE` itself), not a manually-duplicated list, so it can never
  select `AUTO_SHOWCASE` internally and would automatically pick up any
  future real effect added above it in the enum. **No effects are
  excluded** -- all 8 current base effects are ordinary ambient LED
  patterns with no diagnostic/test-only/unsafe-for-unattended-cycling
  effects among them.
- **Entering/leaving:** entering always starts from the *first* eligible
  effect (index 0, `PETAL_BREATHE`), not wherever you left off previously
  -- simpler and more predictable than resuming mid-rotation. Leaving (via
  Mode/`n`/`p` cycling past it, or serial `a`) cleanly stops the internal
  timer and returns to **whichever normal effect you had selected before**
  (`lastNormalBaseEffect`, tracked separately) -- `AUTO_SHOWCASE` never
  overwrites your last manual choice.
- **Overlay interaction:** the selected audio overlay renders on top of
  whatever `AUTO_SHOWCASE` is currently showing, completely unaffected by
  its automatic transitions -- selection and enabled/disabled state are
  untouched when the internal effect changes (verified via `status`
  across multiple forced transitions).
- **Serial:** `a` jumps directly to/from `AUTO_SHOWCASE` (no need to cycle
  through all 8 effects first); `c` forces an immediate transition to the
  next effect (no-op if `AUTO_SHOWCASE` isn't active); `status` shows
  `autoShowcaseCurrentEffect` and `autoShowcaseMsRemaining` while it's
  the active base mode.

## Audio visual-control signals

Every overlay now reads a shared `AudioVisualState` (`AudioVisualState.h`)
instead of one raw scalar, recomputed once per rendered frame from the
existing `AudioFeatures` (`AudioAnalyzer.h` -- rms/normalized/envelope/
transientStrength/lowFrequencyEnergy/clap/transient; **unchanged**, this
task did not touch the microphone or analyzer):

| Field | Source | Real or derived? |
|---|---|---|
| `level` / `envelope` | `AudioFeatures::envelope` directly | real (attack/release-smoothed measurement) |
| `bass` / `lowRange` | `AudioFeatures::lowFrequencyEnergy` directly | **derived proxy** -- a single-pole low-pass filter, not an FFT bin (unchanged from before this task) |
| `transient` / `highRange` | decaying 0..1 signal, set to 1.0 on a transient/clap edge, linear decay otherwise | **derived** -- an animation-control heuristic, not a measurement of high-frequency content |
| `transientStrength` | `AudioFeatures::transientStrength` directly | real (instantaneous envelope rise rate) |
| `midRange` | `level` minus weighted `lowRange`/`highRange` contributions, clamped ≥0 | **derived** -- "whatever's left" of the broadband envelope |
| `clap` | `AudioFeatures::clap` directly | real (edge-triggered, cooldown-gated) |
| `energy8`/`bass8`/`transient8` | `level`/`bass`/`transient` scaled to 0-255 | derived (integer-math convenience copies) |

**This project has no FFT or filter bank.** `lowRange`/`midRange`/
`highRange` are **NOT real frequency bands** -- they're three
independently-varying *animation-control* signals built from the existing
bass proxy, transient/clap edge detection, and the overall envelope, so
overlays have more than one scalar to react to. Treat them as "energy
that behaves in a bass-like/broadband/percussive-like way," not spectral
measurements. `v` (serial) prints the live values of every field above,
labeled with this same distinction.

## Button controls

Audio overlay *mode selection* (which effect: PULSE/RIPPLE/SPARK/
LIGHTNING/BASS_BLOOM/SPECTRUM_WAVE/COLOR_FLOOD/COMET_BURST) and audio
overlay *enabled/disabled* are independent (`selectedOverlayMode` /
`audioOverlayEnabled` in `Controls.cpp`). Selecting a mode never turns the
overlay on, and disabling the overlay never forgets which mode was
selected -- turning it back on always resumes the same mode, not a reset
to PULSE.

- **Mode (GPIO10):** press = next base effect **and** next overlay mode,
  together. Since `AUTO_SHOWCASE` is just another base effect in the
  cycle, a Mode press may enter or leave it depending on where you are in
  the order (8 presses from `PETAL_BREATHE` reaches `AUTO_SHOWCASE`; a
  9th wraps back to `PETAL_BREATHE`) -- the overlay mode still advances
  by one on that same press either way. Prints `[MODE] LED: PREV -> NEW`,
  then `[EFFECT] Base: NEW` (existing, unchanged), then `[AUDIO] Selected
  overlay: PREV -> NEW` (with a `(overlay currently OFF)` suffix if the
  overlay isn't enabled). This pairing lets you dial in the overlay mode
  you want *while it's off*, then switch it on with Button 4. Double-press
  (within 350ms) = previous base effect **only** -- does not touch the
  overlay.
- **Mute (GPIO11):** press = toggle LED output off/on. Base effect and
  overlay selection are preserved; the mic keeps running underneath while
  muted. Prints `[MUTE] ON` / `[MUTE] OFF`.
- **Brightness (GPIO17):** press = next level in the 9-step table below.
  Prints `[BRIGHTNESS] <pct>% | raw=<0-255>`.
- **Button 4 (GPIO5):** dual-purpose -- a short press toggles the LED
  audio overlay alone; a press held for 900ms or longer instead toggles
  the unified **Audio Mode** (see [Audio Mode (unified)](#audio-mode-unified)
  below). An earlier single-purpose click-gesture state machine was
  removed entirely; the current debounce/press-edge mechanics are
  unchanged by that history.
  - **Short press** (released before 900ms): fires on the debounced
    **release edge**, only if no long hold completed during that press.
    Prints `[BUTTON4] Audio overlay toggle`, then `[AUDIO] Overlay: ON` or
    `OFF`, then the matching `[CUE]` lines (green single-flash on enable,
    double red-flash on disable).
  - **Long hold** (held 900ms+): fires exactly once, the instant the
    threshold is crossed -- not on release, so continuing to hold never
    fires it again, and releasing after a long hold does **not** also
    fire the short-press overlay toggle. See
    [Audio Mode (unified)](#audio-mode-unified) for what it does and its
    exact log/cue output.
  - Mic diagnostics via Button4 has been removed; use the `d` serial
    command instead. (Neither the short nor long press is a literal
    "microphone diagnostic" -- see that section for what each actually
    does.)

**Boot-arming:** if GPIO5 reads LOW when firmware starts (a wiring/short
issue on real hardware, not something firmware can fix), it is **not**
treated as a legitimate press. Button4 stays disarmed -- printing
`[BUTTON4] Waiting for released HIGH state before arming` once -- until
one stable debounced HIGH (released) is observed, then prints `[BUTTON4]
Armed`. If the pin never reaches HIGH, Button4 stays permanently disarmed
rather than firing a spurious toggle; that's a hardware fault to fix, not
a firmware workaround to add. This part of Button4's handling is
unchanged from before the click-machine removal.

**If Button4 doesn't seem to respond physically:** enable the `b` serial
command (toggles a raw/debounced transition trace --
`[BUTTON4 RAW] HIGH -> LOW`, `[BUTTON4 DEBOUNCED] PRESSED/RELEASED` --
printed only on actual transitions, never every loop) and watch what a
real press produces; `[BUTTON4] Audio overlay toggle` always prints (not
gated by `b`). Also check `status`'s `Button 4 raw state:` / `Button 4
debounced state:` lines at rest (nobody touching it): both should read
**HIGH** / **RELEASED**. If raw reads LOW with nothing pressed, or a real
press/release produces no `[BUTTON4 RAW]` transition at all, that's a
wiring/hardware-level fault (stuck switch, marginal/loose connection,
short to GND) -- not a firmware bug.

## Serial controls

Commands are **Enter-terminated** (type the command, press Enter) so
single letters like `o` never collide with word commands like `overlays`.

| Command | Action |
|---|---|
| `n` | next base effect |
| `p` | previous base effect |
| `o` | next audio overlay mode (does not change enabled/disabled) |
| `x` | toggle audio overlay ON/OFF (does not change the selected mode) |
| `+` | brightness up |
| `-` | brightness down |
| `m` | mute toggle |
| `d` | audio diagnostics (features + tuning constants) |
| `h` | help |
| `effects` | list base effects, marks the current one |
| `overlays` | list audio overlays, marks the current one |
| `status` | full system status (see below) |
| `g` | **[diagnostic only]** force the green/enabled cue, no overlay state change |
| `r` | **[diagnostic only]** force the double-red/disabled cue, no overlay state change |
| `b` | **[diagnostic only]** toggle a raw/debounced Button4 transition trace on/off |
| `a` | toggle `AUTO_SHOWCASE` (jump directly to it, or back to your last normal effect) |
| `c` | force `AUTO_SHOWCASE` to its next effect immediately (no-op if not active) |
| `v` | print the current `AudioVisualState` (level/bass/transient/derived low-mid-high bands, 0-255 copies, active ripple/spark/comet pool counts) |
| `mf` / `mr` | select motor PWM-test direction forward/reverse (see [Motor PWM calibration test](#motor-pwm-calibration-test-development-branch)) |
| `mstop` | coast the motor immediately and cancel every pending motor-test kick/ramp/hold/reversal/routine |
| `m1`-`m100` (e.g. `m20`, `m50`, `m100`) | run the selected direction continuously at that % duty until another motor command, `mstop`, or `k` |
| `mramp` | automatic PWM ramp test (20%-100% forward, coast, 20%-100% reverse) |
| `mcycle` | automatic dance-style speed/direction test (13-step sequence) |
| `mkick` | toggle the startup kick on/off |
| `mstatus` | full motor PWM-test status |
| `audiomode on` / `off` / `status` | unified Audio Mode (LED overlay + `MusicMotorController` together) -- same coordinated path as Button 4 long-hold (see [Audio Mode (unified)](#audio-mode-unified)); dev/test convenience, normal users use the button |
| `danceon`, `danceoff`, `dancestatus`, `dancetest`, `dancetestoff`, `dancequiet`/`dancemid`/`dancehigh`/`dancepeak` | **[unavailable by default]** legacy `DanceEngine` commands, superseded by `MusicMotorController` -- see [Dance Engine V1](#dance-engine-v1-superseded----disabled-by-default). Only exist in a build with `ENABLE_LEGACY_DANCE_ENGINE=1` |
| `musicmotor on` | enable music-reactive movement ALONE (see [MusicMotorController](#musicmotorcontroller-development-branch)) -- for full Audio Mode, prefer `audiomode on` or Button 4 |
| `musicmotor off` | disable it and coast the motor safely |
| `musicmotor status` | full `MusicMotorController` status |
| `musicmotor intensity` | energy pipeline snapshot: `fastEnergy`/`songEnergy`/`baselineEnergy`/band/thresholds |
| `musicmotor motion` | physical-calibration tuning surface: M80 floor, band ranges, drop-hold, wobble, hip-shake, spin |
| `musicmotor summary` | revision 9 compact post-song session stats: band time distribution, drop/phrase counts, max speed |
| `musicmotor dropdetect on` / `off` | revision 9 A/B toggle for the relative/song-adaptive EDM/dubstep drop detector |
| `musicmotor dynamics status` | revision 10 motion palette/duty-cycle/drop-phrase config surface |
| `musicmotor quietmotion on` / `off` | revision 10 QUIET_BUILDUP subtle-sway toggle |
| `musicmotor switchchance <0-100>` / `switchcooldown <ms>` / `switchlimit <count>` | revision 10 drop-phrase reversal tuning |
| `musicmotor test` | revision 10.1 one-command physical-test setup (on+dropdetect+debug+quietmotion, summary reset) |
| `musicmotor test stop` | ends test mode: prints summary, disables debug logging, stops safely |
| `musicmotor spin` | manually trigger one extended spin (only while enabled; obeys cooldown/reverse-coast protection) |
| `musicmotor slow <percent>` / `fast <percent>` | temporarily override the LOW-band / hip-shake speed target |
| `musicmotor hitthreshold <value>` / `beatthreshold <value>` | temporarily override the strong-hit / ordinary-beat transient-delta threshold |
| `musicmotor accel <ms>` / `hold <ms>` / `decel <ms>` | temporarily override the acceleration / hip-shake-hold / deceleration duration |
| `musicmotor lowthreshold <value>` / `mediumthreshold <value>` / `highthreshold <value>` / `peakthreshold <value>` | temporarily override an intensity-band `songEnergy` threshold |
| `musicmotor spintime <ms>` | temporarily fix the extended-spin duration to one value |
| `musicmotor spincooldown <ms>` | temporarily override the minimum gap between two extended spins |
| `musicmotor debug on` / `off` / `status` | enable/disable/report detailed decision diagnostics (dropHold/band/strongHit/choreography evaluation lines); diagnostic only, never changes movement behavior |

`status` prints: current base effect (plus, while `AUTO_SHOWCASE` is
active, its current internal effect and ms remaining before the next
automatic transition), brightness % + raw value, mute state, RMS,
normalized level, envelope, adaptive noise floor estimate, clap/transient
flags, low-frequency energy, configured frame interval, measured FPS,
selected audio-overlay mode, whether the overlay is enabled, current
audio energy/bass/transient control values, active ripple count, active
particle+comet count, and Button 4's pin/raw state/debounced
state/debounce interval.

## Brightness levels

Perceptual 9-step table (not linear) so the low end is genuinely dim and
usable rather than "slightly less bright":

| % | raw (0-255) |
|---|---|
| 3% | 8 |
| 7% | 18 |
| 12% | 31 |
| 20% | 51 (boot default) |
| 32% | 82 |
| 48% | 122 |
| 68% | 173 |
| 85% | 217 |
| 100% | 255 |

100% is selectable, but the software power limit below (default 1000mA)
will scale a frame down automatically if the composed colors would exceed
it — 100% is not guaranteed to reach full 255-per-channel output on
every frame, by design.

**Note:** all brightness scaling is done in software in `main.cpp`
(`strip.setBrightness(255)` is called once and never changed) so the power
estimate below reflects the exact values actually transmitted to the strip.

## Microphone tuning parameters

All in `include/Config.h`, each with a comment on what raising/lowering it
does. The core normalization/smoothing constants are **preserved exactly**
from the original hardware-verified AUDIO_PULSE defaults:

```
AUDIO_NOISE_FLOOR        20000.0   starting point for the adaptive floor
AUDIO_MAX_RMS            200000.0  normalization ceiling
AUDIO_CLAP_THRESHOLD     400000.0  above loud speech, below typical clap RMS
AUDIO_ATTACK_SMOOTHING   0.6       fast rise on speech/claps
AUDIO_RELEASE_SMOOTHING  0.08      slow fall, avoids flicker
AUDIO_CLAP_DECAY         0.85      per-frame decay of the clap flash
```

New in this revision:

```
AUDIO_NOISE_FLOOR_ADAPT_RATE    0.0008  how fast the floor drifts toward quiet RMS
AUDIO_NOISE_FLOOR_MIN/MAX       10000 / 50000   bounds so loud music can't become "the new silence"
AUDIO_NOISE_FLOOR_ADAPT_MARGIN  1.2     only adapts while rms < floor * this margin (i.e. "currently quiet")
AUDIO_TRANSIENT_RISE_THRESHOLD  1.8/s   envelope rise rate that counts as a transient
AUDIO_CLAP_COOLDOWN_MS          250     min gap between clap events
AUDIO_LIGHTNING_COOLDOWN_MS     900     min gap between LIGHTNING triggers
```

**Low-frequency ("bass") proxy — read this before trusting BASS_BLOOM:**
`lowFrequencyEnergy` is **not** a real band-pass filter or FFT bin. It's a
single-pole IIR low-pass (`AUDIO_BASS_LP_ALPHA`, tuned for a ~200Hz
cutoff at 16kHz) applied to the same sample stream used for the main RMS,
then normalized against `AUDIO_BASS_NOISE_FLOOR` / `AUDIO_BASS_MAX_RMS`.
**Those two constants are not hardware-calibrated** (unlike the main RMS
ones) — they're a starting estimate and will very likely need adjustment
against real material. Treat BASS_BLOOM's response as a rough "energy
skews low vs. high" indicator, not accurate bass extraction.

## Power-limit explanation

`include/Config.h`:

```
LED_CURRENT_LIMIT_MA   1000   software cap; raise/lower to match your actual supply
LED_MAX_MA_PER_CHANNEL 20     assumed max mA per color channel at full (255) drive
LED_IDLE_MA_PER_LED    1      assumed per-LED quiescent current
```

Each frame, `main.cpp`'s `applyPowerLimit()` estimates current as
`NUM_LEDS * LED_IDLE_MA_PER_LED + (sum of all R+G+B values across all 58
LEDs) * LED_MAX_MA_PER_CHANNEL / 255`, and scales the whole frame down
proportionally if that exceeds `LED_CURRENT_LIMIT_MA`, printing a
throttled `[POWER]` warning (at most once every 2 seconds) when it does.

**This is a software estimate for bring-up safety, not a substitute for
correct electrical power design.** It cannot protect against a supply
that's undersized for the LEDs' physical maximum draw, and the per-channel
current assumption is a standard approximation, not a measurement of your
specific LEDs. Do not power the 58-LED strip from the ESP32's 3V3 pin;
see the safety warnings section below.

**Overlay additive-blending safety (richer overlays, this revision):**
overlays now layer multiple particles/rings/bolts/comets on the same
frame buffer via `addClamp()`, which saturates each color channel at 255
independently -- there is no integer overflow/wraparound possible no
matter how many effects stack on one pixel. To keep that saturation from
reading as a harsh solid-white blowout, every overlay caps its own
blend/accent amounts (e.g. the transient-driven near-white accent in
`audioEnergyColor()` is capped at a 25% blend; lightning's peak intensity
is capped at `LIGHTNING_MAX_INTENSITY` = 0.75), so accents stay accents.
The existing whole-frame `applyPowerLimit()` above is the final safety
net regardless of how many overlay elements are active in a given frame.

## Host-side regression tests

No PlatformIO `test` env exists in this project (the `test/` directory is
the untouched PlatformIO placeholder) -- `MusicMotorController`'s pure
decision functions are instead exercised by standalone, deterministic,
host-compiled g++ programs in `test_host/`, one file per
feature/regression area, each independent (no shared build system, no
Arduino dependency -- constants/enums/pure functions are mirrored inline
in each file). As of Revision 10.1 there are 12 files:

```
music_motor_choreography_dynamics.cpp
music_motor_choreography_invariants.cpp
music_motor_debug_diagnostics.cpp
music_motor_diagnostic_handoff.cpp
music_motor_intensity_invariants.cpp
music_motor_pipeline_profiles.cpp
music_motor_relative_drop_detection.cpp
music_motor_renewable_phrase.cpp
music_motor_rotation_commitment.cpp
music_motor_silence_rampdown.cpp
music_motor_sustained_drive.cpp
music_motor_sustained_drive_deadlock.cpp
```

Run the whole suite:

```bash
cd projects/sunflower-esp32-s3/test_host
for f in *.cpp; do
  name="${f%.cpp}"
  g++ -std=c++17 -Wall -Wextra -o "/tmp/${name}" "$f" && "/tmp/${name}"
done
```

Each file also documents its own single-file `g++ -std=c++17 ...` command
in its header comment. All 12 must print `PASS: 0 failure(s)` (or the
file's own `All ... tests passed.` line) with zero compiler warnings
before a build is considered clean.

## Physical test procedure

1. Build, upload, and open the serial monitor (commands above).
2. Confirm the boot report: `[SYSTEM] Sunflower LED controller starting`,
   `[MIC] I2S initialization: SUCCESS`, four `[BUTTON]` lines, `[EFFECT]
   Base: PETAL_BREATHE`, `[AUDIO] Selected audio overlay: PULSE`,
   `[AUDIO] Audio overlay enabled: NO`, `[BRIGHTNESS] 20% | raw=51`, then
   `[SYSTEM] Ready`.
3. Press Mode 9 times, confirm all 8 base effects cycle and look visually
   distinct, that the 9th press enters `AUTO_SHOWCASE` (confirm it starts
   rotating -- watch at least one 15s-effect + 1.5s-crossfade cycle), and
   that each press also advances the overlay mode
   (`[AUDIO] Selected overlay: PREV -> NEW (overlay currently OFF)`, since
   it's still off); double-press to confirm it steps the base effect
   backward *without* touching the overlay mode.
4. While in `AUTO_SHOWCASE`, check `status` a few times across a
   transition -- confirm `autoShowcaseCurrentEffect` advances and
   `autoShowcaseMsRemaining` counts down/resets, while `Selected
   audio-overlay mode` and `Audio overlay enabled` stay exactly where you
   left them. Try serial `c` for an instant transition, and `a` to jump
   back to your last normal effect (not necessarily `PETAL_BREATHE`).
5. With the overlay still off, use Mode presses (or serial `o`) to land on
   PULSE, then press Button 4 once — confirm the overlay turns on
   (`[AUDIO] Overlay: ON` + green flash) showing PULSE specifically, not
   some other mode. Make noise near the mic; confirm the animation
   brightens without erasing the underlying pattern, and that motion/color
   spans well beyond just a couple of LEDs.
6. Try RIPPLE, SPARK, LIGHTNING, BASS_BLOOM, SPECTRUM_WAVE, COLOR_FLOOD,
   COMET_BURST the same way (Mode/`o` to select while off or on, Button
   4/`x` to toggle) — speak, clap, and listen to bass-heavy music near the
   mic for each; use serial `v` to watch `level`/`bass`/`transient`/
   `lowRange`/`midRange`/`highRange` move independently while you do.
7. With the overlay ON and set to, say, SPARK: press Button 4 once to turn
   it off (one red double-flash), then once more to turn it back on —
   confirm it resumes SPARK, not PULSE. Press Button 4 repeatedly in quick
   succession — confirm each press toggles independently (no missed or
   doubled toggles). Hold it down for 6-10 seconds — confirm exactly one
   toggle happens (on press, not on release) and nothing further happens
   while held or when released.
7. Press Mute — confirm LEDs blank instantly and the current effect/overlay
   selection is preserved (check via `status` while muted); unmute and
   confirm the animation resumes smoothly, not from a reset state.
8. Cycle Brightness through all 9 levels, confirm the low end is genuinely
   dim and the steps feel perceptually reasonable.
9. Watch serial for `[POWER]` throttle warnings at high brightness with a
   bright overlay active; confirm they're infrequent (throttled), not
   spamming.
10. Leave it running for a few minutes and confirm no reboot / watchdog
    loop (no repeated `ESP-ROM:esp32s3...` boot banners).

## Tuning guide

All constants below are in `include/Config.h`.

- **Too sensitive in a quiet room** (overlays react to nothing): raise
  `AUDIO_NOISE_FLOOR`, or lower `AUDIO_NOISE_FLOOR_ADAPT_MARGIN` /
  raise `AUDIO_NOISE_FLOOR_ADAPT_RATE` so the adaptive floor tracks your
  actual room noise faster.
- **Not sensitive enough to speech:** lower `AUDIO_NOISE_FLOOR` (or its
  adaptive floor bounds `AUDIO_NOISE_FLOOR_MIN`), or lower `AUDIO_MAX_RMS`
  so normal speech reaches further up the 0-1 range.
- **Music response too weak:** lower `AUDIO_MAX_RMS` (makes the ceiling
  easier to reach), or raise `AUDIO_PULSE_GAIN` / `BASS_BLOOM_GAIN` for
  the specific overlay.
- **Constant false clap triggers:** raise `AUDIO_CLAP_THRESHOLD`, or raise
  `AUDIO_CLAP_COOLDOWN_MS` if it's the *rate* that's the problem rather
  than sensitivity.
- **Release too slow** (pulse stays "stuck" bright after a sound ends):
  raise `AUDIO_RELEASE_SMOOTHING` (higher = faster decay).
- **Response too jittery** (flickery, unstable): lower
  `AUDIO_ATTACK_SMOOTHING`, or raise `AUDIO_RELEASE_SMOOTHING`'s
  denominator effect by lowering it (slower, steadier fall).
- **Lightning triggering too frequently:** raise
  `AUDIO_LIGHTNING_COOLDOWN_MS`, or make the trigger condition stricter in
  `AudioOverlays.cpp`'s `applyLightning()` (currently
  `AUDIO_TRANSIENT_RISE_THRESHOLD * 1.5`).

## Safety warnings

- **Do not power the 58-LED strip from the ESP32's 3V3 pin.** At even modest
  brightness, 58 WS2812B LEDs can draw more current than the ESP32's onboard
  3.3V regulator is rated for. GPIO4 provides a **3.3V data signal only** —
  it does not and must not supply LED power.
- **Use a suitable external power supply for the LED strip**, sized for the
  strip's actual current draw at the brightness/color patterns you intend to
  run, with a **common ground** between that supply, the LED strip, and the
  ESP32 GND pin. Do not float the grounds.
- The software power limit above is a bring-up safety aid, not a
  substitute for this.
- **The DRV8833 motor driver, the MAX98357A amplifier, and the WS2812B
  LEDs all share one 5V rail** (corrected 2026-08-01 — this bullet
  previously and incorrectly stated the DRV8833 ran from the ESP32's
  3.3V rail; see [Power](#power) above for the confirmed layout). The
  shared 5V supply, distribution wiring, connectors, and grounding must
  support the combined peak load and noise generated by the LEDs,
  amplifier, and motor — this has **not** been physically measured or
  validated. Evaluate a dedicated external supply (or confirm the
  existing one is adequately rated) with common ground before any
  sustained or production use, and before adding simultaneous amplifier
  playback to the load — see `docs/SPEAKER_BRINGUP_PLAN.md` for the
  specific preflight checks this requires.
- Physical validation confirmed motor engagement visibly disturbs LED
  output while the two share this supply (motor movement is reliable
  with LEDs muted, inconsistent/weak while LEDs are active). The
  `MotorPowerGuard` LED-muting workaround reduces this contention for
  bench development; it is not a power fix.
- **Expressive motion** (development branch, see above) has been
  software-validated only. Run `motion demo` and review
  `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`'s physical validation checklist
  before leaving `motion idle`/`motion audio` running unattended.
- **Behavior Engine** (development branch, see above) has been
  software-validated only. Run `behavior demo` and review
  `docs/BEHAVIOR_ENGINE_DEVELOPMENT.md`'s physical tuning checklist before
  leaving CURIOUS/LISTENING/THINKING/EXCITED running unattended.
- **Speaker hardware test** (development branch, see above): keep
  MAX98357A `SD` at GND until the serial monitor confirms
  `[SPEAKER] Digital silence active`, and follow the full startup sequence
  in that section before moving `SD` to 3.3V. Never connect either speaker
  output terminal to ground. Only serial-log verification (init success,
  correct tone duration, no crashes) has been performed — actual audio
  quality/volume on real hardware has not yet been confirmed.
