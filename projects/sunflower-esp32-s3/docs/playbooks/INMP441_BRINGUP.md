# Playbook: INMP441 (I2S MEMS Microphone) Bring-Up

## Wiring

BCLK, WS(LRCLK), SD(DATA) to three GPIOs (validated per
`GPIO_VALIDATION.md`). VDD to 3.3V (confirm this exact part's rated
voltage — do not assume 3.3V-tolerant parts are 5V-tolerant). `L/R`
tied deliberately to GND (LEFT) or VDD (RIGHT) — do not leave
floating. GND common with the rest of the system.

## Software

1. Configure I2S at the mic's native rate (commonly 16kHz for voice/
   analysis use) and appropriate bit depth container (the INMP441
   itself outputs 24-bit, left-justified within whatever slot width
   you configure — see `docs/lessons/i2s-32bit-container-24bit-mic-handling.md`).
2. Use a short, bounded (not zero, not unbounded) read timeout — see
   `docs/lessons/i2s-read-nonzero-timeout.md`.
3. Add a bounded, removable boot-time trace that prints both raw hex
   RX words and the extracted/shifted sample values, to empirically
   confirm your slot-index and shift-amount choice against real
   hardware data before trusting it.
4. Build basic fault detection: zero-byte-read streaks, samples stuck
   constant, samples saturated — each should produce a rate-limited
   warning, not silent failure.

## Test procedure

1. Quiet room — confirm low, stable RMS, no errors.
2. Speech at normal volume — confirm RMS responds proportionally.
3. A sharp transient (clap/finger snap) — confirm it's detectable
   distinctly from sustained sound if your application needs that.
4. Physical handling/tapping — confirm no crash, no stuck/saturated
   fault triggered by ordinary handling noise.

## See also

`docs/lessons/inmp441-i2s-bringup.md`,
`docs/lessons/i2s-32bit-container-24bit-mic-handling.md`,
`docs/lessons/i2s-read-nonzero-timeout.md`, `docs/current/MICROPHONE.md`.
