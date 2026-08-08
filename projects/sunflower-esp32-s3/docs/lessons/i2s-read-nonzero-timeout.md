---
name: i2s-read-nonzero-timeout
description: A zero-tick I2S read poll can race the DMA buffer-ready signal — use a short, bounded, non-zero wait
metadata:
  type: lesson
---

# Nonzero I2S read timeout

## Problem

Reading I2S RX data with a zero-tick (`portMAX_DELAY`-free, but also
wait-free) poll can occasionally race the DMA's own buffer-ready
signal, producing spurious zero-byte reads.

## Root cause / discovery

`AudioAnalyzer.cpp`'s `captureSamples()` deliberately uses a short but
**non-zero** timeout (`pdMS_TO_TICKS(20)`) rather than a pure 0-tick
poll, with a comment noting a 0-tick poll can race DMA readiness.

## How it was verified

Documented as a deliberate design choice in the source comment; the
project's own zero-byte-streak fault detector (tracked continuously in
`AudioAnalyzer.cpp`) has not flagged this as a live problem in normal
operation.

## Correct approach

Use a short, bounded wait (tens of milliseconds), never 0 and never
`portMAX_DELAY`/unbounded. See the related, more severe write-path
timeout discussion in `docs/lessons/audio-buzz-noise-diagnosis.md` and
`docs/current/I2S_ARCHITECTURE.md`'s "Write-path discipline" section — an
unbounded wait on the TX side once froze the entire application, not
just the affected call, on this exact project.

## Common failure modes

- 0-tick polls that occasionally miss data right as it becomes ready,
  producing intermittent zero-byte reads that are easy to misdiagnose
  as a wiring or hardware problem.
- Overcorrecting to an unbounded wait "to be safe" — this is worse,
  not safer, on this driver (see the I2S write-path lesson).

## Applies to future projects?

Yes — general ESP-IDF I2S driver guidance, not Sunny-specific.

## Related Sunny files

`src/AudioAnalyzer.cpp` (`captureSamples()`), `src/SpeakerTest.cpp`
(the TX-side analog, `SPEAKER_WRITE_TICKS_TO_WAIT`),
`docs/playbooks/I2S_DEBUGGING.md`.
