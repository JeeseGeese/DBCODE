---
name: i2s-32bit-container-24bit-mic-handling
description: A 32-bit I2S slot container does not mean 32 meaningful bits — extract by shifting, and verify with a real trace
metadata:
  type: lesson
---

# 32-bit I2S slot / 24-bit microphone sample handling

## Problem

Both a 24-bit microphone (INMP441) and a 16/24/32-bit-configurable
amplifier (MAX98357A) can share one I2S bus configured at a single
"bits-per-slot" container width — but that container width is not the
same thing as how many bits of it are actually meaningful data for
either device.

## Root cause / discovery

The INMP441 left-justifies its real 24-bit sample within whatever slot
width the bus uses; the low bits are padding. Extracting it requires
right-shifting by `(slotWidth - 24)` bits — for Sunny's 32-bit slots,
that's `sample >> 8`. Getting the shift amount wrong doesn't just
introduce noise, it produces a systematically wrong-magnitude signal
that can still look "plausible" at a glance (constrast with total
silence, which is more obviously broken).

## How it was verified

A boot-time diagnostic trace (`AudioAnalyzer.cpp`, bounded to
`MIC_RX_TRACE_CALL_COUNT` calls) prints raw hex RX frames alongside the
extracted, shifted values, so the slot-index and shift-amount choice
was confirmed against real captured hardware data, not just assumed
from the datasheet.

## Correct approach

Never assume a bit-depth/justification choice for an I2S peripheral —
verify with a bounded, removable boot-time trace that prints both the
raw word and the extracted value. Document the exact shift amount and
which slot (left/right of the stereo pair) carries data, with the
trace evidence cited, not just "per the datasheet."

## Common failure modes

- Treating "32-bit slot" and "32-bit meaningful sample" as the same
  thing.
- Copy-pasting a shift amount from a different device's bring-up
  without re-verifying for the new device's actual bit depth.
- Never actually looking at a raw captured frame — trusting that
  "sound comes out"/"numbers change" is sufficient proof the exact bit
  alignment is correct (it can be close enough to seem to work while
  still being wrong in ways that matter for a later feature, e.g. an
  amplitude-sensitive threshold).

## Applies to future projects?

Yes — any I2S device where the datasheet describes both a slot width
and a real sample resolution smaller than that slot.

## Related Sunny files

`src/AudioAnalyzer.cpp` (RX trace + extraction), `src/SharedI2S.cpp`,
`docs/current/I2S_ARCHITECTURE.md`, `docs/playbooks/I2S_DEBUGGING.md`.
