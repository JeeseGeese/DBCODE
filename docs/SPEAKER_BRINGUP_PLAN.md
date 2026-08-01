# MAX98357A Speaker Bring-Up Plan

Planning document only — no implementation has started on the stages
below. Written 2026-07-31, the evening the Unified Audio Mode milestone
(`sunny-audio-mode-v1-physical-validation`) was finalized, to prepare the
next day's isolated speaker-subsystem work without touching Audio Mode,
DanceEngine, or MusicMotorController.

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
  melody player). Does **not** route live microphone audio to the
  speaker.
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

- **Stage S0 — Preflight.** Confirm amplifier chip identity if
  physically accessible; confirm speaker spec (8Ω/0.5W) matches what's
  connected; confirm `GAIN` pin state; re-read the README "Speaker
  hardware test" startup safety sequence in full; confirm current
  `SpeakerTest` build still boots clean and write-success stays 100%
  before changing anything. **Also required** (see the shared 5V rail
  risk under Risks above for why): confirm the exact 5V supply type,
  its rated continuous and peak current, the distribution wire gauge,
  connector/breadboard current-path limitations, where the 5V rail
  branches to the LEDs/amplifier/DRV8833, the common-ground layout, bulk
  capacitance on the 5V rail, local decoupling near the amplifier and
  motor driver, motor-noise suppression, and the voltage measured at the
  amplifier during simultaneous motor and LED activity.
- **Stage S1 — Silent I2S initialization.** Already implemented
  (`initSpeakerTest()`/`SharedI2S`) — this stage is a *re-verification*
  pass: confirm digital silence is genuinely silent (no audible hiss/hum)
  once `SD` is moved to 3.3V, on the actual connected speaker.
- **Stage S2 — Low-volume generated tone.** Already implemented (the
  automatic 440Hz demonstration tone, plus `t`/`s`/`low`/`mid`/`high`/
  `sweep` commands) — this stage is the **first real physical-quality
  validation**: listen for clarity, correct pitch, absence of clicks/pops
  at tone start/stop, and acceptable (not damaging) volume before going
  any further.
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

## Validation checklist for tomorrow

- [ ] Wiring verification (VIN/GND/SD/GAIN/BCLK/LRC/DIN all match the
      table above; speaker spec confirmed 8Ω/0.5W)
- [ ] 5V supply verification: exact supply type, rated continuous
      current, rated peak current, wire gauge, connector/breadboard
      current-path limitations, where the 5V rail branches to
      LEDs/amplifier/DRV8833, common-ground layout, bulk capacitance on
      the 5V rail, local decoupling near the amplifier and motor driver,
      motor-noise suppression, and voltage measured at the amplifier
      during motor and LED activity (see Risks above)
- [ ] Successful clean build (`pio run -t clean && pio run`)
- [ ] Successful upload to the identified Sunny board
- [ ] Amplifier initialization confirmed (`[SPEAKER] I2S TX
      initialization: SUCCESS`, `[SPEAKER] Digital silence active`)
- [ ] Tone playback physically confirmed clean (clarity, pitch, no
      clicks/pops, safe volume) — not just write-success in logs
- [ ] Clean shutdown confirmed (silence resumes correctly after a test
      tone/clip ends; `k` emergency stop still returns to silence
      immediately)
- [ ] Interaction with LEDs checked (no visible flicker/disturbance
      during playback)
- [ ] Interaction with `MusicMotorController` checked (playback +
      Audio Mode active simultaneously — Stage S4)
- [ ] Interaction with microphone checked (capture still works
      correctly during/after playback; no corruption from shared-port
      contention)
- [ ] Interaction with the power system checked (no brownout, reset, or
      brightness/motor disturbance when amplifier, motor, and LEDs are
      all active together)

Do not mark any box complete without direct hardware observation, per
this project's physical-validation policy (`/AGENTS.md` section on
testing philosophy).
