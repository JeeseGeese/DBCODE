# Expressive Motion Development — Sunflower ESP32-S3

**Branch:** `feature/expressive-motion-v1` (from tag `v1.0.0`)
**Status:** Implemented and software-validated only. **Not yet physically
validated** — natural-looking movement, audio responsiveness, mechanical
load, and motor/LED coexistence under this new behavior have not been
observed on the physical unit. See "Physical validation checklist" below.

This document covers the expressive-motion feature built on top of the
`v1.0.0` baseline (see `docs/DRV8833_MOTOR_BRINGUP.md` for the full motor
bring-up history that baseline represents — mechanical belt fix,
centralized serial dispatcher, reliable emergency stop, audio logging
toggle, LED index mapper). It does not modify or rewrite anything in that
baseline; it only adds a new, disabled-by-default layer on top.

## 1. Goal

Coordinate gentle, non-mechanical-looking motor movement with the
existing LED effects and audio-reactive overlay, without touching any of
the validated `v1.0.0` infrastructure (`MotorDriver`, `MotorPowerGuard`,
`MotorPriorityMode`, the centralized serial dispatcher, the diagnostics
`0`-`7`).

## 2. Architecture

New files:

- `include/ExpressiveMotion.h` / `src/ExpressiveMotion.cpp` — the
  controller itself.
- Constants: a new "Expressive motion" section in `include/Config.h`
  (all `MOTION_*` names), so tuning doesn't require reading the
  implementation.

Integration points (all in already-existing files, minimally touched):

- `main.cpp`: includes `ExpressiveMotion.h`; calls `initExpressiveMotion()`
  once in `setup()`; calls either `pauseExpressiveMotion()` (while any of
  `2`/`3`/`5`/`6` is active) or `updateExpressiveMotion(now)` every
  `loop()` iteration; adds `isExpressiveMotionMoving()` to each of those
  four diagnostics' own start-guards; implements
  `isAnyMotorDiagnosticActive()` (declared in `ExpressiveMotion.h`) so the
  controller can check the reverse direction; calls
  `cancelExpressiveMotion()` from `serviceEmergencyStop()`; calls
  `printExpressiveMotionDebugState()` from the `?` handler.
- `Controls.cpp`: adds the `motion` word command, routed through the
  *existing* Enter-terminated line parser — `dispatchMotionCommand()` is
  called from `dispatchCommand()`, which is itself fed one byte at a time
  by `main.cpp`'s central serial dispatcher via `feedSerialByte()`. **No
  new `Serial.read()`/`available()` call site was introduced anywhere.**

The controller never touches GPIO8/GPIO9 directly — it calls
`motorForward()`/`motorReverse()`/`motorStop()` (`MotorDriver.h`), the
same pattern already used by every existing motor diagnostic. It never
creates a second `Adafruit_NeoPixel` object or calls `strip.show()` — LED
coexistence is entirely delegated to `MotorPowerGuard`'s existing
`DIM_DURING_MOTION` mode (see `docs/DRV8833_MOTOR_BRINGUP.md` section 21).

### Mutual exclusion (bidirectional)

- `startPriorityTest()`/`startBreakawayTest()`/`startMotorLedTest()`/
  `startLedMap()` each refuse to start while `isExpressiveMotionMoving()`
  is true.
- Expressive motion's own idle-timer tick refuses to start a new pulse
  while `isAnyMotorDiagnosticActive()` is true, and `main.cpp`'s `loop()`
  additionally calls `pauseExpressiveMotion()` instead of
  `updateExpressiveMotion()` for as long as any diagnostic is active —
  releasing the motor/`MotorPowerGuard` immediately if expressive motion
  happened to be mid-pulse when the diagnostic started, and re-rolling a
  fresh idle timer once the diagnostic ends (so a pending movement isn't
  "saved up" and fired the instant the diagnostic finishes).
- `motion demo` additionally refuses to start while a diagnostic is
  active, and pauses ordinary expressive movement first if it was mid-pulse.

## 3. State machines

```cpp
enum class ExpressiveMotionMode { OFF, IDLE_ALIVE, AUDIO_REACTIVE };
```

