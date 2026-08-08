# Sunny — Audio Overlay Engine (Current)

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

Reference snapshot. For the actionable "how do I add an overlay"
procedure, see `docs/development/ADDING_AUDIO_OVERLAYS.md`.

## Files

`include/AudioOverlays.h` / `src/AudioOverlays.cpp` — the 8 overlays.
`include/AudioVisualState.h` / `src/AudioVisualState.cpp` — the richer
per-frame signal overlays actually read. `include/AudioPalette.h` /
`src/AudioPalette.cpp` — shared audio-reactive color helpers.

## BaseEffect vs. AudioOverlay — the key distinction

A **base effect** is the continuous background animation, always
running (`renderBaseEffect()`/`renderAutoShowcase()`). An **audio
overlay** is an independent, optional layer **blended on top of
whichever base effect is currently active** — it does not replace it,
and switching overlays never alters the base effect's own animation
state. `selectedOverlayMode` (which overlay would render) and
`audioOverlayEnabled` (whether it's currently rendering at all) are
two independent booleans/values — `OFF` is not a member of the
selected-mode cycle, it's a separate enabled flag. **Do not collapse
these back into one mutually-exclusive mode system** — that was
explicitly designed out.

## The `AudioOverlay` enum (`AudioOverlays.h`)

```cpp
enum class AudioOverlay : uint8_t {
  OFF, PULSE, RIPPLE, SPARK, LIGHTNING,
  BASS_BLOOM, SPECTRUM_WAVE, COLOR_FLOOD, COMET_BURST,
  COUNT,
};
```

| Overlay | Behavior |
|---|---|
| PULSE | breathing/traveling energy ring from center; bass thickens+warms it; transients add edge accents; claps launch a separate impact wave |
| RIPPLE | up to 6 concurrent expanding rings (fixed pool), origin alternates center/ends; bass=wider/slower, transient=narrower/faster |
| SPARK | up to 10 concurrent particles (fixed pool); level/transient/bass each spawn a different particle character; clap = bounded 5-spark burst |
| LIGHTNING | up to 3 branching bolts (fixed pool), cooldown-gated, capped intensity; sustained bass adds a background storm glow |
| BASS_BLOOM | large center bloom (mirrors at strong bass) + outer ring + transient outline + smoothly decaying per-pixel trail |
| SPECTRUM_WAVE | multicolor hue wave; level=amplitude, bass=thickness/speed, clap reverses direction |
| COLOR_FLOOD | bass fills from left, derived "mid" control fills from right, transient sparkles the edges, clap floods+fades |
| COMET_BURST | up to 6 concurrent directional comets (fixed pool) with fading tails, launched from either end |

## Signal provenance — read this before writing a new overlay

**There is no FFT or filter bank anywhere in this project.**
`AudioVisualState.bass` is the existing single-pole low-pass proxy from
`AudioAnalyzer` (a real, if simple, low-pass filter — not a
frequency-selective measurement). `lowRange`/`midRange`/`highRange` are
further **derived animation-control heuristics**, not real frequency
bands: `lowRange` reuses `bass` directly; `highRange` is a decaying
"spike" signal seeded by transient/clap edges (behaves visually like
high-frequency content because percussive sounds do tend to trigger
transients, but it is not a measurement of one); `midRange` is the
broadband envelope with low/high subtracted out. Treat these as three
independently varying **control channels for animation**, never
document them as a spectrum analyzer.

## The `AudioVisualState` struct (`AudioVisualState.h`)

`level`/`envelope` (alias of `AudioFeatures.envelope`), `bass`
(=`lowFrequencyEnergy`), `transient` (decaying 0..1 spike),
`transientStrength` (raw passthrough), `clap` (passthrough),
`lowRange`/`midRange`/`highRange` (see above), plus `energy8`/`bass8`/
`transient8` (0-255 integer copies for cheap per-LED math). Recomputed
exactly once per rendered frame, before any overlay renders.

## Blending mechanism

Overlays write directly into the same `RGB8 *buf` the base effect just
filled, using `addClamp()` (per-channel saturating add — never
overflow/wraps), `maxChannel()` (poke-through without darkening), or
`scaledColor()`/`blendAudioColors()` (`lerpRGB()` wrapper). Every
overlay deliberately caps its own blend/accent amounts (e.g. the
transient near-white accent in `audioEnergyColor()` is capped at a 25%
blend) so accents stay accents rather than causing a white blowout —
`applyPowerLimit()` is the final safety net regardless.

## Enable/disable and restore behavior

Turning the overlay off does **not** erase `selectedOverlayMode` — it
sets `audioOverlayEnabled = false` and the base effect keeps rendering
unmodified. Turning it back on resumes the same mode, not a different
one. `resetAudioOverlayState()` clears an overlay's persistent pools
(active ripples/sparks/comets/flash timers) whenever the *selected*
mode changes (even while disabled) or when re-enabled, so switching
never leaves stale visual artifacts armed.

## Visual confirmation cue

See `LED_ENGINE.md`'s "Diagnostics / cues" section — one green flash on
enable, two red flashes on disable, suppressed while muted, at a fixed
45% brightness cap, independent of base effect/overlay selection.

## Mute interaction

While muted, `main.cpp`'s render branch renders solid black once (not
every frame) and skips the whole base+overlay pipeline entirely —
overlay/effect state is never corrupted by this, since every persistent
element ages off stored timestamps rather than a per-frame counter.

## Not yet host-tested

Same gap as `LED_ENGINE.md` notes — no `test_host/*.cpp` file currently
covers overlay blending/pool logic.
