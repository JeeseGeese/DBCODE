# Sunny — Electrical Wiring Detail (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

Distinguishes **physically verified wiring** (what's actually connected
and confirmed working) from **currently tested configuration** (a
specific choice among options, e.g. gain strapping) from **future
production recommendations** (not yet done). Do not blur these three.

## WS2812 LED strip — verified wiring

| Pin | Connection |
|---|---|
| DIN (data) | `ESP32 GPIO4 -> 330Ω series resistor -> WS2812 DIN` (added during V1.1 power/noise investigation, see `docs/current/POWER.md`) |
| VCC | shared 5V rail |
| GND | common ground |

The 330Ω series resistor is a **data-line signal-integrity measure**
(reduces ringing/reflections on the single-wire WS2812 protocol) —
**not** a power-rail component and provides no additional 5V current
capacity. It did not, by itself, resolve the V1.1 brownout
investigation (expected, since it addresses signal integrity rather
than current delivery) — see `docs/current/POWER.md`'s "LED data-line
series resistor" section. Recommended to carry forward into the final
PCB design unless future testing gives a specific reason otherwise.

## INMP441 microphone — verified wiring

| Pin | Connection |
|---|---|
| BCLK | GPIO6 |
| WS (LRCLK) | GPIO7 |
| SD (DATA) | GPIO15 |
| VDD | 3.3V |
| GND | common ground |
| L/R | tied to GND (selects LEFT channel) |

Channel: LEFT. Format: 24-bit sample, left-justified in a 32-bit I2S
word (see `I2S_ARCHITECTURE.md` for why the shared bus reads it this
way).

## MAX98357A amplifier — verified wiring

| Pin | Connection |
|---|---|
| VIN | shared 5V rail |
| GND | common ground |
| BCLK | GPIO6 (shared with INMP441) |
| LRC | GPIO7 (shared with INMP441) |
| DIN | GPIO16 |
| SD (shutdown/enable) | **currently tested**: tied to 3.3V (amplifier enabled) |
| GAIN | **currently tested**: tied to GND (a specific hardware-gain configuration, being evaluated for loudness — see `SPEAKER.md`) |
| Speaker output | connects only between SPK+ and SPK− — **never** to ground (MAX98357A output is bridge-tied/BTL) |

Current speaker: 40mm diameter, 4Ω, 3W full-range. Physically observed
to perform substantially better than an earlier, smaller toy speaker
tried during initial bring-up (see `SPEAKER.md`).

`SD` is a **manually moved** logic pin, not GPIO-driven by firmware —
moving it is part of the documented startup safety sequence (confirm
`[SPEAKER] Digital silence active` on serial before moving `SD` to
3.3V). `GAIN` is currently strapped to GND as a deliberate test
configuration to evaluate a higher fixed-gain setting; it is not
GPIO-controlled and has no firmware-visible state.

## Power decoupling (as physically installed)

- 1000 µF electrolytic capacitor across the 5V rail and GND.
- 1 µF capacitor across the 5V rail and GND.
- Neither capacitor is installed in series — both are shunt
  (parallel) decoupling across the rail.

This decoupling was added during speaker bring-up specifically to
reduce power-rail noise reaching the amplifier; it has not eliminated
the residual buzz/static (see `SPEAKER.md`) but is part of the
currently-tested configuration.

## DRV8833 motor driver — verified wiring

| Pin | Connection |
|---|---|
| IN1 | GPIO8 |
| IN2 | GPIO9 |
| VCC | shared 5V rail (see `POWER.md` — corrected 2026-08-01; an earlier bring-up phase ran this from the ESP32's own 3.3V pin, since superseded) |
| GND | common ground |
| OUT1/OUT2 | motor terminals |

`SLEEP`/`nSLEEP` is not wired and not driven by firmware.

## Future production recommendations (not yet done)

These are recommendations only — nothing below has been implemented:

- A dedicated external motor power supply, common-grounded with the
  ESP32, sized for the motor's actual current draw including startup
  inrush — removes the shared-5V-rail contention with LEDs/amplifier
  entirely rather than working around it (see `POWER.md`).
- Confirm the DRV8833 breakout's actual rated input voltage range
  against the 5V rail before any sustained/loaded production use.
- Measure supply voltage at each peripheral's own VIN/VCC pin during
  combined LED + amplifier + motor peak load — not yet done (see
  `docs/SPEAKER_BRINGUP_PLAN.md`'s preflight checklist, archived, for
  the specific measurements this requires).
- Soldered/production wiring in place of the current breadboard/bench
  connections, and eventually a PCB (see `ROADMAP.md`, V2.0).