(Named `OFF`, not `DISABLED` as originally proposed — `DISABLED` collides
with an ESP32 Arduino core macro, `esp32-hal-gpio.h`'s
`#define DISABLED 0x00` for GPIO pull-mode constants, and won't compile as
an enumerator name. `OFF` also matches the existing
`MotorBehaviorMode::OFF` naming convention already used in this exact
codebase.)

```cpp
enum class ExpressiveMotionPhase { IDLE, PREPARING, MOVING, STOPPING, RELEASING };
```

One pulse: `IDLE` (resting, counting down) → `PREPARING` (MotorPowerGuard
requested, waiting for READY) → `MOVING` (energized) → `STOPPING` (brief
full stop; may lead into a second "curious" pulse, back to `MOVING`) →
`RELEASING` (MotorPowerGuard settle/restore, `MotorLedPowerMode` restored
to `FULL_MUTE`) → back to `IDLE`.

```cpp
enum class MotionDemoPhase { IDLE, PREPARING, FORWARD, STOP1, REVERSE, STOP2, CURIOUS_PULSE, CURIOUS_STOP, RELEASING };
```

A separate, smaller one-shot state machine for `motion demo` (see section
8) — takes exclusive priority over the ordinary pulse machine above for
its brief duration.

## 4. IDLE_ALIVE timing

All constants in `Config.h`'s "Expressive motion" section:

| Constant | Value | Meaning |
|---|---|---|
| `MOTION_PULSE_MIN_MS` / `MOTION_PULSE_MAX_MS` | 100–220ms | one short movement |
| `MOTION_REST_MIN_MS` / `MOTION_REST_MAX_MS` | 900–3000ms | typical rest between movements |
| `MOTION_LONG_REST_MIN_MS` / `MOTION_LONG_REST_MAX_MS` | 4000–7000ms | occasional longer pause |
| `MOTION_LONG_REST_CHANCE` | 15% | fraction of idle cycles using the long-rest range |
| `MOTION_CURIOUS_CHANCE` | 20% | fraction of idle cycles that do a second "curious" pulse |
| `MOTION_INTRA_PULSE_STOP_MS` | 200ms | full stop between a curious double-movement's two pulses |
| `MOTION_MAX_CONSECUTIVE_SAME_DIR` | 2 | never more than this many same-direction pulses in a row |

Each idle cycle: roll a rest duration (occasionally the longer range),
roll whether this cycle will be a "curious" double movement, then once the
rest elapses, choose a direction (forced to switch if the consecutive-
same-direction cap was hit, otherwise random) and pulse for a random
100–220ms. If curious, stop for 200ms, choose again (cap still applies,
so this is where "occasionally move in the opposite direction" mostly
happens), pulse again, then release. `Config.h`'s constants are the single
place to retune this without touching the implementation.

## 5. AUDIO_REACTIVE bands, hysteresis, cooldowns

Reuses `AudioAnalyzer.h`'s existing `AudioFeatures.envelope` (already
attack/release-smoothed, 0..1) — no new microphone reader, no raw
per-sample decisions.

```cpp
enum class AudioActivityBand { QUIET, ACTIVE, STRONG };
```

