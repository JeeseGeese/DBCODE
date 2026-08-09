# Sunny — Testing (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

Three distinct levels of "it works" — never used interchangeably (see
`/AGENTS.md` section 5 for the repository-wide policy this follows):

1. **Host-validated** — a deterministic host-compiled test passed.
   Proves pure decision logic in isolation. Proves nothing about
   hardware.
2. **Software-validated** — builds, uploads, runs without crashing,
   serial logs show expected state transitions. Proves the code runs
   on the target MCU. Does **not** prove the physical behavior is
   correct/safe.
3. **Physically validated** — a human directly observed the real
   hardware behave correctly, and that observation is recorded.

## Host tests (`test_host/`)

No PlatformIO `test` env exists — 20 standalone, deterministic g++
programs, one per feature/regression area, each independent (constants/
enums/pure functions mirrored inline, no Arduino dependency). 18 of
these existed at the `sunny-v1-baseline` tag; `led_count_and_power.cpp`
and `speaker_v11_diagnostics.cpp` were added during V1.1:

```
audio_mode_button4_integration.cpp     unified Audio Mode / Button4 short+long press
led_count_and_power.cpp                V1.1: 36-LED count, power estimator, HWTEST safety
music_motor_choreography_dynamics.cpp  Revision 10 motion tier/duty-cycle/drop-phrase
music_motor_choreography_invariants.cpp drop-hold/hip-shake/spin-profile sequencing
music_motor_debug_diagnostics.cpp      Revision 4 decision-reason helpers
music_motor_diagnostic_handoff.cpp     stop-reason classification fix
music_motor_intensity_invariants.cpp   per-band interpolation/clamp/hysteresis
music_motor_pipeline_profiles.cpp      full per-tick energy/band/drop pipeline
music_motor_relative_drop_detection.cpp Revision 9 drop confidence/phase machine
music_motor_renewable_phrase.cpp       Revision 8 SUSTAINED_DRIVE phrase system
music_motor_rotation_commitment.cpp    Revision 6 "favor continuation" gate
music_motor_silence_rampdown.cpp       Revision 8 MUSICAL_RAMP_DOWN
music_motor_sustained_drive.cpp        Revision 7 SUSTAINED_DRIVE state machine
music_motor_sustained_drive_deadlock.cpp Revision 10.1 deadlock regression fix
speaker_bench.cpp                      speaker t/1/2/3/volume ladder (V1.1: 35-100%/70%)
speaker_bringup.cpp                    Stage S1/S2 speaker bring-up (silence, tone)
speaker_fmt_diag.cpp                   32-bit-slot packing A/B diagnostic (fmt1/2/3)
speaker_multitone.cpp                  sweep/melody/chord/noise bring-up tests
speaker_v11_diagnostics.cpp            V1.1: lowmidhigh/speechtest/musictest/silencecheck/isolate
speaker_voltest.cpp                    automatic volume-ladder diagnostic
```

Run the whole suite:

```bash
cd projects/sunflower-esp32-s3/test_host
for f in *.cpp; do
  name="${f%.cpp}"
  g++ -std=c++17 -Wall -Wextra -o "/tmp/${name}" "$f" && "/tmp/${name}"
done
```

All must print `PASS: 0 failure(s)` (or the file's own `All ... tests
passed.` line) with **zero compiler warnings** before a change in the
area they cover is considered done.

**Coverage gap**: no host test covers `LedEffects.cpp` or
`AudioOverlays.cpp` *rendering* logic directly (`led_count_and_power.cpp`
covers the power estimator and LED-count invariants, not effect
rendering itself). See `docs/development/ADDING_LED_EFFECTS.md` /
`ADDING_AUDIO_OVERLAYS.md` for the recommendation going forward.

## Physical test procedure

See `README.md`'s "Physical test procedure" section (10 steps covering
boot report, effect cycling, overlay behavior, mute, brightness, and
power-limit throttling) — run after any change touching
`Controls.cpp`, `LedEffects.cpp`, or `AudioOverlays.cpp`.

## V1.1 closure validation (current)

The checklist below is the physical validation gate for closing Sunny
V1.1 (see `docs/current/V1_1_STATUS.md`) — run entirely on **battery
power**, not computer USB (see `docs/current/POWER.md` for why that
distinction matters for this specific milestone). Do not deliberately
force repeated brownouts or run an aggressive stress test beyond what's
listed.

**A. Boot**
1. Power on from battery. Confirm one clean boot (no reset loop).
2. Confirm `[HWTEST] LED count configured: 36 (expected 36)`.
3. Confirm `[HWTEST] LED power limiter: LED_CURRENT_LIMIT_MA=1000` and,
   if SOLID WHITE triggers throttling, confirm the `[POWER] Throttling:`
   line appears (expected, not a fault).
4. Confirm no `[SYSTEM] WARNING: this boot followed a brownout reset`
   line.

**B. Buttons**
5. Mode button — cycles LED base effect.
6. Mute button — LEDs off/on.
7. Brightness button — steps brightness level.
8. Button4 short press — audio-overlay toggle.
9. Button4 long press — unified Audio Mode toggle; confirm the green
   "ON" cue and, on a second long-press, the red "OFF" cue.

**C. LEDs**
10. Cycle through several base effects (`Mode` button or serial).
11. Cycle through several audio overlays.
12. Change brightness across its range, including 100% requested —
    confirm the power limiter still engages/behaves sanely at max
    requested brightness (it should throttle a high-current frame the
    same way it does at boot).

**D. Microphone**
13. Speak/clap near the mic — confirm an audio-reactive overlay
    responds.

**E. Motor**
14. Enable Audio Mode / MusicMotor and play music — confirm the motor
    responds to audio (beats/intensity).
15. Confirm normal high-intensity movement (including M100-range
    moments) works on battery power without a brownout.
16. Do **not** deliberately force repeated reversals/brownouts beyond
    what normal music playback naturally produces.

**F. Speaker**
17. `speaker silencecheck` — confirm true digital silence (whatever
    residual noise is heard is the known, documented low-frequency
    buzz — not new).
18. `speaker carriercheck` — same, plus confirm the printed I2S
    clock/pin diagnostics look sane.
19. `speaker lowmidhigh`, `speaker speechtest`, `speaker musictest` —
    confirm each plays and returns to silence (no hang, no runaway
    loop).
20. Confirm `speaker volume` reports **70%** as the boot default.
21. Document whether the low-frequency buzz is still present (expected
    — do not treat its presence as a new regression).

**G. Combined system**
22. On battery power, with LEDs actively rendering, an audio overlay
    active, and MusicMotor active simultaneously, confirm the speaker/
    audio subsystem remains initialized and responsive (e.g. `speaker
    volume` still responds) throughout.

Report results back per step (pass/fail/observation) rather than a
single pass/fail for the whole checklist — a partial pass should be
recorded as exactly that, not rounded up (see `/AGENTS.md` section 6).

## Build

```bash
cd ~/DOBETTERCODE/DBCODE/projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run
```

## Upload

```bash
pio device list                          # confirm the port -- don't assume;
                                          # if drv8833-motor-test's board is
                                          # also plugged in, check SER=...
pio run -t upload --upload-port /dev/ttyACM0
```

If not in the `dialout` group:
`sg dialout -c "pio run -t upload --upload-port /dev/ttyACM0"`.

## Serial monitor

```bash
pio device monitor -p /dev/ttyACM0 -b 115200
```

115200 baud (set in `platformio.ini`'s `monitor_speed`, must match on
both ends). Boot takes ~30-40s before serial commands are processed —
`HardwareTest` and `MicRetest` both run unconditionally and block at
the end of `setup()`.
