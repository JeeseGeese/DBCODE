#include "LedEffects.h"
#include <math.h>

const char *BASE_EFFECT_NAMES[NUM_BASE_EFFECTS] = {
    "PETAL_BREATHE", "COLOR_WAVE",     "SUNSET_SPIN", "RAINBOW_FLOW",
    "SPARKLE_BLOOM", "FIREFLY_GARDEN", "AURORA",      "SOLAR_FLARE",
    "AUTO_SHOWCASE",
};

// ============================================================================
// Shared lookup tables / helpers
// ============================================================================
static uint8_t sineTable[256]; // (sin(theta)+1)/2 * 255 -- midline 127.5, for oscillators
static uint8_t humpTable[256]; // sin(theta in [0,pi]) * 255 -- true zero baseline, for envelopes

void initLedEffects() {
  for (int i = 0; i < 256; i++) {
    float theta = (i / 256.0f) * 2.0f * PI;
    sineTable[i] = (uint8_t)(((sinf(theta) + 1.0f) * 0.5f) * 255.0f);

    float halfTheta = (i / 255.0f) * PI;
    humpTable[i] = (uint8_t)(sinf(halfTheta) * 255.0f);
  }
}

uint8_t sine8(uint8_t angle) { return sineTable[angle]; }
uint8_t hump8(uint8_t phase) { return humpTable[phase]; }

RGB8 paletteLookup(const RGB8 *stops, uint8_t numStops, uint8_t position) {
  if (numStops == 1) return stops[0];
  float t = (position / 255.0f) * (numStops - 1);
  uint8_t idx = (uint8_t)t;
  if (idx >= numStops - 1) return stops[numStops - 1];
  float frac = t - idx;
  return lerpRGB(stops[idx], stops[idx + 1], frac);
}

// Maps elapsed time onto a repeating 0..255 phase without needing any
// persistent per-effect counters -- purely a function of `now`.
static inline uint8_t phaseFromPeriod(unsigned long now, uint32_t periodMs) {
  return (uint8_t)(((now % periodMs) * 256UL) / periodMs);
}

// Full-saturation, full-value hue wheel: exactly one channel is 0 and the
// other two sum to 255 at every point, so adjacent hues never wash out
// toward white the way naive RGB blending can.
RGB8 hueToRGB(uint8_t hue) {
  uint8_t region = hue / 43;
  uint8_t remainder = (hue - region * 43) * 6;
  uint8_t q = 255 - remainder;
  uint8_t t = remainder;
  switch (region) {
    case 0: return RGB8{255, t, 0};
    case 1: return RGB8{q, 255, 0};
    case 2: return RGB8{0, 255, t};
    case 3: return RGB8{0, q, 255};
    case 4: return RGB8{t, 0, 255};
    default: return RGB8{255, 0, q};
  }
}

// ============================================================================
// PETAL_BREATHE
// ============================================================================
static const RGB8 PETAL_STOPS[3] = {{255, 200, 40}, {255, 150, 20}, {255, 100, 10}};

static void renderPetalBreathe(RGB8 *buf, unsigned long now) {
  uint8_t basePhase = phaseFromPeriod(now, PETAL_BREATHE_PERIOD_MS);
  uint8_t huePhase = phaseFromPeriod(now, PETAL_BREATHE_HUE_DRIFT_PERIOD_MS);
  RGB8 color = paletteLookup(PETAL_STOPS, 3, sine8(huePhase));

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t localPhase = (uint8_t)(basePhase + i * 6); // subtle spatial spread, not a uniform value
    float level = 0.35f + (sine8(localPhase) / 255.0f) * 0.65f; // breathes 35%-100%, never fully dark
    buf[i] = color;
    scaleClamp(buf[i], level);
  }
}

// ============================================================================
// COLOR_WAVE
// ============================================================================
static const RGB8 WAVE_STOPS[4] = {{0, 80, 255}, {80, 0, 255}, {200, 0, 200}, {255, 60, 120}};

static void renderColorWave(RGB8 *buf, unsigned long now) {
  uint8_t basePhase = phaseFromPeriod(now, COLOR_WAVE_PERIOD_MS);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t pos = (uint8_t)(((i * 256) / NUM_LEDS) + basePhase);
    buf[i] = paletteLookup(WAVE_STOPS, 4, pos);
  }
}

// ============================================================================
// SUNSET_SPIN
// ============================================================================
static const RGB8 SUNSET_STOPS[4] = {{255, 90, 0}, {255, 180, 40}, {200, 40, 40}, {90, 20, 90}};

static void renderSunsetSpin(RGB8 *buf, unsigned long now) {
  uint8_t basePhase = phaseFromPeriod(now, SUNSET_SPIN_PERIOD_MS);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t pos = (uint8_t)(((i * 256) / NUM_LEDS) + basePhase);
    buf[i] = paletteLookup(SUNSET_STOPS, 4, pos);
  }
}

