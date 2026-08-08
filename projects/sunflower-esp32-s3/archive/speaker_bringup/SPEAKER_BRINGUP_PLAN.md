# MAX98357A Speaker Bring-Up Plan

Written 2026-07-31 as a planning-only document, the evening the Unified
Audio Mode milestone (`sunny-audio-mode-v1-physical-validation`) was
finalized. **Updated 2026-08-01:** Stage S0 preflight is documented in
full (checklist below) and Stage S1 (silent I2S TX) is code-complete;
Stage S2 (the bring-up tone) is prepared but deliberately not yet run —
see "Software: planned implementation stages" below for exactly what
changed and what still requires physical hardware observation. None of
this touches Audio Mode, DanceEngine, or MusicMotorController.

**Scope note:** this is a near-term, concrete bring-up plan for the
*existing* wiring/output-verification work (`SpeakerTest`) through basic
embedded-audio playback. It is deliberately narrower than
`docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` sections 17–22, which plan the
*future* LLM/TTS speech-transport architecture (announcement queues,
feedback-suppression state machine, LED/movement sync during speech).
Read this plan's Stage S5 as a bridge toward that later work, not a
replacement for it — do not duplicate those sections here, and keep this
document's own scope to "make real sound come out reliably."

## Current state (what already exists, verified where noted)

- `include/SpeakerTest.h` / `src/SpeakerTest.cpp` — a temporary, isolated
  MAX98357A bring-up/diagnostic module. Generates continuous digital
  silence plus on-request test signals (tones, sweeps, a small procedural
  melody player, the `speaker tone`/`speaker stop` Stage S2 bring-up pair,
  plus the `speaker t`/`1`/`2`/`3`/`v`/`+`/`-`/`h` gain/volume sweep bench
  addendum -- see Stage S2 below). Does **not** route live microphone audio to the
  speaker. As of 2026-08-01, the speaker is silent-only at boot by
  default — the old automatic ~5s-after-boot demonstration tone is
  disabled (`ENABLE_SPEAKER_AUTO_DEMO_TONE=0`, `include/Config.h`); every
  sound now requires an explicit serial command.
- `include/SharedI2S.h` / `src/SharedI2S.cpp` is the sole owner of
  `I2S_NUM_0`: one full-duplex master port (`I2S_MODE_MASTER |
  I2S_MODE_RX | I2S_MODE_TX`), 16kHz, 32-bit-per-slot,
  `I2S_CHANNEL_FMT_RIGHT_LEFT` (true stereo). `AudioAnalyzer.cpp` only
  calls `i2s_read()`; `SpeakerTest.cpp` only calls
  `i2s_write()`/`i2s_zero_dma_buffer()`. Exactly one
  `i2s_driver_install()` call exists in the firmware.
- **Software-validated / write-path verified:** `i2s_write()` reports
  100% write success in serial logs (per `CURRENT_STATUS.md`). This is
  **not** the same as confirmed audio quality — nobody has yet listened
  to the amplifier's actual output and judged it clean, correctly
  leveled, and free of the digital-silence-artifact class of problems.
- **Not yet built:** any real embedded PCM/WAV sample playback.
  `SpeakerTest.cpp`'s "music" commands are procedurally generated tones
  (a `Note`/`Song` data model over a `songSample()` synthesis function),
  not decoded audio samples.

## Hardware

### MAX98357A amplifier assumptions

- Chip is assumed to be a MAX98357A based on existing code/docs
  (`SpeakerTest.h`, `AGENTS.md` hardware summary). `docs/
  EXPRESSIVE_MOTION_DEVELOPMENT.md` section 17 flags this as
  "MAX98357A (or whatever amplifier photographing the board confirms)" —
  **the exact chip has not been visually/physically re-confirmed since
  that note was written.** Tomorrow's Stage S0 should include a quick
  visual check (chip marking) if the board is accessible without
  desoldering anything.
- `VIN` wired to the shared **5V** supply rail — the same rail powering
  the WS2812B LED strip and the DRV8833 motor driver. `GND` is the
  common ground shared across all subsystems. (Corrected 2026-08-01: an
  earlier version of this document incorrectly stated `VIN` ran from the
  ESP32's 3.3V pin, matching a since-identified-as-wrong claim in
  `AGENTS.md`/`README.md`'s own text. The ESP32 itself only supplies
  3.3V to the INMP441 microphone — LEDs, amplifier, and motor driver are
  all 5V-rail loads. `AGENTS.md`/`README.md` have not been corrected as
  part of this pass — only this planning document and the related
  `CURRENT_STATUS.md` note were in scope.)
