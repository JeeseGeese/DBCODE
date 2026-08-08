# Sunny — LED Base Effect Engine (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

This is a **reference snapshot**. For the actionable "how do I add an
effect" procedure, see `docs/development/ADDING_LED_EFFECTS.md`. For a
compact working-context file, see
`docs/development/LED_AUDIO_QUICK_REFERENCE.md`.

## Files

`include/LedEffects.h` / `src/LedEffects.cpp` — the 8 real base
effects plus shared render helpers (`RGB8`, `paletteLookup()`,
`sine8()`/`hump8()`, `hueToRGB()`, `addClamp()`/`maxChannel()`/
`scaleClamp()`/`lerpRGB()`).

## The `BaseEffect` enum (`LedEffects.h`)

```cpp
enum class BaseEffect : uint8_t {
  PETAL_BREATHE, COLOR_WAVE, SUNSET_SPIN, RAINBOW_FLOW,
  SPARKLE_BLOOM, FIREFLY_GARDEN, AURORA, SOLAR_FLARE,
  AUTO_SHOWCASE,  // not a real effect -- see AutoShowcase.h
  COUNT,
};
```

`AUTO_SHOWCASE` is deliberately last before `COUNT` — `AutoShowcase.cpp`
derives its rotation list as "every real effect with index <
NUM_REAL_BASE_EFFECTS" rather than a manually duplicated list. Any new
real effect must be added **above** `AUTO_SHOWCASE`, never below.

| Effect | Visual |
|---|---|
| PETAL_BREATHE | warm sunflower palette, breathing intensity, spatial phase spread, slow hue drift |
| COLOR_WAVE | moving blue→violet→magenta→pink gradient wave |
| SUNSET_SPIN | slow rotating warm gradient (orange/gold/red/purple) |
| RAINBOW_FLOW | full-saturation hue rotation + independent secondary brightness wave |
| SPARKLE_BLOOM | dim animated base glow + warm rising/decaying sparkles (persistent per-LED state) |
| FIREFLY_GARDEN | dark ambient base + drifting yellow-green/green points, independent lifetimes |
| AURORA | two layered cyan/blue/purple bands at different speeds |
| SOLAR_FLARE | flowing warm background + an occasional traveling brighter flare |
| AUTO_SHOWCASE | rotates through effects 1-8, crossfading — see `AutoShowcase.cpp` |

## Render pipeline (exact order, from `main.cpp`'s `loop()`)

```
1. updateAudioVisualState(getAudioFeatures(), now)   // keeps 'v'/status fresh
2. renderVisualCue(now, frameBuffer) -- if a cue is active, it OWNS the frame this tick
3. else: renderBaseEffect(effect, frameBuffer, now)   // or updateAutoShowcase()+renderAutoShowcase()
4. if overlay != OFF: applyAudioOverlay(overlay, frameBuffer, audio, now)
5. scale frameBuffer by brightness (9-level table, or the cue's fixed cap, or MotorPowerGuard's dim override)
6. applyPowerLimit(frameBuffer)   // software current estimate, scales down if over budget
7. strip.setPixelColor() for every LED, then EXACTLY ONE strip.show() call
```

`strip.show()` is called from exactly one place per composed frame
(`main.cpp`, tagged `// exactly one show() per rendered frame`) —
diagnostic modes (LED index mapping, `HardwareTest`) own the strip
directly and are mutually exclusive with the normal pipeline, never
calling `show()` in the same tick as it.

Frame pacing: `FRAME_INTERVAL_MS = 20` (`Config.h`, ~50fps target) —
`loop()` returns early if less than this has elapsed since the last
rendered frame, but `updateAudioAnalyzer()`/motor updates/serial
service still run every iteration regardless.

## State storage pattern

Purely time-derived effects (PETAL_BREATHE, COLOR_WAVE, SUNSET_SPIN,
RAINBOW_FLOW, AURORA, SOLAR_FLARE) need **no persistent state** —
every pixel is a pure function of `now`. Effects with real per-element
lifetimes (SPARKLE_BLOOM, FIREFLY_GARDEN) use file-scope `static`
arrays (e.g. `static bool sparkleActive[NUM_LEDS]`) reset by
`resetBaseEffectState()` whenever the effect is selected, so a newly
selected effect never inherits stale state from whichever one ran
before it.

## Brightness and power limiting

Brightness (a 9-level table, GPIO17 button) and power limiting
(`applyPowerLimit()`, a software current estimate) are both applied
**after** base effect + overlay compositing, in `main.cpp` — never
inside an individual effect or overlay function. See `POWER.md` for the
exact estimation formula.

## Diagnostics / cues

`VisualCue.cpp` — a short, non-blocking full-strip flash confirming
overlay enable/disable (and a few motor-mode/rejection cues). When
active, it fully replaces the frame that would otherwise render (a
"suppress and flash" mechanism, at its own fixed 45%-brightness cap),
not a compositing layer — see `AUDIO_OVERLAYS.md` for why per-state
subtle accents were deferred rather than extending this mechanism.

## Not yet host-tested

No `test_host/*.cpp` file currently covers `LedEffects.cpp` or
`AudioOverlays.cpp` logic — all 18 existing host tests cover
`MusicMotorController` and the speaker suite. See
`docs/development/ADDING_LED_EFFECTS.md` for the recommendation to add
host-testable coverage (e.g. `paletteLookup()`, `hueToRGB()`, envelope
math) going forward.
