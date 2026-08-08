# Sunny — Speaker (MAX98357A) Current Status

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

**Speaker debugging is still active/ongoing.** This file is the
CURRENT status only. For the detailed historical bring-up narrative
(Stage S0-S3, buzz-diagnosis investigation, format-packing audit), see
`archive/speaker_bringup/` — the useful conclusions from that history
are extracted below; the raw narrative logs are preserved, not deleted.

## Architecture

Shares the microphone's full-duplex I2S bus — see `I2S_ARCHITECTURE.md`.
No dedicated I2S controller of its own; `SpeakerTest.cpp` is the sole
owner of `i2s_write()`/`i2s_zero_dma_buffer()` calls on that shared bus.

## Current wiring and configuration

See `ELECTRICAL.md` for the full pin table. Summary:

- VIN: shared 5V rail. GND: common ground.
- BCLK=GPIO6, LRC=GPIO7 (shared with the mic), DIN=GPIO16.
- **SD (enable)**: currently tied to 3.3V — amplifier enabled. Manually
  moved, not GPIO-driven; the firmware's startup sequence requires
  confirming `[SPEAKER] Digital silence active` on serial before this
  is moved from GND.
- **GAIN**: currently tied to GND — a specific hardware-gain
  configuration being evaluated. Physically observed to improve
  loudness somewhat versus the prior (floating/default) configuration.
- **Speaker**: 40mm / 4Ω / 3W full-range. Physically observed to
  perform **substantially better** than the original small toy speaker
  used in earlier bring-up — this alone accounts for some of the
  perceived improvement, independent of any firmware/gain change.
- Decoupling: 1000 µF + 1 µF, both shunt across 5V/GND (see
  `ELECTRICAL.md`).

## Physically observed usable range

- Roughly **35% through 100% digital amplitude** is currently useful
  output.
- Higher digital levels tend to make the residual buzz **less
  noticeable** (relatively, not necessarily lower in absolute terms —
  not measured).
- **50-100% produced clear, recognizable tones.**
- A residual buzz/static is present and **not yet resolved**.

## What has been ruled out (with evidence)

- **Mono-into-stereo, wrong slot order, sign-handling, integer
  overflow, phase discontinuity across DMA buffer boundaries, or
  double amplitude scaling** — a full source-level review of
  `SharedI2S.cpp`/`SpeakerTest.cpp`'s sample formatting found none of
  these as active bugs (see `archive/speaker_bringup/` for the full
  review). One real (but almost certainly inert on this toolchain)
  defect was found and fixed: a left-shift of a possibly-negative
  signed value (undefined behavior pre-C++20) was rewritten as a
  well-defined unsigned-shift-then-reinterpret — bit-identical output
  on this compiler, not a behavior change.
- **A two-controller I2S design** — conclusively failed differently
  (see `I2S_ARCHITECTURE.md`); not the cause of the current buzz, which
  persists under the working full-duplex architecture.

## What is NOT yet conclusively ruled out

- **Which of the three 32-bit-slot packings** (16-bit MSB-justified /
  24-bit left-justified / full 32-bit direct — see `speaker fmt1`/
  `fmt2`/`fmt3` below) the MAX98357A's internal word-length
  auto-detection actually prefers. A diagnostic exists to A/B this
  physically; results have not been reported back as conclusive.
- **`use_apll=false` BCLK clock-generation precision** at
  16kHz×32×2=1.024MHz (not evenly divisible from the source clock) —
  a commonly-cited secondary cause of audible buzz on this exact
  chip/MCU combination in general; not tested on this board.
- **Electrical/power-rail noise** on the shared 5V rail (motor +
  amplifier + LEDs) — plausible given the known shared-rail contention
  (see `POWER.md`), not isolated as the specific buzz source.
- **Do not claim a proven root cause for the residual buzz anywhere in
  this project.** It remains genuinely open.

## Speaker diagnostic commands (`SpeakerTest.cpp`, via `Controls.cpp`)

Full reference: `README.md`'s "Speaker hardware test" section and
`SpeakerTest.h`'s own comments. Summary of the current surface:

- `speaker tone` / `speaker stop` — the original Stage S2 bring-up
  tone + stop.
- `speaker t`/`1`/`2`/`3` — bench tones at an adjustable volume ladder
  (`speaker v`/`+`/`-`, 2/5/8/12/18/25%).
- `speaker fmt1`/`fmt2`/`fmt3`/`fmtstatus` — the 32-bit-slot packing
  A/B diagnostic described above.
- `speaker sweep`/`melody`/`chord`/`noise` — multi-tone bring-up tests
  at the current bench volume (noise independently capped at 10%).
- `speaker voltest`/`volquick`/`volstop`/`volstatus` — the automatic
  2%→100% (or shorter 5-level) volume-ladder diagnostic, playing a
  multi-tone sequence at each level, for characterizing usable loudness/
  distortion onset/buzz growth/stability across the full range. This is
  the tool that produced the "physically observed usable range" section
  above.
- `t`/`s`/`low`/`mid`/`high`/`sweep`/`melody`/`beep`/`noise`/`loud` —
  the original flat diagnostic suite (pre-dates the `speaker`-namespaced
  bench).
- `music1`-`music4`/`stopmusic` — procedural melody player (original
  compositions, not copyrighted encodings).

All reuse the same non-blocking scheduler, the same 20ms fade
convention, and the same continuous-digital-silence-between-tests
guarantee — see `docs/development/` if adding a new one is ever needed
(not currently documented as its own SOP; follow the existing pattern
in `SpeakerTest.cpp`).

## Known brownout history

The ESP32 ROM bootloader unconditionally prints its reset reason on
every boot (e.g. `rst:0xf (BROWNOUT_RST)`), visible in this project's
own captured boot logs — this is the existing, always-on brownout
observability. ESP32-S3's hardware brownout detector remains enabled
and has not been disabled anywhere in this firmware. No confirmed
brownout event has been tied specifically to speaker operation as of
this baseline (see `POWER.md` for the general shared-rail risk this
sits inside).

## Likely future power/noise cleanup topics

- Isolate whether the residual buzz scales with motor/LED activity
  (shared-rail noise) versus being present even with motor/LEDs fully
  idle (points toward digital-format or clock-precision causes
  instead).
- Physically test `speaker fmt1`/`fmt2`/`fmt3` and report which (if
  any) is audibly cleaner.
- Try `use_apll=true` as an isolated A/B test (would require a
  `SharedI2S.cpp` change — currently unexercised).
- Measure `VIN` voltage at the amplifier during idle vs. motor-active
  vs. LED-active vs. combined load (see `docs/SPEAKER_BRINGUP_PLAN.md`'s
  archived preflight checklist for the full list).