- `SD` (shutdown) is **manually** moved by hand between GND (disabled)
  and 3.3V (enabled) — it is not GPIO-driven by firmware. The existing
  startup safety sequence (README "Speaker hardware test" section)
  requires confirming `[SPEAKER] Digital silence active` on serial
  *before* moving `SD` to 3.3V.
- `GAIN` pin state is **undocumented anywhere in this repo** — genuinely
  unknown whether it's floating (typically ~9dB on the real MAX98357A
  per its datasheet's default-float behavior) or tied to a specific
  resistor/pin. Do not assume a gain value without checking the physical
  board.

### Current unknowns

- Actual audio quality: clarity, distortion, correct pitch, absence of
  clicks/pops at start/stop of playback — **nothing beyond write-path
  success has been confirmed.**
- Actual output volume/loudness at the connected speaker.
- `GAIN` pin wiring (see above).
- Exact amplifier chip identity (see above).
- Whether simultaneous motor + LED + speaker activity causes audible
  noise, dropouts, or a brownout (see Risks below).
- Whether the current 16kHz sample rate (chosen for the microphone side)
  produces acceptable audio quality for played-back content, or whether
  speaker playback should eventually move to a different rate on a
  separate I2S port.

### Expected speaker requirements

- 8Ω / 0.5W speaker, per `AGENTS.md`'s hardware summary — this is the
  only speaker spec currently on record for this build. Confirm the
  physically connected speaker still matches this before driving it at
  any new volume level.

### Expected power requirements

**Confirmed power layout** (2026-08-01):

- 5V rail powers the WS2812B LEDs.
- 5V rail powers the MAX98357A amplifier.
- 5V rail powers the DRV8833 motor driver.
- 3.3V (from the ESP32) powers only the INMP441 microphone.
- All subsystems share a common ground.

This means the amplifier, LEDs, and motor are three **simultaneous 5V
loads on one shared supply** — not, as an earlier version of this
document incorrectly claimed, an amplifier-on-3.3V-alongside-the-ESP32's-
own-logic-rail situation. The correct electrical risk is:

> The shared 5V supply, distribution wiring, connectors, and grounding
> must support the combined peak load and noise generated by the LEDs,
> amplifier, and motor.

No current-draw measurement of the amplifier (or the combined 5V load)
at any real playback/motor-active volume exists yet — see Stage S0 below
for the full preflight list this risk requires before any audio-quality
testing begins.

### Required wiring information (as currently documented)

| Signal | GPIO | Notes |
|---|---|---|
| MAX98357A BCLK | 6 | shared with INMP441 SCK/BCLK, `SharedI2S.cpp` |
| MAX98357A LRC/WS | 7 | shared with INMP441 WS/LRCLK, `SharedI2S.cpp` |
| MAX98357A DIN | 16 | I2S data OUT from ESP32 to amplifier |
| MAX98357A VIN | shared 5V rail | same 5V supply as the WS2812B LEDs and DRV8833 |
| MAX98357A GND | common GND | shared with everything else |
| MAX98357A SD | manual (GND/3.3V) | **not** a GPIO; hand-moved per startup sequence |
| MAX98357A GAIN | unknown/undocumented | verify against physical board before assuming a value |

### Candidate GPIO mappings

No new GPIOs are anticipated for Stage S0–S4 — wiring above is already
in place and is not expected to change. If a future stage needs firmware
control of `SD` (to let software mute/unmute the amp instead of a manual
wire move), that would need a **new, currently-unassigned** GPIO — see
`AGENTS.md` section 2's explicit "do not invent a pin number... without a
hardware audit" list before proposing one.

### Possible I2S architecture

- **Current (in use):** one shared full-duplex `I2S_NUM_0` port for both
  RX (mic) and TX (speaker). This replaced an earlier two-controller
  design that failed conclusively on real hardware (`i2s_write()` on a
  slave TX port always returned `bytesWritten=0`; an unbounded wait froze
  the whole application). Do not resurrect the two-controller approach
  without new evidence it would work differently this time.