// ============================================================================
// RAINBOW_FLOW
// ============================================================================
static void renderRainbowFlow(RGB8 *buf, unsigned long now) {
  uint8_t huePhase = phaseFromPeriod(now, RAINBOW_FLOW_PERIOD_MS);
  uint8_t brightPhase = phaseFromPeriod(now, RAINBOW_FLOW_SECONDARY_PERIOD_MS);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t hue = (uint8_t)(huePhase + (i * 256) / NUM_LEDS);
    RGB8 c = hueToRGB(hue);
    uint8_t bwave = sine8((uint8_t)(brightPhase + i * 3)); // secondary wave, different frequency than hue motion
    float level = 0.55f + (bwave / 255.0f) * 0.45f;
    scaleClamp(c, level);
    buf[i] = c;
  }
}

// ============================================================================
// SPARKLE_BLOOM
// ============================================================================
static const RGB8 SPARKLE_BASE_COLOR = {40, 26, 10};
static const RGB8 SPARKLE_COLOR = {255, 235, 190}; // warm, not harsh pure white

static bool sparkleActive[NUM_LEDS];
static unsigned long sparkleStartMs[NUM_LEDS];

static void resetSparkleBloom() {
  memset(sparkleActive, 0, sizeof(sparkleActive));
}

static void renderSparkleBloom(RGB8 *buf, unsigned long now) {
  uint8_t basePhase = phaseFromPeriod(now, 5000);
  float baseLevel = 0.75f + (sine8(basePhase) / 255.0f) * 0.25f; // gentle animated base, not static
  for (int i = 0; i < NUM_LEDS; i++) {
    buf[i] = SPARKLE_BASE_COLOR;
    scaleClamp(buf[i], baseLevel);
  }

  if (random(0, 10000) < (long)(SPARKLE_BLOOM_SPAWN_CHANCE_PER_FRAME * 10000)) {
    int idx = random(0, NUM_LEDS);
    if (!sparkleActive[idx]) {
      sparkleActive[idx] = true;
      sparkleStartMs[idx] = now;
    }
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (!sparkleActive[i]) continue;
    unsigned long age = now - sparkleStartMs[i];
    if (age >= SPARKLE_BLOOM_LIFETIME_MS) {
      sparkleActive[i] = false;
      continue;
    }
    uint8_t phase = (uint8_t)((age * 255UL) / SPARKLE_BLOOM_LIFETIME_MS);
    float env = hump8(phase) / 255.0f; // smooth rise and decay, true zero at both ends
    RGB8 c = SPARKLE_COLOR;
    scaleClamp(c, env);
    maxChannel(buf[i], c); // sparkle pokes through the base glow without darkening it
  }
}

// ============================================================================
// FIREFLY_GARDEN
// ============================================================================
static const RGB8 FIREFLY_BASE_COLOR = {6, 8, 2}; // dark ambient base
static const RGB8 FIREFLY_COLOR_A = {180, 200, 40}; // yellow-green
static const RGB8 FIREFLY_COLOR_B = {120, 220, 90};  // green

struct Firefly {
  bool active;
  float position;
  float speed; // LEDs per second, can be negative
  unsigned long startMs;
  unsigned long lifetimeMs;
  bool useColorA;
};
static Firefly fireflies[FIREFLY_COUNT];

static void spawnFirefly(Firefly &f, unsigned long now) {
  f.active = true;
  f.position = random(0, NUM_LEDS * 10) / 10.0f;
  f.speed = (random(-150, 151)) / 100.0f; // -1.5..1.5 LEDs/sec, gentle drift
  f.startMs = now;
  f.lifetimeMs = FIREFLY_LIFETIME_MS + random(-400, 401);
  f.useColorA = random(0, 2) == 0;
}

static void resetFireflyGarden(unsigned long now) {
  for (int i = 0; i < FIREFLY_COUNT; i++) {
    fireflies[i].active = false;
    // Stagger initial spawns so they don't all fade in sync.
    fireflies[i].startMs = now - (unsigned long)random(0, FIREFLY_LIFETIME_MS);
  }
}

