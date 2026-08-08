# Sunny V1 — Shared I2S Architecture

**FROZEN at the Sunny V1 baseline (2026-08-07). Do not edit for post-V1 changes.** For the living, always-current version of this document, see `docs/current/I2S_ARCHITECTURE.md`.

## The single owner

`include/SharedI2S.h` / `src/SharedI2S.cpp` is the **sole owner** of
`I2S_NUM_0` — the only file in the firmware that calls
`i2s_driver_install()`, `i2s_set_pin()`, or `i2s_driver_uninstall()`.
It configures exactly one **full-duplex master port**:

```
mode:               I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX
sample rate:        16000 Hz
bits per slot:      32
channel format:     I2S_CHANNEL_FMT_RIGHT_LEFT (true stereo)
comm format:        I2S_COMM_FORMAT_STAND_I2S
BCLK:                GPIO6
WS:                  GPIO7
RX data (mic in):    GPIO15
TX data (speaker out): GPIO16
```

- `AudioAnalyzer.cpp` only calls `i2s_read()` on this port.
- `SpeakerTest.cpp` only calls `i2s_write()` / `i2s_zero_dma_buffer()`
  on it.
- Neither reconfigures, reinstalls, or uninstalls the driver.

## Why full-duplex, not two controllers

An earlier two-controller design (I2S_NUM_0 master RX + I2S_NUM_1
slave TX, sharing BCLK/WS) **failed conclusively on real hardware**:
`i2s_write()` on the slave TX port always returned `ESP_OK` with
`bytesWritten=0` at every bounded wait tried, and an unbounded
`portMAX_DELAY` wait froze the entire application (not just the write)
until a hardware reset. A single full-duplex master port has exactly
one clock domain generated once for both directions, removing the
cross-controller DMA-availability question entirely.

**Do not resurrect the two-controller approach without new evidence it
would work differently.** See `docs/lessons/shared-full-duplex-i2s.md`
for the generalized lesson.

## Why the mic and speaker use different "meaningful bit depths"

Both directions share the 32-bit slot **container width** — a hard
full-duplex constraint on this driver (RX and TX can't run different
bit depths on one legacy-driver port). Within that shared container,
each direction independently chooses how many bits are meaningful:

- **RX (mic)**: INMP441's native 24-bit sample, left-justified —
  `AudioAnalyzer.cpp` right-shifts 8 bits to recover it.
- **TX (speaker)**: `SpeakerTest.cpp` generates 16-bit-resolution
  samples, MSB-justified into the upper 16 bits of each 32-bit slot
  (lower 16 bits = 0) for most tests, with an additional 24-bit and
  32-bit direct-packing diagnostic (`speaker fmt1`/`fmt2`/`fmt3`) added
  during buzz-diagnosis work — see `SPEAKER.md`.

This is a deliberate design, not a bug — see `docs/lessons/` for the
I2S packing lessons extracted from the buzz-diagnosis work.

## Write-path discipline

`i2s_write()` in `SpeakerTest.cpp` uses a small **bounded** wait
(`SPEAKER_WRITE_TICKS_TO_WAIT`, ~20ms) — never 0, never
`portMAX_DELAY`. An isolated `portMAX_DELAY` diagnostic was tried once
(on the old slave-TX architecture) and froze the whole application;
that code path has been permanently removed, not just disabled. Sample
generation is a stateless function of an absolute sample index, so a
partial write is handled exactly: the sample cursor only advances by
frames the DMA actually accepted.

## Concurrency status

Mic capture and speaker output have both been exercised on the same
bus and both continue to work — see `SPEAKER.md`/`MICROPHONE.md` for
the current physical evidence. **Not yet physically validated**:
speaker playback + full-duty `MusicMotorController` + full LED
rendering, all simultaneously, under sustained load (see
`KNOWN_LIMITATIONS.md`).

## For a future Raspberry Pi audio handoff

If a future Raspberry Pi integration needs to inject or receive PCM
audio, it must go through **one explicit owner** on top of this layer,
per the existing planning note (archived, see
`archive/superseded_docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 17)
— do not add a second I2S consumer independent of `SharedI2S.cpp`.
