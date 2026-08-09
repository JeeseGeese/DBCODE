# AGENTS.md — sunflower-esp32-s3

Project-specific AI instructions. Read `/AGENTS.md` (repository-wide rules)
first — this file adds technical detail on top of it and does not repeat
its safety/process rules. For current work-in-progress state, see
`CURRENT_STATUS.md` and `docs/AI_HANDOFF.md`. For where this is headed,
see `ROADMAP.md`.

**This project is not finished.** It is an actively evolving animatronic
sunflower: WS2812 LEDs, an INMP441 microphone, a DRV8833-driven brushed DC
motor, and (in development) a MAX98357A speaker, all coordinated on one
ESP32-S3. Current milestone: **Expressive Motion / MusicMotor Revision
10.1** (see `CURRENT_STATUS.md`).

Full narrative documentation — every design decision, every physical test
result, every revision's reasoning — lives in `README.md` (this
directory) and `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`,
`docs/BEHAVIOR_ENGINE_DEVELOPMENT.md`, `docs/DRV8833_MOTOR_BRINGUP.md`,
`docs/LED_TEST_PLAN.md`. This file is a map and a rules summary, not a
replacement for those — when in doubt about a specific subsystem, go read
its section in `README.md`.

## 1. Current hardware

- **MCU:** ESP32-S3-WROOM, N16R8 (16 MB flash, 8 MB octal PSRAM), generic
  devkit, USB-C bridged to UART0 via an onboard WCH CH343 chip (not the
  ESP32-S3's native USB-OTG peripheral — this is why `ARDUINO_USB_MODE=0`
  is set in `platformio.ini`).
- **LEDs:** 36x WS2812B-compatible addressable LEDs (`NUM_LEDS=36`),
  single data line, GPIO4. Corrected 2026-08-08 (Sunny V1.1 LED-count
  audit) — this section previously said 58 (`NUM_LEDS=58`) with a
  separately-documented, never-independently-confirmed "42-LED assembly"
  theory layered on top; both are superseded by a direct physical count
  of 36. See `docs/current/HARDWARE_ARCHITECTURE.md` and
  `docs/lessons/verify-physical-led-count.md`. `docs/V1/` retains the
  frozen 58-LED snapshot as historically accurate for that tag — do not
  edit it to match this correction.
- **Buttons:** 4x momentary pushbuttons, `INPUT_PULLUP`, no external
  resistors.
- **Microphone:** 1x INMP441 I2S MEMS microphone.
- **Motor:** 1x DRV8833 H-bridge + brushed DC motor. Digital
  (`digitalWrite`) control in the original production path; PWM speed
  control (`MotorPwmCalibration`, `DanceEngine`, `MusicMotorController`)
  exists and is physically validated down to the M80 floor — see section
  6 below.
- **Speaker (development branch):** 1x MAX98357A I2S amplifier + 8Ω/0.5W
  speaker. Wiring/output-verification only so far (`SpeakerTest`) —
  actual audio quality/volume has not been physically confirmed beyond
  serial-log write-success verification.
