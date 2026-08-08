# Architecture — LED Pipeline

How LED rendering is structured and why. For the current effect/overlay
lists and exact constants, see `docs/current/LED_ENGINE.md`/
`AUDIO_OVERLAYS.md`. For how to add to it, see `docs/development/`.

## The compositing model

```
final LED frame =
    base effect            (always running, exactly one selected)
  + audio overlay           (optional, blended on top, independent selection)
  + global brightness       (applied once, centrally)
  + power-limit scaling     (applied once, centrally, last)
```

Then: exactly one `strip.setPixelColor()` pass, exactly one
`strip.show()` call.

## Why two separate concepts (BaseEffect vs. AudioOverlay), not one mode system

An earlier design point had a single mutually-exclusive "mode" concept
covering both background animation and audio reactivity — collapsing
these makes it impossible to have "my favorite background animation,
audio-reactive or not" as an independent user choice, and makes
enable/disable state and selection state the same variable, which
loses information (you can't remember "SPARK is selected" while
overlay is off). Splitting them into an always-on base layer plus an
independently-selectable, independently-enableable overlay layer fixes
both. See `DESIGN_DECISIONS.md`.

## Why brightness and power-limiting are centralized, not per-effect

If every effect/overlay had to implement its own brightness scaling,
(a) every new effect author would need to remember to do it correctly,
and (b) the power limiter couldn't see the *true* combined brightness
of base+overlay to make a correct current estimate — it needs the
final composited frame, not each layer's opinion of its own
brightness. Centralizing both in `main.cpp`, after compositing, is the
only place either can be correct. See `DESIGN_DECISIONS.md`'s "Why
power limiting?" and `docs/standards/POWER_STANDARD.md`.

## Why exactly one `strip.show()` per frame

`strip.show()` sends the entire frame buffer over the LED data line;
calling it more than once per logical frame either wastes time or (if
partial buffers are sent) can visibly tear/flicker. Enforcing "one
composited buffer, one `show()` call" as an invariant — checked by
convention, not compiler — removes a whole class of bugs where an
effect or overlay accidentally reaches for `strip` directly.

## Frame timing

Target ~50fps (`FRAME_INTERVAL_MS=20`, see `docs/current/LED_ENGINE.md`).
Every render function is a pure(-ish) function of `now` (`millis()`)
plus optionally some small persistent state — never a sleep/blocking
wait. See `docs/standards/TESTING_STANDARD.md`'s non-blocking
companion principle and `docs/lessons/non-blocking-firmware-architecture.md`.

## See also

`docs/current/LED_ENGINE.md`, `docs/current/AUDIO_OVERLAYS.md`,
`docs/development/ADDING_LED_EFFECTS.md`,
`docs/development/ADDING_AUDIO_OVERLAYS.md`.
