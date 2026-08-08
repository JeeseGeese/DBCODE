# Architecture — Audio Pipeline

How microphone capture and speaker output are structured and why. For
current wiring/status, see `docs/current/MICROPHONE.md`/`SPEAKER.md`/
`I2S_ARCHITECTURE.md`.

## The shape

```
INMP441 mic --RX-->  \
                       SharedI2S (ONE full-duplex I2S_NUM_0 controller)
MAX98357A amp <--TX-- /
       ^                        |
       |                        v
  speaker              AudioAnalyzer (RX only)  -->  AudioFeatures
  test tones                                          |
  (SpeakerTest,                                        v
   TX only)                                    AudioVisualState
                                                (richer per-frame signals)
                                                        |
                                    +-------------------+-------------------+
                                    v                                       v
                              AudioOverlays                       MusicMotorController /
                              (LED rendering)                     ExpressiveMotion (motor)
```

## Why one shared full-duplex controller, not two

An earlier two-controller design (separate RX master + TX slave
sharing clocks) failed conclusively on real hardware — see
`docs/lessons/shared-full-duplex-i2s.md` and `DESIGN_DECISIONS.md`'s
"Why Shared I2S?". One full-duplex master port has exactly one clock
domain for both directions, removing the cross-controller
DMA-availability problem structurally rather than working around it.

## Why one owner, many readers/writers

`SharedI2S.cpp` is the sole owner of driver install/config/uninstall.
`AudioAnalyzer.cpp` only reads; `SpeakerTest.cpp` only writes. Neither
reconfigures the bus. This is the audio-specific instance of the
single-owner-resource pattern described in `SOFTWARE_ARCHITECTURE.md`
— applied here because the alternative (either side able to
reconfigure) reintroduces exactly the kind of cross-module race that
already broke the two-controller design once.

## Why the mic and speaker use different bit depths within the same slot

The shared bus's slot width (32 bits) is a hard full-duplex container
constraint — it doesn't mean either direction must use all 32 bits
meaningfully. The mic uses its native 24-bit resolution; the speaker's
test-tone engine currently defaults to 16-bit resolution (with 24-bit
and 32-bit direct-packing options available as a diagnostic). This is
intentional, not an oversight — see
`docs/lessons/i2s-32bit-container-24bit-mic-handling.md`.

## Feature extraction is layered, not duplicated

`AudioFeatures` (raw signal-processing output) →
`AudioVisualState` (richer, LED/overlay-oriented derived signals) is
one linear pipeline, computed once per frame, read by every consumer
(`AudioOverlays`, `MusicMotorController`, `ExpressiveMotion`,
`BehaviorEngine`). No consumer re-derives its own signal from raw
samples — there is exactly one microphone-analysis pipeline in the
project. See `docs/current/AUDIO_ANALYSIS.md`.

## Write-path safety discipline

Bounded, non-zero waits only, never `portMAX_DELAY`, on both RX and
TX. Sample generation as a pure function of an absolute index, so
partial writes are handled exactly. See `docs/playbooks/I2S_DEBUGGING.md`.

## See also

`docs/current/I2S_ARCHITECTURE.md`, `docs/current/SPEAKER.md`,
`docs/lessons/shared-full-duplex-i2s.md`,
`docs/playbooks/I2S_DEBUGGING.md`.
