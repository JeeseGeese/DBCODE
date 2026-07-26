#pragma once

#include <Arduino.h>
#include "Config.h"

// A plain RGB triple, pre-brightness / pre-power-limit. Base effects and
// overlays both operate on buffers of these; the composition stage in
// main.cpp is the only place that scales for brightness/power and hands
// off to the NeoPixel library.
struct RGB8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// AUTO_SHOWCASE is deliberately last before COUNT: AutoShowcase.cpp derives
// its rotation list as "every effect with index < AUTO_SHOWCASE" rather
// than a manually duplicated list, so it stays correct automatically if
// more real effects are ever added above it. Keep any future real effects
// ABOVE AUTO_SHOWCASE in this list, never below.
enum class BaseEffect : uint8_t {
  PETAL_BREATHE,
  COLOR_WAVE,
  SUNSET_SPIN,
  RAINBOW_FLOW,
  SPARKLE_BLOOM,
  FIREFLY_GARDEN,
  AURORA,
  SOLAR_FLARE,
  AUTO_SHOWCASE, // not a real effect -- see AutoShowcase.h; renderBaseEffect()/resetBaseEffectState() never receive this value
  COUNT,
};
constexpr uint8_t NUM_BASE_EFFECTS = static_cast<uint8_t>(BaseEffect::COUNT);
constexpr uint8_t NUM_REAL_BASE_EFFECTS = static_cast<uint8_t>(BaseEffect::AUTO_SHOWCASE);

extern const char *BASE_EFFECT_NAMES[NUM_BASE_EFFECTS];

// One-time setup (builds the shared sine lookup table). Call once from setup().
void initLedEffects();

// Resets an effect's persistent animation state (phase, sparkle/firefly
// arrays, etc). Called whenever the base effect changes so a new effect
// doesn't inherit stale state from whichever one was previously selected.
void resetBaseEffectState(BaseEffect effect, unsigned long now);

// Renders `effect` into buf[0..NUM_LEDS). Does not touch brightness or
// power limiting -- full-strength color values only.
void renderBaseEffect(BaseEffect effect, RGB8 *buf, unsigned long now);

// --- Shared helpers, reused across effects and overlays ---

inline RGB8 makeRGB(uint8_t r, uint8_t g, uint8_t b) { return RGB8{r, g, b}; }

// Adds `add` onto `base` with per-channel saturation (no overflow wraparound).
inline void addClamp(RGB8 &base, const RGB8 &add) {
  base.r = (uint8_t)min(255, (int)base.r + (int)add.r);
  base.g = (uint8_t)min(255, (int)base.g + (int)add.g);
  base.b = (uint8_t)min(255, (int)base.b + (int)add.b);
}

// Per-channel max blend (good for overlays that should "poke through"
// without darkening anything the base effect already lit brightly).
inline void maxChannel(RGB8 &base, const RGB8 &other) {
  base.r = max(base.r, other.r);
  base.g = max(base.g, other.g);
  base.b = max(base.b, other.b);
}

// Scales all channels by `factor` (can exceed 1.0), clamped to [0,255].
inline void scaleClamp(RGB8 &c, float factor) {
  c.r = (uint8_t)constrain((int)(c.r * factor + 0.5f), 0, 255);
  c.g = (uint8_t)constrain((int)(c.g * factor + 0.5f), 0, 255);
  c.b = (uint8_t)constrain((int)(c.b * factor + 0.5f), 0, 255);
}

inline RGB8 lerpRGB(const RGB8 &a, const RGB8 &b, float t) {
  t = constrain(t, 0.0f, 1.0f);
  return RGB8{
      (uint8_t)(a.r + (b.r - a.r) * t),
      (uint8_t)(a.g + (b.g - a.g) * t),
      (uint8_t)(a.b + (b.b - a.b) * t),
  };
}

// Interpolates across a small palette of color stops spread evenly over
// the 0..255 position range. `stops` must have at least 2 entries.
RGB8 paletteLookup(const RGB8 *stops, uint8_t numStops, uint8_t position);

// Fast sine wave, 0..255 in, 0..255 out (midline 127.5), backed by a
// lookup table built once in initLedEffects() -- avoids repeated sinf()
// calls in per-LED inner loops. Good for continuous oscillation
// (breathing, moving waves) where the midline offset doesn't matter.
uint8_t sine8(uint8_t angle);

// Envelope "hump" shape: 0 at phase==0, peak (255) at phase==~128, back to
// 0 at phase==255. Good for rise-and-decay shapes (sparkle/firefly/ripple
// lifetimes) where a true zero baseline matters, unlike sine8's midline.
uint8_t hump8(uint8_t phase);

// Full-saturation, full-value hue wheel (0..255 hue in): exactly one
// channel is 0 and the other two sum to 255 at every point, so adjacent
// hues never wash out toward white the way naive RGB blending can. Used
// by RAINBOW_FLOW and shared with the audio-reactive palette/overlays.
RGB8 hueToRGB(uint8_t hue);