- **Open question carried from `EXPRESSIVE_MOTION_DEVELOPMENT.md` section
  17:** whether the *current* shared-port architecture remains
  appropriate once real embedded playback (Stage S3) needs sustained,
  glitch-free TX throughput at possibly a different sample rate than the
  mic's 16kHz RX — not yet determined, do not assume either way without
  testing.

### Risks and open questions

- **Shared 5V rail risk:** the WS2812B LEDs, MAX98357A amplifier, and
  DRV8833 motor driver are three simultaneous loads on one shared 5V
  supply — untested in combination. `AGENTS.md` already documents motor
  engagement disturbing LEDs on this shared supply; the amplifier adds a
  third simultaneous load (plus its own switching/PWM-driven noise) that
  has never been measured. The correct framing: *the shared 5V supply,
  distribution wiring, connectors, and grounding must support the
  combined peak load and noise generated by the LEDs, amplifier, and
  motor.* Stage S0 preflight must confirm, before any audio-quality
  testing begins:
  - Exact 5V supply type (wall adapter, USB, battery, bench supply —
    whichever is actually connected)
  - Rated continuous current of that supply
  - Rated peak current of that supply
  - Wire gauge used for the 5V distribution
  - Connector and breadboard/current-path limitations (breadboard rails
    and jumper connections are a common hidden current bottleneck)
  - Where exactly the 5V rail branches to the LEDs, amplifier, and
    DRV8833 (star topology vs. daisy-chained — affects both current
    capacity and noise coupling)
  - Common-ground layout (single star ground point vs. daisy-chained
    ground — affects noise coupling between the three loads)
  - Bulk capacitance present on the 5V rail
  - Local decoupling capacitance near the amplifier and near the motor
    driver specifically (not just bulk capacitance at the supply)
  - Motor-noise suppression in place (or not) — brushed DC motors are a
    significant EMI/conducted-noise source that can couple into the
    amplifier's audio output
  - Voltage measured at the amplifier's `VIN` pin *during* motor and LED
    activity, not just at rest — this is the actual test that answers
    whether the shared rail can support the combined load
- **I2S contention:** speaker playback and microphone capture share one
  physical port. `SpeakerTest`'s existing silence-write-every-loop
  approach was designed around this; real PCM playback (Stage S3) will
  need to confirm it doesn't starve or corrupt microphone reads (or
  vice versa) under sustained load.
- **Acoustic feedback:** the INMP441 will hear the speaker. Out of scope
  for Stage S0–S4 (no live-audio routing exists yet), but Stage S5 must
  not begin without reading `EXPRESSIVE_MOTION_DEVELOPMENT.md` section
  18's feedback-prevention plan first.
- **Unconfirmed amplifier identity/gain** (see Hardware section above) —
  resolve before assuming any specific loudness/quality target.
- **No embedded audio asset pipeline exists yet** — Stage S3 needs a
  decision on format (raw PCM vs. WAV-with-header), storage (PROGMEM/
  SPIFFS/LittleFS — none currently used for audio in this project), and
  a size budget against this board's flash (16MB total, current firmware
  uses well under 1%).

## Software: planned implementation stages

None of the following stages are implemented tonight. Each should get
its own physical build/upload/test cycle — do not batch multiple stages
into one untested change.

- **Stage S0 — Preflight and architecture verification.** ✅ Documentation
  complete 2026-08-01 — see "Stage S0 hardware preflight checklist" below
  for the full, itemized electrical/power/software checklist. Confirm
  amplifier chip identity if physically accessible; confirm speaker spec
  (8Ω/0.5W) matches what's connected; confirm `GAIN` pin state; re-read
  the README "Speaker hardware test" startup safety sequence in full.
  **Every checklist item requires direct hardware observation — none are
  satisfied by this document existing.**
- **Stage S1 — Silent I2S TX initialization.** ✅ Code complete
  2026-08-01. The shared-bus init (`SharedI2S.cpp`) and `SpeakerTest.cpp`
  already existed and were structurally sound (audited, not rewritten —
  see "I2S architecture audit" below); the one necessary fix was
  disabling `SpeakerTest`'s automatic demonstration tone
  (`ENABLE_SPEAKER_AUTO_DEMO_TONE`, now `0` by default in `Config.h`),
  which previously played an audible 440Hz tone ~5s after boot with no
  serial command — that contradicted this stage's own "no audible test
  at boot, speaker output remains disabled by default" requirement.
  Sunny's default boot now transmits digital silence only, permanently,
  until a human sends an explicit serial command. Host-tested
  (`test_host/speaker_bringup.cpp`); **not yet physically re-verified**
  that the silence is genuinely inaudible on the real amplifier/speaker
  (see checklist below).
