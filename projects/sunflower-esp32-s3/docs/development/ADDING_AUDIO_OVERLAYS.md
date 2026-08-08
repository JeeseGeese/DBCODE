# SOP: Adding a New Audio Overlay

## BaseEffect vs. AudioOverlay — read this first

A **base effect** (`docs/development/ADDING_LED_EFFECTS.md`) is the
continuous background animation, always running, selected exclusively
(exactly one active at a time). An **audio overlay** is a separate,
optional layer **blended on top of whichever base effect is currently
active** — it never replaces the base effect, and switching overlays
never resets the base effect's own animation state. Overlay *selection*
(`selectedOverlayMode`, always one of the 8 real overlays) and overlay
*enabled state* (`audioOverlayEnabled`, a separate bool) are
independent — `OFF` is not a selectable mode, it's the enabled flag
being false while a real mode stays remembered underneath.

**Do not collapse these into one mutually-exclusive mode system.**
That was an earlier design this project deliberately moved away from —
see `docs/current/AUDIO_OVERLAYS.md`.

## 1. Add a new `AudioOverlay` enum value

`include/AudioOverlays.h`:

```cpp
enum class AudioOverlay : uint8_t {
  OFF, PULSE, RIPPLE, SPARK, LIGHTNING,
  BASS_BLOOM, SPECTRUM_WAVE, COLOR_FLOOD, COMET_BURST,
  YOUR_OVERLAY,  // add here, before COUNT
  COUNT,
};
```

Order relative to the other real overlays doesn't matter (unlike
`BaseEffect`'s `AUTO_SHOWCASE` constraint) — `OFF` must stay first and
`COUNT` must stay last.

## 2. Register its name

`src/AudioOverlays.cpp`, top of file — add to `AUDIO_OVERLAY_NAMES` at
the matching index:

```cpp
const char *AUDIO_OVERLAY_NAMES[NUM_AUDIO_OVERLAYS] = {
    "OFF", "PULSE", ..., "COMET_BURST", "YOUR_OVERLAY",
};
```

## 3. Register it with cycling

No extra step needed — `Controls.cpp`'s `advanceOverlayMode()` cycles
through `1..NUM_AUDIO_OVERLAYS-1` generically (skipping index 0/`OFF`,
which is never a selectable mode). Adding the enum value and name is
sufficient for it to appear in the Mode-button/`o` cycle and in the
`overlays` word-command listing.

## 4. Access AudioFeatures (indirectly — via AudioVisualState)

**Do not read `AudioFeatures`/`AudioAnalyzer` directly from your
overlay.** Every overlay reads the richer, already-computed
`AudioVisualState` instead (`include/AudioVisualState.h`):

```cpp
struct AudioVisualState {
  float level, envelope, bass, transient, transientStrength;
  bool clap;
  float lowRange, midRange, highRange;      // derived control bands, NOT real frequency bands
  uint8_t energy8, bass8, transient8;       // 0-255 integer copies
};
```

It's recomputed once per frame (`updateAudioVisualState()`, called from
`main.cpp` before any overlay renders) and passed into
`applyAudioOverlay()` as `const AudioVisualState &audio`.

## 5. Write your blend function

`src/AudioOverlays.cpp`, following the existing pattern:

```cpp
static void applyYourOverlay(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  // read v.level / v.bass / v.transient / v.highRange / v.clap, write into buf[i]
}
```

Add `case AudioOverlay::YOUR_OVERLAY: applyYourOverlay(buf, audio, now); break;`
to `applyAudioOverlay()`'s switch. If you need persistent state (a
particle pool, a phase counter), add a `static void
resetYourOverlay()` and a case in `resetAudioOverlayState()`'s switch
too — same pattern as `LedEffects.cpp`, see
`docs/development/ADDING_LED_EFFECTS.md` #6.

## 6. Use normalized/envelope/transient/bass correctly

- `v.level`/`v.envelope` — overall loudness, 0..1, already smoothed.
  Use for continuous intensity (e.g. pulse size).
- `v.bass` — a single-pole low-pass **proxy**, not real bass
  extraction. Fine for "thicken/warm the effect when bass-heavy," not
  fine to document as frequency-selective.
- `v.transient`/`v.transientStrength` — a decaying spike / raw rise
  rate. Use for punchy, momentary accents (sparks, edge highlights).
- `v.clap` — a single-frame edge, already cooldown-gated upstream.
  Use for discrete "launch an event" triggers (a burst, a direction
  reverse), not for continuous modulation.
- `v.lowRange`/`v.midRange`/`v.highRange` — derived animation-control
  heuristics (see `docs/current/AUDIO_OVERLAYS.md`'s "Signal provenance").
  Treat as three independently-varying control channels, not a
  spectrum.
- Use `AudioPalette.h`'s `audioEnergyColor(energy, bass, transient)`
  for color instead of a fixed color, so your overlay's hue shifts with
  the audio state like every existing one.

## 7-9. Preserve OFF/restore, mute, and visual-cue behavior — automatically

You don't implement any of these — they're handled once, centrally:

- **OFF/restore**: `main.cpp` only calls `applyAudioOverlay()` when
  `overlay != AudioOverlay::OFF`; your function is simply never called
  while disabled. `resetAudioOverlayState()` is called when the
  *selected* mode changes (even while disabled) so no stale state waits
  to be seen.
- **Mute**: handled entirely in `main.cpp`'s render branch (renders
  black, skips the base+overlay pipeline). Nothing in your overlay
  needs to check mute state.
- **Visual cue** (green/red flash on enable/disable): fires from
  `Controls.cpp`'s `toggleOverlayOffOn()`, independent of which overlay
  is selected. Nothing to do here either.

## 10. Avoid resetting the base animation

Never write to any state owned by `LedEffects.cpp`, and never call
`resetBaseEffectState()` from your overlay. Overlays only ever read
`buf` (already filled by the base effect) and add to it.

## 11. Avoid extra `strip.show()` calls

Never call `strip.show()` or touch `strip` from `AudioOverlays.cpp` —
same rule as base effects, see
`docs/development/ADDING_LED_EFFECTS.md` #11. Overlays only write into
the `RGB8 *buf` they're handed.

## 12. Keep overlay rendering non-blocking

No `delay()`, no blocking waits. Drive any multi-phase behavior
(particle pools, cooldown timers) from `now` and stored timestamps —
follow `RIPPLE`'s or `SPARK`'s fixed-pool pattern (`freeSparkSlot()`-
style: reuse the oldest/inactive slot, never grow a container
dynamically).

## 13. Update tests

Same coverage gap as base effects — no host test currently exercises
`AudioOverlays.cpp` (see `docs/current/KNOWN_LIMITATIONS.md`). If your
overlay has host-testable pure logic (particle-pool slot selection,
envelope math), consider adding one, following the existing
`test_host/*.cpp` convention.

## 14. Update docs

- `README.md`'s "Audio overlays" list (under "Architecture: base
  effects vs. audio overlays").
- `docs/development/LED_AUDIO_QUICK_REFERENCE.md`'s overlay list.
- Physical test procedure (`README.md`) if your overlay needs a
  specific new test step.
