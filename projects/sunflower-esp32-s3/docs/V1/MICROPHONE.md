# Sunny V1 — Microphone (INMP441)

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/MICROPHONE.md`.

## Wiring (see `ELECTRICAL.md`/`GPIO_MAP.md` for the table)

BCLK=GPIO6, WS=GPIO7, SD(DATA)=GPIO15, VDD=3.3V, GND=common, L/R tied
to GND (selects LEFT channel).

## I2S format

Runs on the shared full-duplex `I2S_NUM_0` bus (see
`I2S_ARCHITECTURE.md`) — 16kHz, 32-bit-per-slot, stereo
(`I2S_CHANNEL_FMT_RIGHT_LEFT`). The INMP441 left-justifies its 24-bit
sample within the 32-bit slot; `AudioAnalyzer.cpp` extracts only the
mic's active slot (`Config.h`'s `MIC_I2S_SLOT_INDEX`, currently `0`)
from each stereo RX frame in software, right-shifting 8 bits to recover
the 24-bit value. This slot choice was **empirically confirmed** against
a captured hardware RX trace (a boot-time diagnostic trace, bounded to
`MIC_RX_TRACE_CALL_COUNT` calls), not assumed from the L/R wiring alone.

## Feature extraction (`AudioAnalyzer.cpp` → `AudioFeatures`)

Recomputed once per analysis window (~`MIC_PRINT_INTERVAL_MS`):

| Field | Meaning |
|---|---|
| `rms` | raw DC-corrected RMS, this window |
| `normalized` | 0..1 against the adaptive noise floor / `AUDIO_MAX_RMS` ceiling |
| `envelope` | attack/release-smoothed `normalized` |
| `transientStrength` | normalized units/second rise rate of `envelope` |
| `lowFrequencyEnergy` | 0..1 low-frequency **proxy** (single-pole low-pass, not true FFT bass — see `AUDIO_ANALYSIS.md`) |
| `clap` | edge-triggered, cooldown-gated sharp loud event |
| `transient` | edge-triggered, cooldown-gated fast envelope rise |
| `peak` | raw DC-corrected peak magnitude (hardware-test support field) |

An adaptive noise floor drifts slowly toward ambient quiet, bounded to
`AUDIO_NOISE_FLOOR_MIN`/`_MAX`, so a sustained loud passage can't become
"the new silence."

## Fault detection (built in, always running)

Zero-byte-read streaks, samples stuck at zero, samples appearing
constant, and samples appearing saturated (clipping) are each tracked
and produce a rate-limited `[MIC] WARN:` line (gated by
`setAudioLogEnabled()`, off by default — see
`docs/development/LED_AUDIO_QUICK_REFERENCE.md`).

## Verified behavior

- Zero I2S read errors observed across repeated multi-phase capture
  tests (quiet room, speech, claps, handling/tapping) both before and
  after the full-duplex speaker-sharing migration.
- RMS/level values respond visibly and proportionally to real input
  across all tested phases.
- Continues to initialize and capture correctly with the MAX98357A
  sharing the same I2S bus (see `I2S_ARCHITECTURE.md` for why this is
  safe).

## Known limitation

`lowFrequencyEnergy` is a single-pole low-pass proxy, not real
frequency-band extraction — there is no FFT or filter bank anywhere in
this project. Its noise-floor/max-RMS constants are not
hardware-calibrated the way the main RMS ones are. Treat it as a rough
low-vs-high energy skew indicator, not a spectrum measurement.
