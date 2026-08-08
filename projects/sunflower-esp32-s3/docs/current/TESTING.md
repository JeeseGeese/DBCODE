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

No PlatformIO `test` env exists — 18 standalone, deterministic g++
programs, one per feature/regression area, each independent (constants/
enums/pure functions mirrored inline, no Arduino dependency):

```
audio_mode_button4_integration.cpp     unified Audio Mode / Button4 short+long press
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
speaker_bench.cpp                      speaker t/1/2/3/v/+/- bench volume ladder
speaker_bringup.cpp                    Stage S1/S2 speaker bring-up (silence, tone)
speaker_fmt_diag.cpp                   32-bit-slot packing A/B diagnostic (fmt1/2/3)
speaker_multitone.cpp                  sweep/melody/chord/noise bring-up tests
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

**Coverage gap**: no host test currently covers `LedEffects.cpp` or
`AudioOverlays.cpp` logic (all 18 files cover `MusicMotorController` or
the speaker suite). See `docs/development/ADDING_LED_EFFECTS.md` /
`ADDING_AUDIO_OVERLAYS.md` for the recommendation going forward.

## Physical test procedure

See `README.md`'s "Physical test procedure" section (10 steps covering
boot report, effect cycling, overlay behavior, mute, brightness, and
power-limit throttling) — run after any change touching
`Controls.cpp`, `LedEffects.cpp`, or `AudioOverlays.cpp`.

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
