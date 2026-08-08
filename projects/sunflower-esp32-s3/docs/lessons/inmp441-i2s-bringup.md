---
name: inmp441-i2s-bringup
description: Bringing up an INMP441 I2S MEMS microphone, including L/R channel selection
metadata:
  type: lesson
---

# INMP441 I2S bring-up

## Problem

Get a working, correctly-scaled microphone signal from an INMP441 over
I2S on an ESP32.

## Root cause / discovery

The INMP441 outputs a 24-bit sample, left-justified within whatever
I2S word width the bus is configured for (Sunny uses 32-bit slots).
The `L/R` pin selects which stereo slot (LEFT or RIGHT) the mic
transmits on — tying it to GND selects LEFT. Assuming the correct slot
without checking is risky: this project later needed to *empirically
confirm* which slot carried real data via a captured hardware RX trace
(see `docs/lessons/i2s-32bit-container-24bit-mic-handling.md`), because
software assumption alone was not treated as sufficient evidence.

## How it was verified

Zero I2S read errors across repeated multi-phase capture tests (quiet
room, speech, claps, handling/tapping), both before and after the
later full-duplex speaker-sharing migration. RMS/level values respond
visibly and proportionally to real input in every tested phase.

## Correct approach

Wire `L/R` deliberately (don't leave it floating), and verify the
active slot with a real captured trace rather than assuming from the
wiring alone — see the boot-time RX trace pattern in
`AudioAnalyzer.cpp` (`MIC_RX_TRACE_CALL_COUNT`). Use a short but
non-zero I2S read timeout (see
`docs/lessons/i2s-read-nonzero-timeout.md`) so buttons/rendering stay
responsive.

## Common failure modes

- Assuming the mic's data always lands in "the first slot" without
  verifying — silently reading garbage/zero when it's actually the
  other one.
- Extracting the sample as if it were left-justified in a 24-bit word
  instead of a 32-bit slot (wrong shift amount → wildly wrong
  magnitude, not just wrong sign).
- Zero-tick I2S read polls racing the DMA's buffer-ready signal.

## Applies to future projects?

Yes — broadly reusable for any INMP441 (or similar I2S MEMS mic)
bring-up.

## Related Sunny files

`include/AudioAnalyzer.h`/`.cpp`, `include/SharedI2S.h`/`.cpp`,
`docs/current/MICROPHONE.md`, `docs/playbooks/INMP441_BRINGUP.md`.
