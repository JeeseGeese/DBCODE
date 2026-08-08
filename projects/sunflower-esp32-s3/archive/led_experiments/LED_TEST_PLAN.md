# LED Test Plan — sunflower-esp32-s3

Planned test coverage for the 58-LED WS2812B strip driven from GPIO4. This
plan tracks tests that are planned/in-progress; it is not itself firmware —
implementations should live in `projects/sunflower-esp32-s3/src/` (or a
dedicated test variant) as they're built out.

## Planned tests

1. **Individual LED addressing (0 → 57)** — light each LED one at a time in
   ascending index order, confirm correct physical position and no ghosting
   on neighboring pixels.
2. **Reverse addressing (57 → 0)** — same as above, descending index order.
3. **Color verification** — red, green, blue, white, yellow, cyan, and
   purple, confirmed against expected RGB output on every pixel.
4. **Brightness ramp** — sweep brightness from 0 to the operating maximum,
   confirm smooth, monotonic perceived brightness change with no flicker or
   dropout at any step.
5. **Walking-pixel test** — single lit pixel moving along the strip,
   confirm consistent timing and no skipped/duplicated pixels.
6. **Forward fill** — progressive fill from LED 0 through LED 57.
7. **Reverse fill** — progressive fill from LED 57 through LED 0.
8. **Rainbow animation** — full-strip rainbow cycle, confirm smooth hue
   transition and consistent frame timing.
9. **Theater chase** — classic marquee/theater-chase pattern.
10. **Breathing effect** — smooth brightness pulse (fade in/out) on a solid
    color.
11. **Twinkle effect** — random sparkle/twinkle pattern across the strip.
12. **Long-duration stability test** — extended run (target: multi-hour)
    checking for crashes, memory leaks, watchdog resets, or visual
    degradation over time.
13. **Power and voltage-drop observations** — measure supply voltage at the
    strip's power input and at the far end (LED 57) under worst-case
    brightness/color load; note any visible color-shift or dimming along
    the strip caused by voltage drop.

## Pass/Fail results

| # | Test                          | Date | Result | Notes |
|---|--------------------------------|------|--------|-------|
| 1 | Individual LED addressing      |      |        |       |
| 2 | Reverse addressing              |      |        |       |
| 3 | Color verification              |      |        |       |
| 4 | Brightness ramp                 |      |        |       |
| 5 | Walking-pixel test               |      |        |       |
| 6 | Forward fill                     |      |        |       |
| 7 | Reverse fill                     |      |        |       |
| 8 | Rainbow animation                |      |        |       |
| 9 | Theater chase                    |      |        |       |
| 10| Breathing effect                 |      |        |       |
| 11| Twinkle effect                   |      |        |       |
| 12| Long-duration stability test     |      |        |       |
| 13| Power and voltage-drop observations |   |        |       |
