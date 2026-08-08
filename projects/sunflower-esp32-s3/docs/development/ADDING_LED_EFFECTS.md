# SOP: Adding a New LED Base Effect

Goal: "Add a meteor effect" should require reading only this file plus
2-3 source files, not the project's speaker/motor/Pi history.

## 1. Files you need to read

- `include/LedEffects.h` — the enum, the shared helpers, the function
  contracts.
- `src/LedEffects.cpp` — every existing effect, as worked examples.
- `include/Config.h` — only the section with existing effect timing
  constants (search for the effect names already there, e.g.
  `PETAL_BREATHE_PERIOD_MS`), for the naming/placement convention.

That's it for a typical new effect. You do **not** need
`AudioOverlays.cpp`, `SpeakerTest.cpp`, any motor file, or anything
under `archive/`.

## 2. Where the `BaseEffect` enum lives

`include/LedEffects.h`:

```cpp
enum class BaseEffect : uint8_t {
  PETAL_BREATHE, COLOR_WAVE, SUNSET_SPIN, RAINBOW_FLOW,
  SPARKLE_BLOOM, FIREFLY_GARDEN, AURORA, SOLAR_FLARE,
  AUTO_SHOWCASE,  // NOT a real effect -- keep this last
  COUNT,
};
```

**Add your new value ABOVE `AUTO_SHOWCASE`, never below it.**
`AutoShowcase.cpp` derives its rotation list from
`NUM_REAL_BASE_EFFECTS` (= the index of `AUTO_SHOWCASE`), so a new
effect placed above it is automatically included in the showcase
rotation with no further change needed.

## 3. How effect names are registered

`src/LedEffects.cpp`, top of file:

```cpp
const char *BASE_EFFECT_NAMES[NUM_BASE_EFFECTS] = {
    "PETAL_BREATHE", "COLOR_WAVE", ..., "AUTO_SHOWCASE",
};
```

Add your name at the same index position as your new enum value.
Mismatched order here silently mislabels the wrong effect in every
`[EFFECT] Base: ...` log line — keep this array in sync with the enum
by construction (add both in the same edit).

## 4. How rendering is dispatched

Two functions in `LedEffects.cpp`, both `switch`-dispatched on
`BaseEffect`:

```cpp
void resetBaseEffectState(BaseEffect effect, unsigned long now);  // optional per-effect reset
void renderBaseEffect(BaseEffect effect, RGB8 *buf, unsigned long now);  // required
```