- **Power** (corrected 2026-08-01 — an earlier version of this section
  incorrectly stated the DRV8833 ran from the ESP32's 3.3V rail):

  ```text
  Sunny power distribution:
  - Shared 5V rail: WS2812 LEDs, MAX98357A amplifier, and DRV8833 motor driver
  - 3.3V rail: INMP441 microphone
  - Common ground across ESP32-S3 and all peripherals
  ```

  ESP32 GPIO logic signals (I2S, buttons, LED data) remain 3.3V
  regardless of a peripheral's own power rail. See section 5 for the
  real electrical risk this shared 5V rail creates — this architecture
  is **not** claimed to be power-validated; see that section for exactly
  what remains unconfirmed.

## 2. GPIO ownership

| Signal | GPIO | Sole owner | Notes |
|---|---|---|---|
| WS2812 LED data (DIN) | 4 | `main.cpp`'s single `strip` object | 3.3V logic-level data only — never power the strip from this pin's rail |
| Mode button | 10 | `Controls.cpp` | `INPUT_PULLUP` |
| Mute button | 11 | `Controls.cpp` | `INPUT_PULLUP` |
| Brightness button | 17 | `Controls.cpp` | `INPUT_PULLUP` |
| Button 4 | 5 | `Controls.cpp` | `INPUT_PULLUP`; boot-arming logic — see README "Button controls" |
| INMP441/MAX98357A BCLK | 6 | `SharedI2S.cpp` (`I2S_NUM_0`) | shared full-duplex master port, see section 4 |
| INMP441/MAX98357A WS | 7 | `SharedI2S.cpp` (`I2S_NUM_0`) | shared full-duplex master port |
| INMP441 SD (RX data in) | 15 | `SharedI2S.cpp` / `AudioAnalyzer.cpp` reads | |
| MAX98357A DIN (TX data out) | 16 | `SharedI2S.cpp` / `SpeakerTest.cpp` writes | |
| DRV8833 IN1 | 8 | `MotorDriver.cpp` (sole owner of all GPIO8/9 access) | briefly attached to an LEDC PWM channel only while `MotorPwmCalibration` owns it |
| DRV8833 IN2 | 9 | `MotorDriver.cpp` | same |
| DRV8833 SLEEP/nSLEEP | — | not wired / not driven by firmware | |

**Unassigned — do not invent a pin number for these without a hardware
audit**: Button 5/6 GPIOs, conversation-enable switch GPIO,
voice-prompt-enable switch GPIO. See README section "GPIO ownership
table" (in `docs/EXPRESSIVE_MOTION_DEVELOPMENT.md`) for the exclusion
list any future audit must respect (flash/PSRAM-reserved pins, USB/JTAG-
sensitive pins, boot-strapping pins, pins already in the table above).

## 3. Subsystem ownership (single-owner convention)

This codebase's strongest architectural convention: **every hardware
resource has exactly one owning module**, and every higher-level behavior
reaches that resource only through the owner's public API — never
directly. Preserve this when adding anything new.

| Resource | Sole owner | Everything else goes through it via |
|---|---|---|
| GPIO8/GPIO9 (motor pins) | `MotorDriver.cpp` | `motorForward()`/`motorReverse()`/`motorStop()`/`motorBrake()`/`motorForwardMs()`/`motorReverseMs()`, plus PWM primitives `initMotorPWM()`/`motorPWMForward()`/`motorPWMReverse()`/`motorPWMCoast()`/`deinitMotorPWM()` |
| `I2S_NUM_0` driver install/config | `SharedI2S.cpp` | `AudioAnalyzer.cpp` (`i2s_read()` only), `SpeakerTest.cpp` (`i2s_write()`/`i2s_zero_dma_buffer()` only) — see section 4 |
| `Serial.read()`/`available()` | `main.cpp`'s `pollSerialDispatcher()` | `Controls.cpp`'s `feedSerialByte()`, fed one byte at a time — no module may call `Serial.read()`/`available()` independently |
| The single `Adafruit_NeoPixel strip` object / `strip.show()` | `main.cpp` | LED-mute state via `Controls.h`'s `isMuted()`/`setMuted()` — never a second `NeoPixel` instance, never a second `strip.show()` call site |
| LED mute save/restore around motor engagement | `MotorPowerGuard.cpp` | `requestMotorPower()`/`isMotorPowerReady()`/`releaseMotorPower()`/`releaseMotorPowerImmediately()` — used by `MotorBehavior` (IDLE_SWAY), `MotorPriorityMode`, `ExpressiveMotion`. **Not** used by `DanceEngine` or `MusicMotorController` (deliberate — see section 5) |
| Audio feature extraction (RMS/envelope/clap/transient/bass proxy) | `AudioAnalyzer.cpp` | `AudioFeatures` struct, read-only by every consumer (LED overlays, `DanceEngine`, `MusicMotorController`, `ExpressiveMotion`) — no second audio-analysis implementation, no raw per-sample decisions elsewhere |
| Motor-behavior/diagnostic mutual exclusion | `isAnyMotorDiagnosticActive()` (declared in `ExpressiveMotion.h`) | every motor-owning module checks this before starting; see section 7 |

**Never create a second owner of any resource in this table.** If a new
feature seems to need direct access to one of these, it almost certainly
needs to call the existing owner's API instead — this has been the fix
for every "which module should touch X" question in this project's
history so far.

## 4. SharedI2S ownership

`include/SharedI2S.h` / `src/SharedI2S.cpp` is the **sole owner** of
`I2S_NUM_0` — the only file in the firmware that calls
`i2s_driver_install()`, `i2s_set_pin()`, or `i2s_driver_uninstall()`. It
configures exactly one full-duplex master port: `I2S_MODE_MASTER |
I2S_MODE_RX | I2S_MODE_TX`, 16kHz, 32-bit-per-slot,
`I2S_CHANNEL_FMT_RIGHT_LEFT` (true stereo), BCLK=6, WS=7, RX data=15
(INMP441), TX data=16 (MAX98357A).

- `AudioAnalyzer.cpp` only calls `i2s_read()` on this port.
- `SpeakerTest.cpp` only calls `i2s_write()`/`i2s_zero_dma_buffer()` on
  it.
- Neither reconfigures, reinstalls, or uninstalls the driver.
- The microphone extracts only its own active 32-bit slot
  (`Config.h`'s `MIC_I2S_SLOT_INDEX`, currently `0`) from each stereo RX
  frame in software — this was **empirically confirmed** against a
  captured hardware RX trace, not assumed from wiring alone. Do not
  "simplify" this back to a mono-only I2S format without re-deriving why
  it was changed (see `SharedI2S.cpp`'s own top-of-file comment and the
  README's "Speaker hardware test" section: an earlier two-controller
  design with a separate slave TX port failed conclusively on real
  hardware, including one unbounded-wait freeze that required a physical
  reset).
- If you ever need a second I2S peripheral or reason to touch this
  architecture, re-read that failure history first — it is not a
  hypothetical concern, it already happened once on this exact hardware.

## 5. Motor ownership and known electrical constraints

- `MotorDriver.cpp` is the only module that ever calls `pinMode`/
  `digitalWrite`/LEDC functions on GPIO8/GPIO9. Every behavior layer
  (`MotorBehavior`, `MotorPriorityMode`, `MotorPwmCalibration`,
  `DanceEngine`, `MusicMotorController`, `ExpressiveMotion`,
  `BehaviorEngine`) calls only `MotorDriver`'s exported functions.
- **DRV8833 VCC is powered from the shared 5V rail** — the same rail as
  the WS2812 LEDs and (in development) the MAX98357A amplifier; only the
  INMP441 microphone runs from the ESP32's 3.3V pin. (Corrected
  2026-08-01 — this section previously and incorrectly stated the
  DRV8833 ran from the ESP32's 3.3V logic rail; see this section's own
  history in `git log` for the prior wording.) The real electrical risk:
  the shared 5V supply, distribution wiring, connectors, and grounding
  must support the combined peak load and noise generated by the LEDs,
  amplifier, and motor. This is explicitly **not** claimed to be a
  power-validated architecture — before any sustained or unattended
  operation, or before adding the amplifier as a third simultaneous 5V
  load, this needs real measurement (supply type/rated current, wiring
  gauge, bulk/local decoupling, voltage under combined load — see
  `docs/SPEAKER_BRINGUP_PLAN.md`'s preflight list for the specific
  checks) or a dedicated external supply sized for actual combined draw
  including motor startup inrush.
- **Motor engagement measurably disturbs LED output** while the two share
  this 5V supply — physically observed, correlated with shared-supply
  power contention (no exact voltage sag has been instrumented). Motor
  movement is reliable with LEDs muted, inconsistent/weak while LEDs are
  active.
- Three different LED-coexistence strategies exist for this reason, and
  they are **not** interchangeable — know which one a given behavior
  uses before touching it:
  - `MotorPowerGuard` `FULL_MUTE` (default) — LEDs fully muted around
    motor engagement. Used by `IDLE_SWAY`, `MotorPriorityMode`,
    `ExpressiveMotion`.
  - `MotorPowerGuard` `DIM_DURING_MOTION` — LEDs dimmed, not muted, base
    effect keeps animating. Used only by the `5` diagnostic test.
  - **No mitigation at all** — `DanceEngine` and `MusicMotorController`
    deliberately never call `MotorPowerGuard` and never touch LED mute
    state, by explicit prior design decision, so LEDs stay exactly as the
    user set them through sustained dance/music sessions. This means
    sustained high-duty PWM movement and full LED rendering can draw from
    the shared 5V rail simultaneously for as long as a session lasts —
    **whether this causes visible disturbance, brownout, or resets during
    real extended use has not been physically confirmed.** Do not silently
    add power mitigation back into these two modules without it being a
    deliberate, documented decision — it was removed once already by
    explicit request.
- **M80 (80% PWM duty) is the physically-validated minimum reliable
  movement command** on the real mechanism — below it, movement is not
  dependable. All active PWM behaviors clamp to this floor; only
  commanded coast/stop (M0) and genuine deceleration may go below it.
- No current-sensing or thermal-monitoring hardware exists anywhere in
  this project. Over-current, stall, and thermal conditions cannot be
  detected in software — only avoided via conservative timing/duty
  choices and the safety backstops in section 8.
- The mechanical belt-preload root cause that originally blocked motor
  startup (section 20 of `docs/DRV8833_MOTOR_BRINGUP.md`) is closed —
  don't re-open pulse-duration tuning or PWM-characterization work
  assuming that's still the open question; it isn't.

## 6. MusicMotor architecture

```
AudioAnalyzer  ->  MusicMotorController  ->  MotorDriver / PWM motor primitives
```

`include/MusicMotorController.h` / `src/MusicMotorController.cpp` is the
actively-developed, physically-calibrated music-reactive motor behavior —
currently at **Revision 10.1**. It is a separate module from
`DanceEngine` by explicit design (experimental/actively-evolving logic is
not embedded into the earlier choreography engine); the two are mutually
exclusive at runtime via `isAnyMotorDiagnosticActive()`, never merged.

Core structure (do not flatten this back into a single energy value —
each layer answers a different question, and revision history shows
conflating them was the actual bug in Revision 1):

- **Triple independently-smoothed EMA:** `fastEnergy` (individual beats),
  `songEnergy` (sustained section intensity), `baselineEnergy` (slow
  adaptive "recent normal level"). `transientDelta = fastEnergy -
  baselineEnergy` drives beat/strong-hit detection.
- **Intensity bands** (`QUIET`/`LOW`/`MEDIUM`/`HIGH`/`PEAK`) from
  `songEnergy`, hysteresis-gated, each mapped to an M80-M100 PWM range
  with continuous interpolation inside the band — never an instant jump.
- **Deterministic beat-action selection** (`selectBeatAction()`) — never
  `random()`. Ordinary beats always accent; strong hits pick among
  accent/reverse/hip-shake/extended-spin via a per-band modular counter.
  PEAK never reverses (deliberately, per an early physical-test finding
  that reversing too often didn't look like committed movement).
- **`EXTENDED_SPIN`** is open-loop and time-based — **there is no encoder
  or position sensor anywhere in this project.** Never document or imply
  an exact rotation angle; "approximate extended rotation" is the correct
  framing.
- **Reversal safety, one shared gate** (`checkReversalGate()`/
  `tryRequestReversal()`) — minimum direction hold, reversal cooldown,
  post-spin hold, all before any reversal is granted; every accepted
  reversal ramps to 0, forces both GPIO8/GPIO9 LOW, coasts, then drives
  the new direction. No instantaneous polarity reversal anywhere in this
  module.
- **Revision 9** added relative/song-adaptive drop detection (confidence
  scoring + phase machine) — treat this as unmodified by Revision 10;
  Revision 10 only changed how detected drops are *choreographed*.
- **Revision 10** added a speed-authority cap ("bounded lending" — how
  far historical energy memory may raise the *commanded* speed, keyed to
  the live measured band) and a `MotionTier`-based pulse/rest duty cycle
  for slower-than-M80-feeling motion (there is still no validated
  continuous PWM value below M80 — "slow" is always duty-cycled M80+,
  never a low raw PWM number).
- **Revision 10.1** fixed a genuine deadlock (`SUSTAINED_DRIVE` could get
  permanently stuck at M0 while logging a step it never applied — root
  cause: a step comparison used a stale array value instead of live
  `currentDirection`) and added `checkSustainedDriveInvariant()` as
  defense-in-depth against that *class* of stuck state recurring. Covered
  by `test_host/music_motor_sustained_drive_deadlock.cpp`.
- **LED power handling: none** — see section 5. This is deliberate, not
  an oversight; do not silently reintroduce muting.
- **No FFT / real frequency-band analysis anywhere.** The bass-impact
  signal (`AudioFeatures.lowFrequencyEnergy`) is a single-pole low-pass
  RMS proxy, not a real band-pass filter — document it as such if you
  touch it, and note its noise-floor/max-RMS constants are *not*
  hardware-calibrated the way the main RMS ones are.

Full revision-by-revision history and reasoning: `README.md`'s
"MusicMotorController" section (search for "revision" — there are 10
numbered subsections plus 10.1).

## 7. Current validated architecture and invariants

Architectural invariants that hold across the whole firmware and must not
be silently broken by new code:

- **Single-owner resources** (section 3) — never a second owner.
- **No `delay()` in any behavior/controller module** — `MotorBehavior`,
  `MotorPowerGuard`, `MotorPriorityMode`, `MotorPwmCalibration`,
  `DanceEngine`, `MusicMotorController`, `ExpressiveMotion`,
  `BehaviorEngine` are all non-blocking, `millis()`-based, called every
  `loop()` iteration. A `delay()` anywhere in these modules would stall
  serial/button/LED/audio processing for every other subsystem.
- **No instantaneous polarity reversal.** Every motor-driving module
  inserts an explicit stop/coast segment between any forward and reverse
  command — verified by inspection at every revision so far. Preserve
  this in any new motor-driving code.
- **A generic ~2000ms max-energized-runtime safety net
  (`MOTION_MAX_ENERGIZED_MS` and equivalents) backstops every behavior**
  regardless of its own timing logic — a defensive ceiling, not the
  primary timing mechanism. New motor behaviors should stay comfortably
  under it and should not rely on it as their actual stop condition.
- **`k` is a full emergency stop from any state** — raw drive, any
  behavior, any diagnostic, any in-flight pattern — and is checked first,
  unconditionally, even mid-word, in the central serial dispatcher. Any
  new motor-driving module must wire its own cancel function into
  `serviceEmergencyStop()` in `main.cpp`. Never add a code path that
  could leave the motor energized after `k`.
- **Bidirectional mutual exclusion among motor owners.** Every
  motor-owning module both (a) refuses to start while
  `isAnyMotorDiagnosticActive()` (or the equivalent check for that
  specific module) is true, and (b) is itself reflected by that check
  once active, so nothing can fight another module for the motor. Follow
  this exact pattern for any new motor-driving feature — it is the
  established, repeatedly-reused solution to "who owns the motor right
  now."
- **`BehaviorEngine` never becomes a second motor owner.** It coordinates
  purely through `ExpressiveMotion`'s `requestExpressivePattern()`, which
  itself funnels through the one pattern-step engine. If you add a new
  high-level coordinator, make it call through an existing owner's public
  API the same way — never let it reach for `MotorDriver` directly.

## 8. Current coding conventions

- `enum class` for all mode/state/phase types (not plain `enum`), e.g.
  `MotorBehaviorMode`, `ExpressiveMotionMode`, `MusicMotorState`,
  `BehaviorState`. Follow this for any new state type.
- Tunable constants live in `include/Config.h`, grouped by feature, each
  with a comment describing what raising/lowering it does. New tunables
  belong there, not hardcoded inline, so a future physical-calibration
  pass can find them.
- Shared timing values are expressed as **tiers** referenced by name
  (e.g. `MOTION_GENTLE_PULSE_MIN/MAX_MS`), not repeated as literal
  numbers in each call site — retuning a tier retunes every consumer at
  once. Prefer this pattern over a fresh literal when adding a new
  pulse/pause duration that's conceptually "the same strength" as an
  existing tier.
- Percent-based motor speed (0-100, the "M" scale used by
  `MotorPwmCalibration`/`DanceEngine`/`MusicMotorController`) is converted
  to raw 8-bit PWM duty in exactly one place
  (`percentToMotorPwm()` in `MusicMotorController.cpp`) — never scatter a
  second percent-to-duty formula elsewhere.
- Deterministic over random wherever a decision needs to be reproducible
  for validation — e.g. `selectBeatAction()`'s modular per-band counters,
  not `random()`. Prefer this pattern for new decision logic that will
  need repeatable test/tuning sessions.
- Reserved single-character serial commands must never contain the
  letter `k` (case-sensitive check happens on `k`/`K` unconditionally,
  even mid-word) — this is why `BehaviorState::THINKING`'s serial token
  is `pondering`, not `thinking`. Check any new serial word command
  against this before choosing its token.
- Every new serial word command must be checked against the existing
  command tables (README "Serial commands" and "Serial controls"
  sections) for collisions before being added — this project has hit and
  fixed real collisions before (`p`, and the `f` inside `effects`).

## 9. Current testing procedures

- **Host tests first.** `test_host/*.cpp` — 12 files as of Revision
  10.1, each independent, no shared build system, no Arduino dependency
  (constants/enums/pure functions mirrored inline per file). Run the
  whole suite:

  ```bash
  cd projects/sunflower-esp32-s3/test_host
  for f in *.cpp; do
    name="${f%.cpp}"
    g++ -std=c++17 -Wall -Wextra -o "/tmp/${name}" "$f" && "/tmp/${name}"
  done
  ```

  All must print `PASS: 0 failure(s)` (or the file's own `All ... tests
  passed.` line) with **zero compiler warnings** before a change in the
  area they cover is considered done. Each file also documents its own
  single-file build/run command in its header comment.
- **No PlatformIO `test` env exists** — `test/` is the untouched
  PlatformIO placeholder. Don't try to run `pio test`; it won't find
  anything.
- **New host-testable regressions get a host test.** When a bug is found
  (via physical testing, serial-log inspection, or code review) in logic
  that's host-testable, add a `test_host/*.cpp` file that reproduces the
  original bug and proves the fix, following the existing files' style
  (see any existing file's header comment for the pattern —
  `music_motor_sustained_drive_deadlock.cpp` is a recent, thorough
  example).
- **Physical test procedure** for the base LED/audio/button system is in
  `README.md`'s "Physical test procedure" section — a 10-step checklist
  covering boot report, effect cycling, overlay behavior, mute,
  brightness, and power-limit throttling. Run it after any change
  touching `Controls.cpp`, `LedEffects.cpp`, or `AudioOverlays.cpp`.
- **`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 12** and
  **`docs/BEHAVIOR_ENGINE_DEVELOPMENT.md` section 15** each have their own
  physical validation/tuning checklists, not yet fully executed as of the
  current milestone — see `CURRENT_STATUS.md`.
- Never report a test as passed without actually running it this
  session; see `/AGENTS.md` sections 5-6.

## 10. Current build commands

```bash
cd ~/DOBETTERCODE/DBCODE/projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"   # if pio isn't already on PATH
pio run
```

Upload:

```bash
pio device list                          # confirm the port, typically /dev/ttyACM0
                                          # — don't assume; if drv8833-motor-test's
                                          # board is also plugged in, check SER=...
pio run -t upload --upload-port /dev/ttyACM0
```

If not in the `dialout` group: `sg dialout -c "pio run -t upload --upload-port /dev/ttyACM0"`.

## 11. Current serial monitor commands

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

(Ctrl+C to exit.) 115200 baud always — this is set in `platformio.ini`'s
`monitor_speed` and must match on both ends.

**Full current command reference lives in `README.md`** ("Serial
commands" and "Serial controls" sections) — it is long (motor
diagnostics, `MotorPwmCalibration`, `DanceEngine`, `MusicMotorController`,
`ExpressiveMotion`'s `motion`, `BehaviorEngine`'s `behavior`, speaker
tests) and changes often enough that duplicating it here would go stale.
A few commands worth knowing before touching anything:

- `k` — universal emergency stop, works from any state, any module.
- `status` / `?` — full system status; `?` additionally includes motor/
  motion/behavior diagnostic state that `status` doesn't.
- `musicmotor test` — one-command physical-test setup for the current
  milestone (enables MusicMotor + drop detection + debug logging +
  quiet-buildup motion, resets summary counters). Use this, not manual
  step-by-step enabling, when starting a MusicMotor physical test session.
- **Boot takes ~30-40s** before serial commands are processed —
  `HardwareTest` and `MicRetest` both run unconditionally and block at
  the end of `setup()`. Don't assume a command sent immediately after
  reset was received; wait for the prompt to actually be responsive.

## 12. Known pitfalls

- **Two independent `Serial` readers will race and drop bytes.** This
  already happened once (see `docs/DRV8833_MOTOR_BRINGUP.md` section 21)
  — an intermittent ~50% `k`-miss rate during one specific test, root-
  caused to `Controls.cpp` having its own independent
  `Serial.available()` loop alongside `main.cpp`'s dispatcher. Fixed by
  making `main.cpp`'s `pollSerialDispatcher()` the sole reader. Never add
  a second `Serial.read()`/`available()` call site anywhere in this
  firmware.
- **The boot-time motor pulse succeeds in a uniquely quiet system state**
  (zero concurrent LED rendering, zero concurrent I2S activity) that
  runtime code never reproduces on its own — see
  `docs/DRV8833_MOTOR_BRINGUP.md` sections 16-18. Don't assume something
  that works at boot will behave identically once `loop()` is running
  with LEDs/audio/buttons all active.
- **UVLO (undervoltage lockout) is a plausible hypothesis, not a
  confirmed root cause, for any early bring-up finding** — do not
  document it as proven anywhere in this project (see
  `docs/DRV8833_MOTOR_BRINGUP.md` section 8). The actual confirmed root
  cause of the original startup failure was mechanical belt preload
  (section 20 of the same doc), not electrical.
- **`NUM_LEDS` was corrected from 58 to 36 on 2026-08-08** (Sunny V1.1
  LED-count audit) after a direct physical count — the "42-LED assembly
  vs. 58" framing that used to live here was itself never independently
  verified, and is now superseded, not confirmed. Every render loop,
  buffer, and the power estimator (`applyPowerLimit()` in `main.cpp`)
  already read `NUM_LEDS` symbolically rather than a hardcoded literal,
  so this one constant change was sufficient — see
  `docs/lessons/verify-physical-led-count.md` for the full lesson and
  `docs/current/POWER.md` for why this correction is explicitly NOT
  evidence for or against the separately-open brownout investigation.
- **The word "thinking" cannot be a serial command token** — it contains
  `k`, which the dispatcher intercepts unconditionally mid-word. This is
  why the enum value is `BehaviorState::THINKING` but the serial token is
  `pondering`. Any new serial token needs the same check.
- **`AUDIO_BASS_NOISE_FLOOR`/`AUDIO_BASS_MAX_RMS` are not
  hardware-calibrated**, unlike the main RMS constants — don't treat
  `lowFrequencyEnergy`/`BASS_BLOOM` output as precise; it's a rough
  low-vs-high energy skew indicator from a single-pole low-pass, not real
  bass extraction.
- **A two-controller I2S design (separate slave TX port) failed
  conclusively on this exact hardware once already** — `i2s_write()`
  silently returning `bytesWritten=0`, and an unbounded wait that froze
  the whole application. Don't re-attempt a multi-controller I2S
  architecture without re-reading why `SharedI2S`'s single full-duplex
  port replaced it.

## 13. Known "do not accidentally break" behaviors

These are physically-observed-working behaviors from the `v1.0.0` tagged
baseline and validated feature work since. Any change that touches
adjacent code should be checked against these before being called done:

- Four-button control (Mode/Mute/Brightness/Button4) — all four
  physically verified at `v1.0.0`. Button4 has changed since: its
  original click-gesture machine was removed (short press = overlay
  toggle on the debounced release edge), then it gained dual-purpose
  short/long-press detection, and its long-hold binding has since moved
  from `ExpressiveMotion`'s `AUDIO_REACTIVE` mode to the unified Audio
  Mode (`setUserAudioModeEnabled()`, `src/Controls.cpp`) — LED overlay +
  `MusicMotorController` together. The short-press/long-hold mechanics
  themselves are host-tested (`test_host/audio_mode_button4_integration.cpp`)
  but the current long-hold binding is **not yet physically validated** —
  see `CURRENT_STATUS.md`'s "DanceEngine removal checklist."
- WS2812 LED effects and the audio-reactive overlay system — physically
  verified at `v1.0.0`.
- INMP441 audio input and audio-reactive overlay response.
- Bidirectional DRV8833 motor control (forward/reverse/stop/brake) via
  `MotorDriver` — physically verified, mechanical root cause (belt
  preload) since fixed.
- Motor + LED coexistence via `MotorPowerGuard` `FULL_MUTE` — physically
  verified for `IDLE_SWAY`/`MotorPriorityMode` diagnostics specifically;
  **not** the same guarantee for `DanceEngine`/`MusicMotorController`,
  which don't use it at all (see section 5).
- The centralized serial dispatcher and reliable `k` emergency stop —
  physically/serially validated after the two-reader race was fixed (10/10
  trials in both directions, see `docs/DRV8833_MOTOR_BRINGUP.md`
  section 21).
- The audio serial-logging toggle (`7`) and the LED index-mapping
  diagnostic (`6`) — both part of the `v1.0.0` baseline.
- `MusicMotorController`'s M80 active-movement floor and its non-
  instantaneous-reversal guarantee — physically validated as of Revision
  10.1; treat any change that could push a live command below M80 or
  remove a coast-before-reversal step as a regression, not a
  simplification.

If you're not sure whether a change could affect one of the above, say so
explicitly rather than assuming it's isolated — this project's own history
has repeatedly found that changes assumed to be isolated (e.g. the I2S
format change) were not.