- **Stage S2 — Low-volume generated tone.** ✅ Prepared, **not run**,
  2026-08-01. New `speaker tone` / `speaker stop` serial commands
  (`startSpeakerBringupTone()`/`stopSpeakerBringupTone()`,
  `src/SpeakerTest.cpp`) — 440Hz, 300ms, 5% amplitude, fixed 16kHz
  sample rate, mono content duplicated to both I2S stereo slots (see
  "Stage S2 tone-command design" in the task report for the full
  rationale for each value). Explicit serial command only; never fires
  automatically; refuses if speaker init failed; bounded duration,
  auto-stops, cannot latch on. Deliberately separate constants from both
  the existing `t`/`s`/`low`/`mid`/`high`/`sweep` diagnostic suite and
  the `loud` diagnostic — this is the one command meant to be the FIRST
  sound a human ever triggers during physical bring-up. **Do not send
  `speaker tone` until every Stage S0 checklist item below has been
  physically confirmed.**

  **Stage S2 addendum (added on top of the above, not yet run):** a small
  gain/volume sweep bench, `speaker t`/`1`/`2`/`3` (440Hz/750ms,
  220Hz/750ms, 440Hz/750ms, 880Hz/500ms) plus `speaker v`/`+`/`-` for a
  runtime-adjustable amplitude (2/5/8/12/18/25% ladder, default 5%, never
  full-scale) and `speaker h` for its own help -- see
  `src/SpeakerTest.cpp`'s `startSpeakerBenchT()`/`1()`/`2()`/`3()` and
  `include/Config.h`'s `SPEAKER_BENCH_*` constants. `speaker stop` (above)
  doubles as this bench's stop command. Namespaced under the existing
  `speaker` word prefix rather than given bare single-char tokens
  (`t`/`1`/`2`/`3`/`v`/`+`/`-`/`h`) because every one of those bytes is
  already reserved by another currently-working command -- `t`/`s` are the
  original speaker sine/square tests, `v` is `printAudioVisualState()`,
  `h` is general help, `+`/`-` are LED brightness, and bare `1`/`2`/`3` are
  live, no-Enter `main.cpp` motor-test triggers
  (`IDLE_SWAY`/priority-test/breakaway-test, `ENABLE_MOTOR_BEHAVIOR_TEST`)
  -- repurposing any of those would either silently break an existing
  verified control or risk firing a real motor test. Host-tested
  (`test_host/speaker_bench.cpp`, 8 items). Same physical-bring-up rule
  applies: **do not send any `speaker t`/`1`/`2`/`3` command until Stage S0
  has been physically confirmed.**
- **Stage S3 — Embedded PCM/WAV playback.** Not yet implemented. Decide
  on an audio-asset format/storage approach (see Risks above), add a
  minimal sample player alongside the existing procedural
  `songSample()` engine (or replacing it for this purpose), and play one
  short embedded clip end-to-end.
- **Stage S4 — Concurrency testing.** Not yet implemented/tested. With
  Stage S3 playback working, physically test simultaneous: speaker
  playback + LED audio overlay active + `MusicMotorController` driving
  the motor + microphone capture running. Watch for I2S contention,
  audible motor-induced noise, LED flicker, or a brownout under combined
  load.
- **Stage S5 — Future behavioral integration.** Not yet implemented, not
  yet designed in detail. Once S0–S4 are physically validated, resume
  planning from `EXPRESSIVE_MOTION_DEVELOPMENT.md` sections 18–21
  (feedback prevention, announcement priorities, LED/movement sync during
  speech, the 13-step future bring-up order) rather than re-deriving that
  plan here.

## Stage S0 hardware preflight checklist

Added 2026-08-01. **Every item below requires direct hardware
observation** (a multimeter reading, a continuity check, a visual
inspection) — none are satisfied by this document existing, and none
should be checked off from memory or assumption. Complete this entire
checklist, in order, before ever sending `speaker tone`.

### Electrical verification