Add a `case BaseEffect::YOUR_EFFECT:` to `renderBaseEffect()`'s switch,
calling a new `static void renderYourEffect(RGB8 *buf, unsigned long
now)` function you write above it (follow the existing
`renderPetalBreathe()`/`renderRainbowFlow()`/etc. as templates). Add a
case to `resetBaseEffectState()` too, **only if** your effect has
persistent per-LED/per-particle state (see #6 below) — purely
time-derived effects (most of the existing 8) need no reset case at
all; the `default: break;` already covers them.

## 5. How state is stored

Two patterns already in use:

- **Purely time-derived** (no state): every pixel is computed as a
  function of `now` alone (e.g. `PETAL_BREATHE`, `RAINBOW_FLOW`). No
  static variables needed. Prefer this when possible — it's simpler
  and needs no reset logic.
- **Persistent per-element state** (e.g. `SPARKLE_BLOOM`,
  `FIREFLY_GARDEN`): file-scope `static` arrays/structs sized to
  `NUM_LEDS` or a small fixed pool count, holding things like
  `active`/`startMs`/`position`. Example from `SPARKLE_BLOOM`:

```cpp
static bool sparkleActive[NUM_LEDS];
static unsigned long sparkleStartMs[NUM_LEDS];
static void resetSparkleBloom() { memset(sparkleActive, 0, sizeof(sparkleActive)); }
```

## 6. Adding persistent per-effect state safely

- Declare the `static` state at file scope in `LedEffects.cpp`, near
  your render function (not in a header — it's private to this file).
- Write a `static void resetYourEffect()` (or `(unsigned long now)` if
  you need to stagger initial timing) that clears it, and call it from
  `resetBaseEffectState()`'s switch.
- `resetBaseEffectState()` is called by `Controls.cpp`'s
  `setBaseEffect()` **every time this effect is newly selected** — so
  a user cycling back to your effect never sees stale state from a
  previous session with it.
- Never allocate on the heap (`new`/`malloc`/`std::vector`) — every
  existing effect uses fixed-size arrays sized at compile time.

## 7. Frame-timing expectations

`main.cpp` targets `FRAME_INTERVAL_MS = 20` (~50fps, `Config.h`) — your
render function is called at most once per that interval, always with
the current `now` (`millis()`). Use `now` directly for animation phase
(see `phaseFromPeriod()`/`sine8()`/`hump8()` helpers) — never call
`millis()` yourself inside the effect, and never call `delay()`
anywhere in this file.

## 8. How brightness is applied

**Not your effect's job.** `renderBaseEffect()` writes full-strength
0-255 color values into `buf`; brightness scaling happens once, later,
in `main.cpp`'s render loop (`scaleClamp(frameBuffer[i],
brightnessScale)`), after the overlay has also been composited. Do not
scale for brightness inside your effect.

## 9. How the power limiter interacts with effects

Also not your effect's job — `main.cpp`'s `applyPowerLimit()` runs
after brightness scaling, estimating current draw and scaling the
whole frame down further if needed. Your effect just needs to produce
reasonable, not-artificially-inflated color values; the pipeline
handles the rest.

## 10. How AudioOverlays are layered after your effect

If an overlay is enabled, `applyAudioOverlay()` runs **after**
`renderBaseEffect()`, blending into the same `buf` your effect just
filled (via `addClamp()`/`maxChannel()`, never a raw overwrite) — see
`docs/development/ADDING_AUDIO_OVERLAYS.md`. Your effect doesn't need
to know or do anything about this; just fill `buf` with the values you
want as the base layer.

## 11. Why `strip.show()` is called only once per composed frame

`main.cpp` composites base effect + overlay + brightness + power-limit
into one `RGB8 frameBuffer[NUM_LEDS]`, converts it to the NeoPixel
library's color format, and calls `strip.show()` exactly once at the
end. **Never call `strip.show()` (or touch `strip` at all) from inside
`LedEffects.cpp`** — effects only ever write into the `RGB8 *buf`
they're handed. This keeps there being exactly one frame per `show()`
call and avoids tearing/flicker from multiple partial updates.

## 12. Keeping effects non-blocking

No `delay()`, no blocking loops waiting on time, no `Serial` reads.
Your render function must return quickly (well under 1ms is typical
for the existing 8) so the rest of `loop()` — motor updates, audio
capture, serial dispatch — keeps running at full rate. If you need a
multi-phase animation, drive it from `now` and stored timestamps
(exactly like `FIREFLY_GARDEN`'s spawn/lifetime timing), never a
software delay.

## 13. How to test mode cycling

1. Build and upload (see `docs/current/TESTING.md`).
2. Press the Mode button repeatedly (or serial `n`/`p`, or the word
   command `effects` to list all base effects by name) and confirm
   your new effect appears in the cycle, renders correctly, and that
   `AUTO_SHOWCASE` (one more press past the last real effect) includes
   it in rotation.
3. Confirm `[EFFECT] Base: YOUR_EFFECT_NAME` prints with the correct
   name when selected.
4. Leave it selected for a minute; confirm no visual glitches, no
   uncommanded brightness change, no serial errors.
5. Switch away and back; confirm it restarts cleanly (no leftover
   state from before), and that `AUTO_SHOWCASE` crossfades into/out of
   it smoothly.

## 14. Host tests that should be added/updated

None currently exist for `LedEffects.cpp` (see
`docs/current/KNOWN_LIMITATIONS.md`). If your new effect has any
host-testable pure logic (e.g. a novel color-blend helper, a
non-trivial timing calculation), consider adding a
`test_host/led_effects_*.cpp` file following the existing
`test_host/*.cpp` convention (standalone g++, no Arduino dependency,
constants/logic mirrored inline) — this would also be the first host
test for this subsystem, closing a real coverage gap.

## 15. Docs that should be updated

- `README.md`'s "Base effects" list (under "Architecture: base effects
  vs. audio overlays").
- `docs/current/LED_ENGINE.md`'s effect table, if this is meant to become
  part of a future baseline (V1 itself should stay describing what was
  true at the V1 checkpoint — add new effects to the *current*
  `README.md`/`CURRENT_STATUS.md`, not by editing `docs/current/` files
  after the fact).
- `docs/development/LED_AUDIO_QUICK_REFERENCE.md`'s effect list.
