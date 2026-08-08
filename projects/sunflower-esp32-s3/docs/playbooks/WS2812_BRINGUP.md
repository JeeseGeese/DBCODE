# Playbook: WS2812/Addressable LED Bring-Up

## Wiring (do this before any code)

1. **Never power the strip from the MCU's own 3V3/5V pin** — use a
   separate, adequately-rated supply. See
   `docs/lessons/ws2812-power-data-separation.md`.
2. Data line: one GPIO (validated per `GPIO_VALIDATION.md`) to the
   strip's DIN.
3. **Common ground** between the LED supply, the strip, and the MCU —
   do not float it.
4. If the strip is long/high-count, consider a large bulk capacitor
   across the LED supply near the strip's power input, and a small
   series resistor (~300-500Ω) on the data line close to the first
   LED — standard WS2812 practice, reduces data-line ringing/EMI.

## Software

1. Start with a small, deliberately conservative brightness for the
   first test (avoid full-white-full-brightness on first power-up).
2. Confirm color order (`NEO_GRB` vs. `NEO_RGB`, etc.) with a
   deliberate red/green/blue test sequence — do not assume the library
   default matches your specific LEDs.
3. Confirm physical LED index order matches your assumption (which end
   is index 0, which direction) — do not assume from wiring alone; an
   interactive per-index walk tool is worth building for any strip
   with more than a handful of LEDs (see Sunny's LED index-mapping
   diagnostic, `archive/led_experiments/` if archived, or
   `docs/current/LED_ENGINE.md` for the current approach).
4. Add a software current estimator/limiter as a bring-up safety aid
   (see `docs/lessons/led-power-limiting.md`) — not a substitute for
   correct electrical sizing.

## Test procedure

1. Solid red, then green, then blue, full strip — confirm color order
   and that every LED responds.
2. Walk one lit LED down the strip — confirm index order and that no
   LED is dead/stuck.
3. Run at intended maximum brightness/pattern for several minutes —
   confirm no flicker, no reset, no thermal issue.

## See also

`docs/lessons/ws2812-power-data-separation.md`,
`docs/lessons/led-power-limiting.md`, `docs/current/LED_ENGINE.md`.
