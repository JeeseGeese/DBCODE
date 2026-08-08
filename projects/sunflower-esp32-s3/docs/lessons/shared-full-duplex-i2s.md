---
name: shared-full-duplex-i2s
description: A two-controller I2S design (separate RX master + TX slave) failed conclusively; one full-duplex master port is the working architecture
metadata:
  type: lesson
---

# Shared full-duplex I2S (one master port, not two controllers)

## Problem

Needed simultaneous I2S microphone capture (RX) and amplifier output
(TX) sharing BCLK/WS clock lines, on an ESP32 with a limited number of
I2S controllers.

## Root cause / discovery

The first attempted architecture used two controllers: `I2S_NUM_0` as
RX master, `I2S_NUM_1` as a TX **slave** sharing the same BCLK/WS.
`i2s_write()` on the slave TX port always returned `ESP_OK` with
`bytesWritten=0`, at every bounded wait tried (20ms, 100ms). An
unbounded `portMAX_DELAY` wait froze the **entire application**, not
just the write call, requiring a hardware reset to recover.

## How it was verified

Directly observed and measured (write-outcome counters showing 0 bytes
written repeatedly; a full application hang requiring physical reset
when `portMAX_DELAY` was tried). Not a hypothesis — reproduced
reliably on real hardware.

## Correct approach

Configure **one** I2S controller as a full-duplex master
(`I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX`), with one
`i2s_pin_config_t` specifying both RX and TX data pins. This has
exactly one clock domain generated once for both directions, removing
the cross-controller DMA-availability question that broke the
two-controller design. See `docs/current/I2S_ARCHITECTURE.md` for Sunny's
exact working configuration.

## Common failure modes

- Assuming a slave TX port will "just work" once clocked by an
  external/shared master — DMA buffer readiness on the slave side can
  still starve independently of the master's own timing.
- Reaching for an unbounded wait to "fix" a 0-byte-write symptom — this
  makes the failure catastrophic (whole-app freeze) instead of merely
  broken (0 bytes written, at least recoverable).

## Applies to future projects?

Yes, with a caveat: the specific "slave TX starves" failure mode is
likely somewhat driver/silicon-specific (this project used the ESP-IDF
*legacy* I2S driver). The general lesson — prefer one full-duplex
controller over two coordinated ones when the hardware supports it,
and never use an unbounded wait on an I2S write — is broadly
applicable.

## Related Sunny files

`include/SharedI2S.h`/`.cpp`, `docs/current/I2S_ARCHITECTURE.md`,
`docs/playbooks/I2S_DEBUGGING.md`.
