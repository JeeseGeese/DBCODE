#include "AutoShowcase.h"
#include "Config.h"

static uint8_t currentIndex = 0;
static unsigned long effectStartMs = 0;

static bool transitioning = false;
static unsigned long transitionStartMs = 0;
static uint8_t nextIndex = 0;

// Dual render buffers for a true crossfade (outgoing effect rendered into
// one, incoming into the other, blended per-pixel) -- fixed-size, no heap,
// negligible RAM cost (2 * NUM_LEDS * 3 bytes).
static RGB8 bufA[NUM_LEDS];
static RGB8 bufB[NUM_LEDS];

static inline BaseEffect effectAt(uint8_t idx) { return (BaseEffect)(idx % NUM_REAL_BASE_EFFECTS); }

void startAutoShowcase(unsigned long now) {
  currentIndex = 0;
  effectStartMs = now;
  transitioning = false;
  resetBaseEffectState(effectAt(currentIndex), now);
}

void stopAutoShowcase() { transitioning = false; }

static void beginTransitionToNext(unsigned long now) {
  nextIndex = (currentIndex + 1) % NUM_REAL_BASE_EFFECTS;
  resetBaseEffectState(effectAt(nextIndex), now);
  transitioning = true;
  transitionStartMs = now;
}

void updateAutoShowcase(unsigned long now) {
  if (!transitioning) {
    if (now - effectStartMs >= AUTO_SHOWCASE_EFFECT_DURATION_MS) beginTransitionToNext(now);
    return;
  }
  if (now - transitionStartMs >= AUTO_SHOWCASE_TRANSITION_MS) {
    currentIndex = nextIndex;
    effectStartMs = now;
    transitioning = false;
  }
}

void renderAutoShowcase(RGB8 *buf, unsigned long now) {
  if (!transitioning) {
    renderBaseEffect(effectAt(currentIndex), buf, now);
    return;
  }
  renderBaseEffect(effectAt(currentIndex), bufA, now);
  renderBaseEffect(effectAt(nextIndex), bufB, now);
  float t = constrain((float)(now - transitionStartMs) / (float)AUTO_SHOWCASE_TRANSITION_MS, 0.0f, 1.0f);
  for (int i = 0; i < NUM_LEDS; i++) buf[i] = lerpRGB(bufA[i], bufB[i], t);
}

void autoShowcaseForceNext(unsigned long now) {
  if (!transitioning) beginTransitionToNext(now);
}

BaseEffect getAutoShowcaseCurrentEffect() { return effectAt(currentIndex); }

uint32_t getAutoShowcaseMsRemaining(unsigned long now) {
  if (transitioning) return 0;
  unsigned long elapsed = now - effectStartMs;
  if (elapsed >= AUTO_SHOWCASE_EFFECT_DURATION_MS) return 0;
  return (uint32_t)(AUTO_SHOWCASE_EFFECT_DURATION_MS - elapsed);
}
