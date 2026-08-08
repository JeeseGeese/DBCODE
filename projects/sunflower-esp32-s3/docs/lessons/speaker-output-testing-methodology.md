---
name: speaker-output-testing-methodology
description: Test a speaker output with a progressive amplitude ladder and multiple frequencies, not one fixed tone
metadata:
  type: lesson
---

# Speaker output testing methodology

## Problem

A single fixed-amplitude test tone doesn't reveal where distortion
begins, whether noise/buzz is amplitude-dependent, or whether the
system is stable across the full usable range.

## Root cause / discovery

Early speaker bring-up used one low, fixed digital amplitude
(originally 5%) as "the" test tone. This proved the write path worked
but said nothing about usable loudness or where problems start. An
automatic volume-ladder diagnostic (`speaker voltest`/`volquick`) was
added later specifically to answer those questions — stepping through
2%→100% (or a shorter set), playing a multi-frequency sequence at each
level, with safety checks (never jump amplitude mid-tone; abort on any
I2S write fault).

## How it was verified

Running the ladder physically produced the concrete findings recorded
in `docs/current/SPEAKER.md` (35-100% useful, 50-100% clear tones, buzz
less noticeable at higher relative levels) — information a single
fixed-tone test could never have produced.

## Correct approach

For any new audio-output hardware bring-up: test across a progressive
amplitude range (not one fixed level), with multiple frequencies per
level (a single tone can hide frequency-specific resonance/distortion),
always fading in/out to avoid clicks, and with a hard abort on any
write-path fault. See `docs/playbooks/SPEAKER_AUDIO_VALIDATION.md`.

## Common failure modes

- Testing only at one comfortable-sounding amplitude and calling the
  amplifier "working" — misses where distortion actually begins.
- Testing only one frequency — hides resonance-specific buzz that only
  appears at certain pitches.
- No safety abort on write failure during an automated multi-minute
  test — a fault partway through can leave the amplifier driven with
  garbage for the rest of the run if not checked every tick.

## Applies to future projects?

Yes — general audio bring-up methodology, not Sunny-specific.

## Related Sunny files

`src/SpeakerTest.cpp` (`speaker voltest`/`volquick`),
`test_host/speaker_voltest.cpp`, `docs/current/SPEAKER.md`,
`docs/playbooks/SPEAKER_AUDIO_VALIDATION.md`.
