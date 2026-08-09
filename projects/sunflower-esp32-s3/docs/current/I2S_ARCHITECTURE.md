# Sunny — Shared I2S Architecture (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

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

## Clock source: APLL is not available on this chip/framework (verified)

`initSharedI2S()` sets `.use_apll = false`. Investigated during the V1.1
speaker buzz-diagnosis sprint whether `use_apll=true` was a plausible,
untested contributor to clock-quality artifacts at this bus's fixed
16kHz/32-bit/stereo/1.024MHz BCLK configuration. Verified directly
against this project's installed framework HAL source (not guessed):
`hal/esp32s3/include/hal/i2s_ll.h`'s `i2s_ll_tx_clk_set_src()`/
`i2s_ll_rx_clk_set_src()` both hardcode the clock-select field to `2`
(D2CLK) and ignore their `src` argument entirely, with the comment
"ESP32-S3 only support I2S_CLK_D2CLK"; `soc_caps.h` for esp32s3 also
does not define `SOC_I2S_SUPPORTS_APLL` (present on the original ESP32,
absent here). APLL is simply not wired to this SoC's I2S peripheral in
this framework version — `use_apll` is part of the driver's cross-chip
struct but has no effect here. No APLL A/B toggle was implemented as a
result (it would be a no-op, not a real test) — see
`docs/current/SPEAKER.md`'s "APLL clock-quality investigation" section
and `SharedI2S.cpp`'s `.use_apll = false` comment for the full citation.

## For a future Raspberry Pi audio handoff

If a future Raspberry Pi integration needs to inject or receive PCM
audio, it must go through **one explicit owner** on top of this layer,
per the existing planning note (archived, see
`archive/superseded_docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` section 17)
— do not add a second I2S consumer independent of `SharedI2S.cpp`.