- [ ] MAX98357A `VIN` connected to the shared 5V rail (not the ESP32's
      3.3V pin — see `README.md`/`AGENTS.md` "Power")
- [ ] MAX98357A `GND` connected to the common ground shared by
      ESP32-S3, LEDs, and DRV8833
- [ ] Confirmed ESP32-S3 GPIO signals (BCLK/LRC/DIN) are 3.3V logic,
      not being driven at or expected to tolerate 5V
- [ ] Speaker connected only to the amplifier's `+` and `-` output
      terminals
- [ ] Neither speaker terminal connected to ground
- [ ] No continuity short between the amplifier's two output terminals
- [ ] No continuity short from either speaker output terminal to ground
- [ ] Understood: MAX98357A output is bridge-tied (BTL) — do not probe
      it as a ground-referenced signal without equipment rated for that
      (a standard oscilloscope probe referenced to ground can short one
      output leg to ground through the probe itself)
- [ ] Supply polarity verified before power-up
- [ ] Speaker impedance and wattage verified against the 8Ω/0.5W
      assumption on record
- [ ] `SD`/enable pin state verified (GND = disabled at power-up, per
      the existing startup safety sequence)
- [ ] `GAIN` pin state verified against the physical board (currently
      undocumented in this repo — see "Current unknowns" above)
- [ ] No breadboard rail split or disconnected power segment between
      the supply and any of the three 5V loads
- [ ] 5V rail confirmed to actually reach the amplifier's `VIN` pin
      under load (not just at the supply terminals)

### Power verification

- [ ] Measure 5V at amplifier `VIN` with the system idle (no motor, no
      LED activity, amplifier silent)
- [ ] Measure 5V at amplifier `VIN` during LED activity
- [ ] Measure 5V at amplifier `VIN` during motor activity
- [ ] (Later, only after S0/S1 pass) measure 5V at amplifier `VIN`
      during speaker playback
- [ ] Record the minimum voltage observed across all of the above
- [ ] Inspect for ESP32 resets, LED flicker, amplifier clicking, or
      motor-induced noise during each of the above
- [ ] Confirm the 5V supply's continuous-current rating against the
      combined LED+amplifier+motor peak load (see "Expected power
      requirements" above)
- [ ] Confirm wire gauge and connector suitability for that current

### Software verification

- [ ] Clean build (`pio run -t clean && pio run`) — done 2026-08-01,
      see the task's build-validation report
- [ ] Speaker disabled/silent at boot confirmed (no automatic tone —
      `ENABLE_SPEAKER_AUTO_DEMO_TONE` is `0`)
- [ ] Digital silence only, continuously, with no audible click loop
- [ ] Microphone still initializes (`[MIC` boot lines present, RMS/level
      values look sane)
- [ ] LEDs still operate normally (base effect, overlay, brightness,
      mute)
- [ ] Buttons still operate normally (Mode/Mute/Brightness/Button4,
      Audio Mode long-hold)
- [ ] Motor remains off at boot (no movement, no PWM commanded)
- [ ] No watchdog reset observed during a full boot + idle period
- [ ] No I2S errors in the serial log (`i2s_write`/`i2s_read` error
      counters stay at 0 — see `status`'s `[SPEAKER]`/`[I2S]` lines)

## Later-stage validation checklist (not this task — after S0 passes)

- [ ] `speaker tone` physically confirmed clean (clarity, pitch, no
      clicks/pops, safe/comfortable volume) — not just the log line
- [ ] `speaker stop` / silence resumes correctly and immediately
- [ ] `k` emergency stop still returns the speaker to silence
      immediately (unaffected by any speaker change in this task)
- [ ] Interaction with LEDs checked (no visible flicker/disturbance
      during playback)
- [ ] Interaction with `MusicMotorController`/Audio Mode checked
      (playback + Audio Mode active simultaneously — Stage S4, not this
      task; speaker code does not currently reference either)
- [ ] Interaction with microphone checked (capture still works
      correctly during/after playback; no corruption from shared-port
      contention)
- [ ] Interaction with the power system checked under combined load (no
      brownout, reset, or brightness/motor disturbance)

Do not mark any box complete without direct hardware observation, per
this project's physical-validation policy (`/AGENTS.md` section on
testing philosophy). This document does not claim any of the above has
happened — see `CURRENT_STATUS.md` for what has actually been
physically validated to date.
