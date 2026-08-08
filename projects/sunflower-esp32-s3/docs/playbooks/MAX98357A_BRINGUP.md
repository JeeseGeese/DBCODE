# Playbook: MAX98357A Amplifier Bring-Up

## Pre-power checklist

- [ ] VIN connected to a rail rated for this part (confirm voltage —
      do not assume).
- [ ] GND common with the rest of the system.
- [ ] BCLK/LRC/DIN confirmed as 3.3V logic from the MCU (not driving
      or expecting 5V on these lines).
- [ ] Speaker connected only to the amplifier's own output terminals —
      **never to ground** (bridge-tied/BTL output — see
      `docs/lessons/speaker-bridge-tied-output.md`).
- [ ] No continuity short between the two speaker output terminals or
      from either to ground.
- [ ] `SD` (shutdown/enable) held at its disabled state (commonly GND)
      before power-up.
- [ ] `GAIN` pin state physically inspected and documented (float,
      tied high, tied low, or resistor value) — see
      `docs/lessons/max98357a-gain-configuration.md`. Never assume.
- [ ] Speaker impedance/wattage rating confirmed against what the
      amplifier expects.

## Startup sequence

1. Power up with `SD` disabled and I2S not yet configured.
2. Initialize I2S, immediately prime TX with digital silence.
3. Confirm via serial log that silence is actively transmitting.
4. **Only then** move `SD` to its enabled state.
5. Send a low-amplitude test tone (a small fraction of full digital
   scale) as the first sound, not full volume.

## If the output is distorted/buzzing

Follow `docs/playbooks/I2S_DEBUGGING.md`'s "Diagnosing" section and
`docs/lessons/audio-buzz-noise-diagnosis.md` — rule out digital-format
bugs before assuming an electrical cause, and don't claim a root cause
you haven't isolated with a control condition.

## See also

`docs/lessons/max98357a-bringup.md`, `docs/lessons/max98357a-sd-usage.md`,
`docs/lessons/max98357a-gain-configuration.md`,
`docs/lessons/speaker-bridge-tied-output.md`, `docs/current/SPEAKER.md`.
