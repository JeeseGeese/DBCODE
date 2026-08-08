# Playbook: I2S Debugging

## If you need simultaneous RX + TX (mic + speaker) on one ESP32

1. **Prefer one full-duplex master controller** over two coordinated
   controllers (one RX master + one TX slave sharing clocks) — the
   two-controller approach has failed conclusively on at least one
   real project (see `docs/lessons/shared-full-duplex-i2s.md`): the
   slave TX port returned `bytesWritten=0` forever, and an unbounded
   wait froze the whole application.
2. Configure `I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX` on one
   controller, one `i2s_pin_config_t` for both directions.
3. Establish **one sole owner** of driver install/config/uninstall for
   that controller — every other module only calls `i2s_read()`/
   `i2s_write()` on it, never reconfigures it.

## Write-path discipline (TX)

- Use a small, **bounded**, non-zero wait
  (`pdMS_TO_TICKS(~10-20)`) — never `portMAX_DELAY`. An unbounded wait
  on a TX write has been observed to freeze an entire application on
  real hardware, not just the write call.
- Generate samples as a pure function of an absolute sample index, so
  a partial write (fewer bytes accepted than requested) can be handled
  exactly — advance your cursor only by frames actually accepted,
  never skip or duplicate.
- Prime the TX buffer with real silence immediately on init, before
  anything downstream could be enabled — never let a data line float
  or carry garbage.

## Read-path discipline (RX)

- Same bounded-wait principle — a 0-tick poll can race the DMA's own
  buffer-ready signal (see
  `docs/lessons/i2s-read-nonzero-timeout.md`).
- Verify slot index and bit-shift assumptions with a real captured
  trace, not just datasheet math (see
  `docs/lessons/i2s-32bit-container-24bit-mic-handling.md`).

## Diagnosing "no sound" or "buzz/distortion"

1. Confirm write success first (log `err`/`written` vs. `requested`
   bytes every call, or a sampled subset) — a "silent" amplifier is
   often actually a 0-byte-write problem, not an analog problem.
2. Rule out digital-format bugs (mono/stereo duplication, slot order,
   sign handling, overflow, phase continuity across DMA buffer
   boundaries, double amplitude scaling) via source review before
   assuming electrical noise.
3. If the exact bit-packing within a slot is ambiguous (which portion
   of a wide slot the DAC actually reads), build an A/B diagnostic
   testing each candidate packing rather than guessing — see
   `docs/lessons/audio-buzz-noise-diagnosis.md`.
4. Test whether noise correlates with other loads on a shared power
   rail before concluding it's a digital/clock issue — see
   `POWER_BROWNOUT_DEBUGGING.md`.

## See also

`docs/lessons/shared-full-duplex-i2s.md`,
`docs/lessons/audio-buzz-noise-diagnosis.md`,
`docs/current/I2S_ARCHITECTURE.md`.