Hysteresis (separate enter/exit thresholds so a level sitting right at a
boundary doesn't chatter):

| | Enter | Exit |
|---|---|---|
| ACTIVE | `MOTION_AUDIO_ACTIVE_ENTER` = 0.20 | `MOTION_AUDIO_ACTIVE_EXIT` = 0.12 |
| STRONG | `MOTION_AUDIO_STRONG_ENTER` = 0.55 | `MOTION_AUDIO_STRONG_EXIT` = 0.40 |

A movement only triggers on a **rising edge** into ACTIVE or STRONG (or on
`AudioFeatures.clap`, treated as an immediate STRONG trigger) — never on a
held level. This is why a sustained loud sound produces intermittent
movement rather than holding the motor energized: the edge only fires
once per crossing, each gated by its own cooldown:

- `MOTION_AUDIO_ACTIVE_COOLDOWN_MS` = 700ms (ordinary reaction: one pulse)
- `MOTION_AUDIO_STRONG_COOLDOWN_MS` = 1400ms (strong reaction: the same
  "curious" two-pulse pattern IDLE_ALIVE uses)

While the band is QUIET (or a trigger is on cooldown), AUDIO_REACTIVE mode
runs the *exact same* idle-timer logic as IDLE_ALIVE — deliberately no
separate code path, so "defer to IDLE_ALIVE behavior" is structural, not
duplicated logic.

## 6. LED coordination

Reuses `MotorPowerGuard`'s existing, already-validated `DIM_DURING_MOTION`
mode (see `docs/DRV8833_MOTOR_BRINGUP.md` section 21) at whatever test
level is currently selected via serial `4` (default 8/255) — no new LED
power strategy was built. The current base effect and overlay keep
running normally (not frozen, not replaced); brightness index, mute
state, base effect, and overlay selection are never touched by
expressive motion, so nothing needs explicit restoration.

**Motion accent (deferred):** the plan called for a subtle
"perceived pulse/highlight intensity" accent as movement begins,
fading back naturally. Implementing this without touching the shared
`LedEffects`/overlay rendering pipeline (which every other effect also
depends on) would require either a new per-frame hook into that pipeline
or a parallel brightness-modulation path — both larger, more invasive
changes than this feature's scope. Per the explicit instruction to defer
rather than refactor the effects engine if an accent can't be added
cleanly, **only brightness coordination (the existing `DIM_DURING_MOTION`
mode) is implemented; no accent exists.**

## 7. Commands

`motion` is a **word command** (Enter-terminated, dispatched through
`Controls.cpp`'s existing line parser — see Architecture above), chosen
over a single reserved byte because the single-character space
(`f`,`k`,`0`-`7`,`?` in the central dispatcher; `n,p,o,x,+,-,m,d,h,g,r,b,a,c,v`
in `Controls.cpp`) is already fully committed.

| Command | Effect |
|---|---|
| `motion` / `motion next` | cycle `OFF → IDLE_ALIVE → AUDIO_REACTIVE → OFF` |
| `motion off` | `ExpressiveMotionMode::OFF` |
| `motion idle` | `ExpressiveMotionMode::IDLE_ALIVE` |
| `motion audio` | `ExpressiveMotionMode::AUDIO_REACTIVE` |
| `motion status` | prints the same `[MOTION]` line `?` includes |
| `motion demo` | runs the one-shot demo below |

`?` also always includes a `[MOTION]` line: mode, phase, direction, ms
until the next eligible movement, audio band, both cooldowns remaining,
whether the demo is active, `MotorPowerGuard` state, the current
motion-motion LED brightness, and whether motion is currently inhibited
by an emergency stop.

## 8. `motion demo`

A one-shot, non-blocking demonstration, independent of whatever
`ExpressiveMotionMode` was selected before it ran (saved and restored on
normal completion):

```
[MOTION DEMO] Preparing
[MOTION DEMO] Gentle forward
[MOTION DEMO] Stop
[MOTION DEMO] Gentle reverse
[MOTION DEMO] Stop
[MOTION DEMO] Curious movement
[MOTION DEMO] Restoring
[MOTION DEMO] Complete
```

("Curious movement" is itself a forward pulse, a full 200ms stop, then a
reverse pulse — the same two-pulse-with-a-stop-between pattern as an
ordinary curious double movement, matching the STRONG audio-reaction
shape.) Refuses to start while any of `2`/`3`/`5`/`6` is active or while
already running; pauses ordinary expressive movement first if it happened
to be mid-pulse; cancels immediately and completely via `k` (does **not**
restore the previous mode on emergency cancel — it forces `OFF`, same as
every other emergency-stop path); never repeats automatically.

**Does not itself claim the resulting movement looks natural** — that is
exactly what physical testing is for.

## 9. Emergency-stop behavior

`k` (via `main.cpp`'s existing `serviceEmergencyStop()` latch) now also
calls `cancelExpressiveMotion()`, which:

- stops the motor and releases `MotorPowerGuard` immediately if a pulse
  or `motion demo` was in flight,
- restores `MotorLedPowerMode` to `FULL_MUTE` (see the bug note below),
- and **forces `ExpressiveMotionMode` to `OFF`** — unlike the four
  diagnostics (which simply return to `IDLE`, ready for the next explicit
  command), expressive motion is a *continuous, autonomous* behavior, so
  merely stopping the current pulse isn't enough: it would just start
  another one shortly after. `k` must inhibit it until a deliberate
  `motion` command re-enables it. `?`'s `emergencyStopped=1` flag reflects
  this until the next explicit `motion` command of any kind (including
  `motion off`) clears it.

**Bug found and fixed during validation:** the ordinary pulse-completion
path (and the pause/cancel paths) initially did not restore
`MotorLedPowerMode` to `FULL_MUTE` after releasing — only `motion demo`'s
own completion did. Since `2`/`3` rely on `FULL_MUTE` already being the
standing default and never set it themselves, this meant that after
expressive motion moved even once, `2`/`3` would silently inherit
`DIM_DURING_MOTION` behavior instead of muting LEDs as documented. Caught
by the `?`-status regression check, not by inspection. Fixed by restoring
`FULL_MUTE` in all three release paths (ordinary pulse RELEASING,
`forceReleaseOrdinaryMovement()`, `forceReleaseDemo()`); re-validated —
`?` now shows `mode=FULL_MUTE` after any expressive-motion release.

## 10. Safety review (software-checked)

- `MotorDriver` remains the sole GPIO8/GPIO9 owner — `ExpressiveMotion.cpp`
  only calls its exported functions.
- The single `Adafruit_NeoPixel strip` in `main.cpp` remains the sole LED
  owner — no second object, no new `strip.show()` call site.
- The centralized serial dispatcher in `main.cpp` remains the sole
  `Serial.read()`/`available()` owner — `motion`/`motion demo` are parsed
  entirely inside `Controls.cpp`'s existing line buffer.
- No `delay()` anywhere in `ExpressiveMotion.cpp`.
- No instantaneous polarity reversal: every direction change (ordinary
  pulses, curious double movements, and the demo) passes through
  `motorStop()` first, with a real stop interval before the next
  `motorForward()`/`motorReverse()`.
- Every pulse is well under the existing 2000ms max-energized safeguard
  (100–220ms ordinary, 200ms demo segments).
- `k` cancels expressive motion and `motion demo` reliably and forces
  `OFF` (verified: emergency stop mid-demo, mid-pulse).
- Diagnostics `2`/`3`/`5`/`6` have priority — expressive motion cannot
  start or continue while any is active (verified both directions).
- Existing LED effects, button behavior, and audio logging default are
  completely unchanged when expressive motion is `OFF` (the default).
- No out-of-range LED access, no duplicate `strip.show()` ownership (this
  feature never touches `strip` at all).
- No reset, watchdog, panic, or brownout observed during any test run.

## 11. Physical validation checklist (not yet performed)

- Run `motion demo` and observe: does the "gentle forward"/"gentle
  reverse"/"curious movement" sequence look reasonable given the belt/
  mechanism, or do the pulse durations need retuning?
- With `motion idle` enabled for an extended period, observe: does the
  idle timing feel "alive" rather than mechanical? Does the
  no-more-than-2-consecutive-same-direction rule produce visible variety?
- With `motion audio` enabled, play sounds at varying levels and observe:
  does QUIET correctly fall back to idle-like behavior, does ACTIVE
  produce occasional single pulses, does STRONG (or a clap) produce the
  two-pulse reaction, and does a sustained loud sound avoid holding the
  motor energized?
- Confirm LEDs remain visibly active (not full-strip white, not frozen)
  throughout motion at the current `4`-selected brightness level, and
  that they visually restore correctly after each movement.
- Confirm `k` physically halts the flower immediately from every mode and
  phase, and that it does not resume moving on its own afterward.
- Confirm no audible motor strain, stalling, or excessive current draw
  during repeated short pulses.
- Only after the above is reviewed should `motion idle` or `motion audio`
  be left running for extended unattended operation.

## 12. Known limitations

- No motion accent (see section 6) — deferred, not implemented.
- Timing constants are conservative defaults, not yet tuned against the
  physical mechanism.
- `AUDIO_REACTIVE` has not been validated against real audio content of
  varying character (music vs. speech vs. claps) — only against a quiet
  room (no false triggers observed) in software validation.
- Expressive-motion mode is not persisted (NVS) — always boots to `OFF`.
  See TASK 9's roadmap note on `?` status persistence if this becomes
  desirable later.

---

# Roadmap: Planning Only (No Implementation Below This Line)

Everything from here down is **documentation and architecture planning**
for future milestones. **Nothing described below has been implemented.**
No GPIO pins have been assigned beyond what's already confirmed working in
`v1.0.0`. No I2S driver changes, no audio files, no new libraries, no
button/switch code, and no changes to the `v1.0.0` tag were made as part
of this planning.

## 13. Milestone phases

```
Phase 1 — v1.0 verified embedded baseline                 [DONE, tagged v1.0.0]
Phase 2 — expressive motor/audio/LED behavior              [THIS BRANCH -- software-validated,
                                                              physical validation pending]
Phase 3 — speaker and digital amplifier integration        [NOT STARTED]
Phase 4 — spoken setting announcements                     [NOT STARTED]
Phase 5 — Raspberry Pi/LLM speech integration               [NOT STARTED]
```

**Phase 3 must begin only after Phase 2's movement behaviors are
physically approved** (see section 11's checklist and section 20's
physical bring-up order).

## 14. Future announcement architecture (planning only)

Central API — nothing in the codebase should call speaker/I2S functions
directly; every module (button handlers, `Controls.cpp`, expressive
motion, a future LLM bridge) requests announcements through this instead:

```cpp
enum class AnnouncementId {
  POWER_ON, POWER_OFF, MUTED, UNMUTED,
  BRIGHTNESS_LOW, BRIGHTNESS_MEDIUM, BRIGHTNESS_HIGH,
  MODE_CHANGED, COLOR_MODE_COOL, COLOR_MODE_HOT, COLOR_MODE_FULL,
  AUDIO_OVERLAY_ON, AUDIO_OVERLAY_OFF,
  MOTION_OFF, MOTION_IDLE, MOTION_AUDIO,
  DIAGNOSTIC_STARTED, DIAGNOSTIC_COMPLETE,
  ERROR_AUDIO_OUTPUT, ERROR_MOTOR, ERROR_MICROPHONE,
};

bool requestAnnouncement(AnnouncementId id);
bool isAnnouncementPlaying();
void cancelAnnouncement();
void updateAnnouncementSystem(uint32_t nowMs);
```

## 15. Two types of spoken output (planning only)

**A. Local pre-recorded announcements** (flash/local storage, work
without a Raspberry Pi/network/LLM): "Muted", "Unmuted", "Brightness
low/medium/high", "Audio mode on/off", "Idle movement", "Audio reactive
movement", "Diagnostic started/complete".

**B. Dynamic LLM speech**, generated by a Raspberry Pi or other
higher-level processor. Candidate transports (none chosen yet):

- USB serial command plus streamed audio data
- Wi-Fi socket
- Bluetooth
- Local network HTTP/WebSocket
- Direct I2S, only if the board topology actually supports it

**Recommendation:** keep real-time motor safety (emergency stop, the
2000ms safeguard, `MotorPowerGuard`) entirely on the ESP32 regardless of
which transport is chosen — the higher-level computer should never be a
dependency for `k` to work.

## 16. I2S resource ownership (planning only)

The INMP441 currently owns `I2S_NUM_0` for receive. Before integrating a
MAX98357A (or whatever amplifier photographing the board confirms),
inspect ESP32-S3's I2S peripheral count/capabilities and this codebase's
architecture, then give the amplifier **one explicit owner**:

```cpp
struct AudioOutputConfig { int bclkPin; int lrclkPin; int dataPin; uint32_t sampleRate; };
bool beginAudioOutput(const AudioOutputConfig &config);
bool queuePcm(const int16_t *samples, size_t sampleCount);
bool playAnnouncement(AnnouncementId id);
void stopAudioOutput();
void updateAudioOutput(uint32_t nowMs);
bool isAudioOutputBusy();
```

No module should install, reconfigure, or own the amplifier's I2S
peripheral independently of this. Whether microphone RX and speaker TX
end up on separate I2S controllers, a full-duplex-capable configuration,
or carefully coordinated sharing of one controller is **not yet
determined** — do not assume they can safely share clocks or a port
without testing.

## 17. Feedback prevention (planning only)

The INMP441 will hear the speaker when it talks. Minimum planned
behavior, gated by a visible playback state:

```cpp
enum class AudioInteractionState { LISTENING, SPEAKING, POST_SPEECH_COOLDOWN };
```

While `SPEAKING`: suppress clap detection, suppress/attenuate
audio-reactive motor triggers, optionally suppress the audio-reactive LED
overlay, keep microphone sampling running if useful for other purposes.
`POST_SPEECH_COOLDOWN` target: 300–800ms, tunable after physical testing.
The sunflower must never be able to trigger its own audio-reactive
movement repeatedly from its own speaker.

## 18. Announcement priorities (planning only)

A small, bounded, fixed-size queue (no dynamic allocation):

- **Highest:** power off, emergency/critical error, explicit cancel
- **High:** mute/unmute, motor or microphone fault
- **Normal:** mode change, motion-mode change, audio-overlay change
- **Low:** brightness announcement, diagnostic completion

Duplicate announcements close together coalesce; rapid brightness cycling
announces only the final selected value; emergency stop cancels
noncritical speech; speech must never block `motorStop()`, LED rendering,
serial service, or button processing.

## 19. LED/movement synchronization during speech (planning only)

Future ideas only, not implemented: a subtle mouth-like/center-petal LED
pulse based on playback amplitude, gentle short nods between phrases, no
continuous motor energization, no strong audio-reactive motor response to
the speaker's own voice. Speech-driven movement must remain subordinate
to emergency stop, the max-energized safeguard, `MotorPowerGuard`, and
diagnostic mutual exclusion — the same rules everything else already
follows.

## 20. Future physical bring-up order (planning only)

1. Finish and physically approve expressive motion (this branch).
2. Confirm amplifier model and photograph both sides.
3. Confirm speaker impedance and power rating.
4. Audit available ESP32 pins (see the table below).
5. Connect amplifier power and ground only.
6. Measure supply voltage.
7. Connect I2S control/data pins.
8. Run a low-volume test tone.
9. Verify no ESP32 reset or motor disturbance.
10. Play one short prerecorded announcement.
11. Add the announcement queue and setting events.
12. Test microphone feedback suppression.
13. Only then begin dynamic LLM/TTS audio transport.

The MAX98357A's speaker output must connect only to the amplifier's own
speaker-output terminals — do not connect either speaker terminal
directly to ESP32 ground unless that exact amplifier board's
documentation explicitly requires it (MAX98357A speaker output is
normally bridge-tied, i.e. neither terminal is ground-referenced).

## 21. Future optional buttons 5 and 6 (planning only)

One or two additional momentary pushbuttons may be added later, through
the existing `Controls.cpp` debounce architecture — **not** yet wired,
**not** yet assigned GPIOs. The low-level debounce layer should only ever
produce semantic events; motor/LED/speaker/LLM logic belongs in
higher-level controllers, not in the debounce code itself:

```cpp
enum class ExtraButtonEvent {
  BUTTON5_SHORT, BUTTON5_DOUBLE, BUTTON5_LONG,
  BUTTON6_SHORT, BUTTON6_DOUBLE, BUTTON6_LONG,
};
```

**Proposed Button 5 role — motion:** short press cycles
`OFF → IDLE_ALIVE → AUDIO_REACTIVE → OFF` (same as the `motion` word
command); long press runs `motion demo` if idle, or cancels it if already
running; an optional double press could trigger one safe expressive
reaction without enabling continuous movement.

**Proposed Button 6 role — voice/LLM:** short press toggles spoken
setting announcements on/off; long press is a future push-to-talk/explicit
listening request; double press repeats the most recent announcement.

Once the amplifier exists, Button 5/6 actions would request announcements
through the central API in section 14 (e.g. "Movement off", "Idle
movement", "Voice prompts on", "Listening") — never play audio directly
from a button handler.

**Electrical assumption** (to be confirmed by hardware inspection): one
terminal to the selected GPIO, one to GND, `INPUT_PULLUP`, pressed = LOW,
software debounce, no external resistor expected.

**Safety notes carried forward from the existing button/motor design:**
`k` remains independent of all button processing; a future dedicated
physical emergency-stop button may be considered separately; buttons must
never bypass `MotorDriver`/`MotorBehavior`; holding or chattering a button
must never hold the motor energized; repeated events must be rate
limited; long press fires once per hold; double press must not also fire
the short-press action unless deliberately designed as delayed
single-click handling.

## 22. Future latching switches (planning only)

One or two panel-mounted latching switches (as an alternative to momentary
buttons) for conversation/voice controls — treated as **persistent
physical state**, not button-toggle events. Not yet wired, not yet
assigned GPIOs.

**Switch 1 — conversation enable:**

```cpp
enum class ConversationMode { DISABLED, ENABLED };
```

OPEN/HIGH = listening disabled, CLOSED/LOW = enabled. Must **not** disable
the INMP441 hardware itself — audio-reactive LED processing may continue
regardless; this switch only controls whether microphone audio is
eligible for speech recognition/LLM input. On change, publish a semantic
event (`CONVERSATION_ENABLED`/`CONVERSATION_DISABLED`) rather than
starting/stopping I2S directly from the switch handler.

**Switch 2 — spoken announcements enable:**

```cpp
enum class VoicePromptMode { DISABLED, ENABLED };
```

OPEN/HIGH = local spoken prompts disabled, CLOSED/LOW = enabled. Affects
local spoken prompts only — must not mute LLM speech replies, diagnostic
audio, the amplifier hardware, or alter LEDs/motor behavior. Turning
prompts off should not recursively announce itself doing so unless that's
deliberately designed in.

**Electrical model** (to be confirmed by hardware inspection): one
terminal to GPIO, one to GND, `INPUT_PULLUP`, open = HIGH, closed = LOW,
low-current signal use only — never route motor, amplifier, speaker, or
system power through these GPIOs.

**Software requirements:** debounce transitions; sample actual physical
position at boot (software state must never drift from the switch);
`status` reports both physical and logical state; chatter must not
repeatedly start/stop conversation mode; changing a switch must never
block serial, motor, LEDs, microphone, or audio playback; no spoken
announcements queued during early boot unless audio output is fully
initialized; print one concise boot line per switch once sampled.

**Conversation safety (future):** even while conversation mode is
enabled, suppress speech recognition while the speaker is actively
talking (`SPEAKING`/`POST_SPEECH_COOLDOWN`, section 17), preventing the
sunflower from transcribing its own voice; emergency stop and all
real-time controls remain responsive regardless; optionally return to
listening automatically after the cooldown.

## 23. GPIO ownership table

Confirmed, currently wired (unchanged by this branch):

| Signal | GPIO | Owner |
|---|---|---|
| WS2812 LED data | 4 | `main.cpp`'s single `strip` object |
| Button: Mode | 10 | `Controls.cpp` |
| Button: Mute | 11 | `Controls.cpp` |
| Button: Brightness | 17 | `Controls.cpp` |
| Button 4 | 5 | `Controls.cpp` |
| INMP441 I2S BCLK | 6 | `AudioAnalyzer.cpp` (`I2S_NUM_0`, RX) |
| INMP441 I2S WS | 7 | `AudioAnalyzer.cpp` (`I2S_NUM_0`, RX) |
| INMP441 I2S DIN | 15 | `AudioAnalyzer.cpp` (`I2S_NUM_0`, RX) |
| DRV8833 IN1 (motor) | 8 | `MotorDriver.cpp` (sole owner) |
| DRV8833 IN2 (motor) | 9 | `MotorDriver.cpp` (sole owner) |
| DRV8833 SLEEP/nSLEEP | *(not wired/driven by firmware)* | — |

**Unassigned, planning only — not wired, no GPIO chosen:**

| Signal | GPIO | Status |
|---|---|---|
| MAX98357A BCLK | TBD | blocked on amplifier confirmation + pin audit |
| MAX98357A LRC | TBD | blocked on amplifier confirmation + pin audit |
| MAX98357A DIN | TBD | blocked on amplifier confirmation + pin audit |
| BUTTON_5_GPIO | TBD | unassigned, not wired |
| BUTTON_6_GPIO | TBD | unassigned, not wired |
| CONVERSATION_SWITCH_GPIO | TBD | unassigned, not wired |
| VOICE_PROMPT_SWITCH_GPIO | TBD | unassigned, not wired |

The eventual audit (before any of the above are assigned) must reject:
pins already in the confirmed table above; flash/PSRAM-reserved pins;
USB/JTAG-sensitive pins where inappropriate; boot-strapping pins that
could prevent startup if held during power-on; pins needed for the
amplifier; any other board-specific unavailable pin on this exact
ESP32-S3-DevKitC-1 module.
