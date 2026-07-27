# sunflower-esp32-s3

ESP32-S3 firmware project driving a 58-pixel WS2812B-compatible addressable
LED strip, controlled by four physical pushbuttons, with an INMP441 I2S
microphone driving optional audio-reactive overlays.

## Hardware

- **MCU:** ESP32-S3-WROOM module, N16R8 variant (16 MB flash, 8 MB octal PSRAM)
- **Board:** generic ESP32-S3 devkit, USB-C, UART0 bridged to USB via an
  onboard WCH CH343 chip (not the ESP32-S3's native USB-OTG peripheral)
- **LEDs:** 58x WS2812B-compatible addressable LEDs, single data line
- **Buttons:** 4x momentary pushbuttons, each wired between its GPIO pin
  and GND, using the ESP32's internal pull-up (no external resistors)
- **Microphone:** 1x INMP441 I2S MEMS microphone
- **Power:** ESP32 5V pin and LED strip 5V share a common ground with the
  ESP32 GND pin; INMP441 VDD runs from the ESP32 3V3 pin
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
| Button 4 | 5 | `INPUT_PULLUP`, wired to GND — press: toggle audio overlay ON/OFF immediately (debounced press edge, no hold/click-count logic) |
| INMP441 SCK/BCLK | 6 | I2S bit clock, driven by the ESP32 (I2S master) |
| INMP441 WS/LRCLK | 7 | I2S word select, driven by the ESP32 (I2S master) |
| INMP441 SD/DATA | 15 | I2S data input to the ESP32 |
| DRV8833 IN1 | 8 | `digitalWrite` only, no PWM yet — see Motor driver section |
| DRV8833 IN2 | 9 | `digitalWrite` only, no PWM yet — see Motor driver section |

INMP441 `L/R` is tied to GND (selects the LEFT I2S channel) and `GND` is
tied to a common ground shared with the ESP32.

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

**STATUS: IDLE_SWAY PHYSICALLY VALIDATED WITH LED POWER LIMITATION.** Full
bring-up and validation history (failed PWM characterization, digital
isolation test, electrical diagnostic, final hardware validation, IDLE_SWAY
physical validation, measured facts vs. hypotheses) is in
[`docs/DRV8833_MOTOR_BRINGUP.md`](../../docs/DRV8833_MOTOR_BRINGUP.md).

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
ordering). `MotorPriorityMode` (below) is a temporary diagnostic layer
built to test that hypothesis directly, before considering motor voltage,
PWM, or mechanical changes.

**Verified wiring** (J2-bridged DRV8833 board):

| Signal | Connection |
|---|---|
| DRV8833 IN1 | ESP32 GPIO8 |
| DRV8833 IN2 | ESP32 GPIO9 |
| DRV8833 VCC | ESP32 3.3V |
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
  wiring it into repeating `IDLE_SWAY`.

### Serial commands

| Command | Action |
|---|---|
| `f` | `MotorDriver` forward (continuous, fires immediately, no Enter needed) |
| `k` | Immediate stop — raw `MotorDriver` hold, any active `MotorBehavior` (forces mode to `OFF`), and any active `MotorPriorityMode`/priority test; releases/restores `MotorPowerGuard` |
| `0` | `MotorBehavior` OFF *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`, on by default)* — also releases `MotorPowerGuard` immediately |
| `1` | `MotorBehavior` IDLE_SWAY *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `2` | Boot-equivalent `MotorPriorityMode` runtime test (Preparing → Forward 250ms → Stop 250ms → Reverse 250ms → Stop → release → back to `MotorBehavior` OFF) *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |
| `?` | Print `MotorBehavior` mode/phase, `MotorPowerGuard` state, `MotorPriorityMode` state + LED/audio-suspended flags, and the priority test's own phase *(requires `ENABLE_MOTOR_BEHAVIOR_TEST`)* |

These fire on the single byte, unlike the Enter-terminated commands in
[Serial controls](#serial-controls) below. They're implemented as a
`Serial.peek()`-based interceptor in `main.cpp` that only consumes bytes
matching these reserved keys, leaving everything else untouched for
`Controls.cpp`'s own line-buffered parser — verified not to collide with
any existing single-char or word command (`n,p,o,x,+,-,m,d,h,g,r,b,a,c,v`,
`effects`/`overlays`/`status`). Two deliberate substitutions from what was
originally planned, both to avoid stealing bytes from existing commands:
`s` → `k` for motor-stop (`s` is the first letter of `status`), and `p` →
`2` for the boot-equivalent test (`p` is Controls.cpp's existing
"previous base effect" command).

### Safety behavior

- No command leaves the motor energized indefinitely by design: timed
  helpers (`motorForwardMs`/`motorReverseMs`) always stop themselves;
  `IDLE_SWAY`'s longest energized segment is 500ms; a generic 2s
  max-runtime safety net backstops all behaviors regardless of mode.
- `k` is a full emergency stop from any state (raw drive or any behavior).
- No current-sensing or thermal-monitoring hardware exists — over-current,
  stall, and thermal conditions cannot be detected in software.

### Current limitations

- No PWM/speed control yet.
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

The DRV8833 is currently powered from the **ESP32's own 3.3V rail**. This
is acceptable for verified bench bring-up only and should **not** be
treated as the final production power architecture. Before sustained or
production use, evaluate a dedicated external motor supply (sized for the
motor's actual current draw, including startup inrush) with a common
ground shared with the ESP32 — see
[Safety warnings](#safety-warnings) below.

`MotorPowerGuard`'s LED muting is a **temporary software workaround** for
continued bench development, not a power fix — it reduces LED current
draw during motor engagement but does not increase available current or
address the underlying shared-supply contention. See
`docs/DRV8833_MOTOR_BRINGUP.md` section 14 for the physical validation
that identified this contention.

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
- **Button 4 (GPIO5):** a dedicated, immediate ON/OFF button for the
  audio overlay -- no click-count, double-click, or hold detection of any
  kind (an earlier click-gesture state machine was removed entirely).
  The toggle fires on the debounced **press edge itself**, not on
  release, so a sustained hold produces exactly one toggle (the edge only
  fires once, on HIGH→LOW) and releasing does nothing beyond ordinary
  debounce bookkeeping. Prints `[BUTTON4] Audio overlay toggle`, then
  `[AUDIO] Overlay: ON` or `OFF`, then the matching `[CUE]` lines (green
  single-flash on enable, double red-flash on disable -- colors/timings
  unchanged).
  - Mic diagnostics via Button4 has been removed; use the `d` serial
    command instead.

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
- **The DRV8833 motor driver is currently powered from the ESP32's 3.3V
  rail** — a logic supply, not a motor supply, with limited current
  headroom. This is acceptable for verified bench bring-up only (see
  [Motor driver (DRV8833)](#motor-driver-drv8833) above); evaluate a
  dedicated external motor supply with common ground before any
  sustained or production use.
- Physical validation confirmed motor engagement visibly disturbs LED
  output while the two share this supply (motor movement is reliable
  with LEDs muted, inconsistent/weak while LEDs are active). The
  `MotorPowerGuard` LED-muting workaround reduces this contention for
  bench development; it is not a power fix.