static void renderFireflyGarden(RGB8 *buf, unsigned long now) {
  for (int i = 0; i < NUM_LEDS; i++) buf[i] = FIREFLY_BASE_COLOR;

  for (int i = 0; i < FIREFLY_COUNT; i++) {
    Firefly &f = fireflies[i];
    if (!f.active) {
      if ((long)(now - f.startMs) >= 0) spawnFirefly(f, now);
      continue;
    }

    unsigned long age = now - f.startMs;
    if (age >= f.lifetimeMs) {
      f.active = false;
      f.startMs = now + random(200, 1500); // brief gap before it drifts back in elsewhere
      continue;
    }

    float elapsedSec = age / 1000.0f;
    float pos = f.position + f.speed * elapsedSec; // position derived from spawn point + elapsed time, not accumulated per-frame
    // Wrap smoothly within the strip.
    while (pos < 0) pos += NUM_LEDS;
    while (pos >= NUM_LEDS) pos -= NUM_LEDS;

    uint8_t phase = (uint8_t)((age * 255UL) / f.lifetimeMs);
    float env = hump8(phase) / 255.0f;

    int center = (int)pos;
    RGB8 color = f.useColorA ? FIREFLY_COLOR_A : FIREFLY_COLOR_B;
    // Gentle 1-2 LED glow radius around the firefly's position.
    for (int d = -1; d <= 1; d++) {
      int idx = center + d;
      if (idx < 0 || idx >= NUM_LEDS) continue;
      float falloff = (d == 0) ? 1.0f : 0.35f;
      RGB8 c = color;
      scaleClamp(c, env * falloff);
      maxChannel(buf[idx], c);
    }
  }
}

// ============================================================================
// AURORA
// ============================================================================
static const RGB8 AURORA_STOPS[4] = {{0, 40, 60}, {0, 150, 180}, {60, 80, 220}, {140, 60, 220}};

static void renderAurora(RGB8 *buf, unsigned long now) {
  uint8_t colorPhase = phaseFromPeriod(now, AURORA_BAND_PERIOD_MS);
  uint8_t brightPhase = phaseFromPeriod(now, (AURORA_BAND_PERIOD_MS * 3) / 2);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t pos = (uint8_t)(((i * 256) / NUM_LEDS) + colorPhase);
    RGB8 c = paletteLookup(AURORA_STOPS, 4, pos);
    uint8_t band = sine8((uint8_t)(((i * 256) / NUM_LEDS) * 2 + brightPhase));
    float level = 0.3f + (band / 255.0f) * 0.7f;
    scaleClamp(c, level);
    buf[i] = c;
  }
}

// ============================================================================
// SOLAR_FLARE
// ============================================================================
static const RGB8 FLARE_BG_STOPS[3] = {{120, 30, 0}, {200, 80, 10}, {255, 150, 30}};
static const RGB8 FLARE_COLOR = {255, 220, 160};
constexpr float SOLAR_FLARE_WIDTH = 4.0f;

static void renderSolarFlare(RGB8 *buf, unsigned long now) {
  uint8_t bgPhase = phaseFromPeriod(now, SOLAR_FLARE_FLARE_PERIOD_MS * 2);
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t pos = (uint8_t)(((i * 256) / NUM_LEDS) + bgPhase);
    buf[i] = paletteLookup(FLARE_BG_STOPS, 3, pos);
  }

  unsigned long cyclePos = now % SOLAR_FLARE_FLARE_PERIOD_MS;
  float travel = (cyclePos / 1000.0f) * SOLAR_FLARE_FLARE_SPEED_LEDS_PER_SEC;
  // Flare is only "on stage" for part of its period -- once it's traveled
  // past the strip it stays gone until the cycle restarts, making it an
  // occasional event rather than a constantly-present marker.
  if (travel > NUM_LEDS + SOLAR_FLARE_WIDTH) return;

  for (int i = 0; i < NUM_LEDS; i++) {
    float dist = fabsf(i - travel);
    if (dist >= SOLAR_FLARE_WIDTH) continue;
    float intensity = 1.0f - (dist / SOLAR_FLARE_WIDTH);
    RGB8 c = FLARE_COLOR;
    scaleClamp(c, intensity);
    addClamp(buf[i], c);
  }
}

// ============================================================================
// Dispatch
// ============================================================================
void resetBaseEffectState(BaseEffect effect, unsigned long now) {
  switch (effect) {
    case BaseEffect::SPARKLE_BLOOM: resetSparkleBloom(); break;
    case BaseEffect::FIREFLY_GARDEN: resetFireflyGarden(now); break;
    default: break; // purely time-derived effects need no persistent reset
  }
}

void renderBaseEffect(BaseEffect effect, RGB8 *buf, unsigned long now) {
  switch (effect) {
    case BaseEffect::PETAL_BREATHE: renderPetalBreathe(buf, now); break;
    case BaseEffect::COLOR_WAVE: renderColorWave(buf, now); break;
    case BaseEffect::SUNSET_SPIN: renderSunsetSpin(buf, now); break;
    case BaseEffect::RAINBOW_FLOW: renderRainbowFlow(buf, now); break;
    case BaseEffect::SPARKLE_BLOOM: renderSparkleBloom(buf, now); break;
    case BaseEffect::FIREFLY_GARDEN: renderFireflyGarden(buf, now); break;
    case BaseEffect::AURORA: renderAurora(buf, now); break;
    case BaseEffect::SOLAR_FLARE: renderSolarFlare(buf, now); break;
    default: for (int i = 0; i < NUM_LEDS; i++) buf[i] = RGB8{0, 0, 0}; break;
  }
}
