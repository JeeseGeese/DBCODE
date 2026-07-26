#include "AudioOverlays.h"
#include "AudioPalette.h"
#include <climits>
#include <math.h>

const char *AUDIO_OVERLAY_NAMES[NUM_AUDIO_OVERLAYS] = {
    "OFF",       "PULSE",        "RIPPLE",      "SPARK",
    "LIGHTNING", "BASS_BLOOM",   "SPECTRUM_WAVE", "COLOR_FLOOD",
    "COMET_BURST",
};

static inline uint8_t phaseFromPeriodMs(unsigned long now, uint32_t periodMs) {
  return (uint8_t)(((now % periodMs) * 256UL) / periodMs);
}

// ============================================================================
// PULSE -- full-strip breathing/traveling energy pulse. A dim moving
// background gradient at rest; a colored pulse expands outward from
// center as level rises; bass thickens it and warms the palette;
// transient energy adds bright leading-edge accents; claps launch a
// separate brief full-range impact wave rather than a solid flash.
// ============================================================================
struct PulseClapWave {
  bool active;
  unsigned long startMs;
};
static PulseClapWave pulseClapWave;

static void resetPulse() { pulseClapWave = PulseClapWave{}; }

static void applyPulse(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  float level = v.level;
  float bass = v.bass;
  float transient = v.highRange;

  float center = (NUM_LEDS - 1) / 2.0f;
  float width = (PULSE_MIN_WIDTH_LEDS + level * (NUM_LEDS * 0.45f)) * (1.0f + bass * PULSE_BASS_WIDTH_GAIN);
  float travel = level * center;
  float halfWidth = width * 0.5f;

  uint8_t bgPhase = phaseFromPeriodMs(now, 6000);
  RGB8 pulseColor = audioEnergyColor(level, bass, transient);
  RGB8 bgColor = audioEnergyColor(0.12f + bass * 0.1f, bass, 0.0f);

  if (v.clap) {
    pulseClapWave.active = true;
    pulseClapWave.startMs = now;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t wavePhase = (uint8_t)(bgPhase + i * 5);
    float bgLevel = 0.35f + (sine8(wavePhase) / 255.0f) * 0.35f;
    RGB8 c = scaledColor(bgColor, bgLevel);

    float dist = fabsf(i - center);
    float ringDist = fabsf(dist - travel);
    if (ringDist < halfWidth) {
      float within = 1.0f - ringDist / halfWidth;
      addClamp(c, scaledColor(pulseColor, within));
      if (transient > 0.2f && within > 0.6f) {
        RGB8 edge = blendAudioColors(pulseColor, AUDIO_ACCENT_WHITE, transient * 0.5f);
        addClamp(c, scaledColor(edge, within * transient * 0.7f));
      }
    }
    buf[i] = c;
  }

  if (pulseClapWave.active) {
    unsigned long age = now - pulseClapWave.startMs;
    if (age >= PULSE_CLAP_WAVE_LIFETIME_MS) {
      pulseClapWave.active = false;
    } else {
      float radius = (age / 1000.0f) * PULSE_CLAP_WAVE_SPEED_LEDS_PER_SEC;
      uint8_t phase = (uint8_t)((age * 255UL) / PULSE_CLAP_WAVE_LIFETIME_MS);
      float env = hump8(phase) / 255.0f;
      float halfW = PULSE_CLAP_WAVE_WIDTH_LEDS * 0.5f;
      RGB8 waveColor = blendAudioColors(pulseColor, AUDIO_ACCENT_WHITE, 0.6f);
      for (int i = 0; i < NUM_LEDS; i++) {
        float d = fabsf(fabsf(i - center) - radius);
        if (d >= halfW) continue;
        addClamp(buf[i], scaledColor(waveColor, env * (1.0f - d / halfW)));
      }
    }
  }
}

// ============================================================================
// RIPPLE -- multiple expanding rings, originating from the center or
// either end (alternating), with fading tails. Bass -> wider/slower;
// transients -> narrower/faster; claps launch a pair of high-energy
// ripples from both ends simultaneously. Colors evolve with intensity.
// ============================================================================
struct AudioRipple {
  bool active;
  float position;
  float speed;
  float width;
  float intensity;
  RGB8 color;
  unsigned long startMs;
  uint32_t lifetimeMs;
};
static AudioRipple ripples[RIPPLE_MAX_COUNT];

static void resetRipple() {
  for (auto &r : ripples) r.active = false;
}

uint8_t getActiveRippleCount() {
  uint8_t n = 0;
  for (auto &r : ripples) if (r.active) n++;
  return n;
}

static void spawnRipple(unsigned long now, float intensity, float speedScale, float widthScale, float origin,
                         const AudioVisualState &v) {
  int slot = -1;
  unsigned long oldest = ULONG_MAX;
  for (int i = 0; i < RIPPLE_MAX_COUNT; i++) {
    if (!ripples[i].active) { slot = i; break; }
    if (ripples[i].startMs < oldest) { oldest = ripples[i].startMs; slot = i; }
  }
  AudioRipple &r = ripples[slot];
  r.active = true;
  r.position = origin;
  r.speed = RIPPLE_SPEED_LEDS_PER_SEC * speedScale;
  r.width = RIPPLE_WIDTH_LEDS * widthScale;
  r.intensity = constrain(intensity, 0.0f, 1.0f);
  r.color = audioEnergyColor(r.intensity, v.bass, v.highRange);
  r.startMs = now;
  r.lifetimeMs = RIPPLE_LIFETIME_MS;
}

static uint8_t rippleOriginCounter = 0;
static float nextRippleOrigin() {
  uint8_t which = rippleOriginCounter % 3;
  rippleOriginCounter++;
  if (which == 0) return (NUM_LEDS - 1) / 2.0f;
  return (which == 1) ? 0.0f : (float)(NUM_LEDS - 1);
}

static void applyRipple(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  if (v.clap) {
    spawnRipple(now, RIPPLE_CLAP_INTENSITY, RIPPLE_TRANSIENT_SPEED_SCALE, RIPPLE_BASS_WIDTH_SCALE, 0.0f, v);
    spawnRipple(now, RIPPLE_CLAP_INTENSITY, RIPPLE_TRANSIENT_SPEED_SCALE, RIPPLE_BASS_WIDTH_SCALE,
                (float)(NUM_LEDS - 1), v);
  } else if (v.highRange > 0.5f) {
    float speedScale = 1.0f + (RIPPLE_TRANSIENT_SPEED_SCALE - 1.0f) * v.highRange;
    spawnRipple(now, RIPPLE_SPEECH_INTENSITY + 0.3f * v.highRange, speedScale, 0.8f, nextRippleOrigin(), v);
  } else if (v.bass > 0.6f &&
             (random(0, 10000) < (long)(RIPPLE_AMBIENT_SPAWN_CHANCE_PER_FRAME * 10000.0f * v.bass))) {
    spawnRipple(now, 0.4f + 0.4f * v.bass, RIPPLE_BASS_SPEED_SCALE, RIPPLE_BASS_WIDTH_SCALE, nextRippleOrigin(), v);
  }

  for (auto &r : ripples) {
    if (!r.active) continue;
    unsigned long age = now - r.startMs;
    if (age >= r.lifetimeMs) { r.active = false; continue; }

    float radius = (age / 1000.0f) * r.speed;
    uint8_t phase = (uint8_t)((age * 255UL) / r.lifetimeMs);
    float envelope = (hump8(phase) / 255.0f) * r.intensity;
    float halfWidth = r.width / 2.0f;

    for (int i = 0; i < NUM_LEDS; i++) {
      float dist = fabsf(i - r.position);
      float ringDist = fabsf(dist - radius);
      if (ringDist >= halfWidth) continue;
      addClamp(buf[i], scaledColor(r.color, envelope * (1.0f - ringDist / halfWidth)));
    }
  }
}

// ============================================================================
// SPARK -- richer particle system: low audio = occasional dim particles,
// medium energy = more/faster particles, transients = bright directional
// sparks, bass = larger glowing embers, all with smooth tails/decay and
// audio-palette colors. Clap = a bounded multi-spark burst.
// ============================================================================
struct Spark {
  bool active;
  float position;
  float velocity; // LEDs/sec drift
  unsigned long startMs;
  uint32_t lifetimeMs;
  float spawnIntensity;
  bool ember;
  RGB8 color;
};
static Spark sparks[SPARK_MAX_COUNT];

static void resetSpark() {
  for (auto &s : sparks) s.active = false;
}

uint8_t getActiveSparkCount() {
  uint8_t n = 0;
  for (auto &s : sparks) if (s.active) n++;
  return n;
}

static Spark *freeSparkSlot() {
  for (auto &s : sparks) if (!s.active) return &s;
  return nullptr; // pool full -- drop this spawn rather than grow unbounded
}

static void spawnSpark(uint8_t index, float intensity, bool ember, const AudioVisualState &v, unsigned long now) {
  Spark *s = freeSparkSlot();
  if (!s) return;
  s->active = true;
  s->position = index;
  s->velocity = ember ? 0.0f : (random(-100, 101) / 100.0f) * 3.0f;
  s->startMs = now;
  s->lifetimeMs = ember ? SPARK_EMBER_LIFETIME_MS : SPARK_LIFETIME_MS;
  s->spawnIntensity = constrain(intensity, 0.0f, 1.0f);
  s->ember = ember;
  s->color = audioEnergyColor(s->spawnIntensity, v.bass, v.highRange);
}

static void applySpark(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  float chance = v.level * SPARK_MAX_SPAWN_CHANCE_PER_FRAME;
  if (chance > 0.0f && (random(0, 10000) < (long)(chance * 10000))) {
    bool ember = v.bass > 0.4f && (random(0, 100) < (int)(SPARK_BASS_EMBER_CHANCE * 100));
    spawnSpark((uint8_t)random(0, NUM_LEDS), v.level, ember, v, now);
  }
  if (v.highRange > 0.4f && !v.clap && random(0, 100) < (int)(v.highRange * 60)) {
    spawnSpark((uint8_t)random(0, NUM_LEDS), 0.6f + 0.4f * v.highRange, false, v, now);
  }
  if (v.clap) {
    for (int k = 0; k < SPARK_CLAP_BURST_COUNT; k++) {
      spawnSpark((uint8_t)random(0, NUM_LEDS), 1.0f, false, v, now);
    }
  }

  for (auto &s : sparks) {
    if (!s.active) continue;
    unsigned long age = now - s.startMs;
    if (age >= s.lifetimeMs) { s.active = false; continue; }

    uint8_t phase = (uint8_t)((age * 255UL) / s.lifetimeMs);
    float env = (hump8(phase) / 255.0f) * (0.5f + 0.5f * s.spawnIntensity);
    float pos = s.position + s.velocity * (age / 1000.0f);
    int idx = (int)constrain(pos, 0.0f, (float)(NUM_LEDS - 1));

    maxChannel(buf[idx], scaledColor(s.color, env));

    int tailIdx = idx - (s.velocity >= 0 ? 1 : -1);
    if (tailIdx >= 0 && tailIdx < NUM_LEDS) maxChannel(buf[tailIdx], scaledColor(s.color, env * 0.35f));

    if (s.ember) {
      for (int d = -1; d <= 1; d += 2) {
        int gIdx = idx + d;
        if (gIdx < 0 || gIdx >= NUM_LEDS) continue;
        maxChannel(buf[gIdx], scaledColor(s.color, env * 0.5f));
      }
    }
  }
}

// ============================================================================
// LIGHTNING -- branching bolt segments (fixed pool) across different strip
// regions with a pale core and colored glow, instead of a single full-strip
// flash. transientStrength controls branch count/length; sustained bass
// adds a low storm glow between strikes; cooldown-gated; capped intensity.
// ============================================================================
struct LightningBolt {
  bool active;
  uint8_t origin;
  int8_t direction;
  uint8_t length;
};
static LightningBolt bolts[LIGHTNING_MAX_BOLTS];
static bool lightningFlashing = false;
static unsigned long lightningFlashStart = 0;
static unsigned long lightningLastTrigger = 0;

static void resetLightning() {
  lightningFlashing = false;
  for (auto &b : bolts) b.active = false;
}

static void applyLightning(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  bool strongEvent = v.clap || (v.transientStrength > AUDIO_TRANSIENT_RISE_THRESHOLD * 1.5f);
  if (strongEvent && !lightningFlashing && now - lightningLastTrigger > AUDIO_LIGHTNING_COOLDOWN_MS) {
    lightningFlashing = true;
    lightningFlashStart = now;
    lightningLastTrigger = now;

    uint8_t branchCount = v.clap ? LIGHTNING_MAX_BOLTS
                                  : (uint8_t)constrain(1 + (int)(v.transientStrength / AUDIO_TRANSIENT_RISE_THRESHOLD),
                                                        1, (int)LIGHTNING_MAX_BOLTS);
    uint8_t lengthBase = (uint8_t)constrain(8 + v.transientStrength * 6.0f, 6.0f, (float)(NUM_LEDS / 2));
    for (uint8_t k = 0; k < LIGHTNING_MAX_BOLTS; k++) {
      if (k >= branchCount) { bolts[k].active = false; continue; }
      bolts[k].active = true;
      bolts[k].origin = (uint8_t)random(0, NUM_LEDS);
      bolts[k].direction = (random(0, 2) == 0) ? 1 : -1;
      bolts[k].length = (uint8_t)constrain((int)lengthBase + random(-4, 5), 4, NUM_LEDS);
    }
  }

  // Sustained bass storm glow, independent of an active strike.
  if (v.bass > 0.3f) {
    RGB8 glow = blendAudioColors(RGB8{20, 20, 60}, RGB8{80, 60, 160}, v.bass);
    RGB8 scaledGlow = scaledColor(glow, v.bass * LIGHTNING_STORM_GLOW_GAIN);
    for (int i = 0; i < NUM_LEDS; i++) addClamp(buf[i], scaledGlow);
  }

  if (!lightningFlashing) return;

  unsigned long elapsed = now - lightningFlashStart;
  unsigned long subframe = elapsed / LIGHTNING_BOLT_SUBFRAME_MS;
  if (subframe >= LIGHTNING_FLASH_FRAMES) {
    lightningFlashing = false;
    for (auto &b : bolts) b.active = false;
    return;
  }

  float intensity = (subframe % 2 == 0) ? LIGHTNING_MAX_INTENSITY : LIGHTNING_MAX_INTENSITY * 0.3f;
  RGB8 core = blendAudioColors(RGB8{160, 180, 255}, AUDIO_ACCENT_WHITE, 0.6f);

  for (auto &b : bolts) {
    if (!b.active) continue;
    RGB8 glowColor = (b.origin % 2 == 0) ? RGB8{80, 100, 255} : RGB8{180, 80, 255};
    for (uint8_t s = 0; s < b.length; s++) {
      int idx = (int)b.origin + (int)b.direction * (int)s;
      if (idx < 0 || idx >= NUM_LEDS) break;
      float coreFalloff = 1.0f - ((float)s / (float)b.length) * 0.5f;
      addClamp(buf[idx], scaledColor(core, intensity * coreFalloff));
      for (int d = -1; d <= 1; d += 2) {
        int gIdx = idx + d;
        if (gIdx < 0 || gIdx >= NUM_LEDS) continue;
        addClamp(buf[gIdx], scaledColor(glowColor, intensity * coreFalloff * 0.4f));
      }
    }
  }
}

// ============================================================================
// BASS_BLOOM -- large-scale expanding bloom from the center (plus a
// mirrored secondary anchor once bass is strong), with a complementary
// outer color ring, a transient-driven bright outline, a smoothly
// decaying color trail, and a subtle atmospheric background at rest.
// ============================================================================
static RGB8 bloomTrail[NUM_LEDS];
static unsigned long bloomLastMs = 0;

static void resetBassBloom() {
  for (auto &c : bloomTrail) c = RGB8{0, 0, 0};
  bloomLastMs = 0;
}

static void applyBassBloom(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  float center = (NUM_LEDS - 1) / 2.0f;
  float radius = v.bass * center * BASS_BLOOM_GAIN;
  RGB8 bloomColor = audioEnergyColor(0.3f + v.bass * 0.5f, v.bass, v.highRange);

  bool mirror = v.bass > BASS_BLOOM_MIRROR_THRESHOLD;
  float anchorA = NUM_LEDS * 0.2f;
  float anchorB = NUM_LEDS * 0.8f;
  float mirrorRadius = radius * 0.5f;

  float dt = bloomLastMs ? (now - bloomLastMs) / 1000.0f : 0.02f;
  bloomLastMs = now;
  float blend = constrain(BASS_BLOOM_DECAY_PER_SEC * dt, 0.0f, 1.0f);

  for (int i = 0; i < NUM_LEDS; i++) {
    float level = 0.0f;
    if (radius > 0.0f) {
      float dist = fabsf(i - center);
      if (dist < radius) level = max(level, 1.0f - dist / radius);
      if (mirror && mirrorRadius > 0.0f) {
        float distA = fabsf(i - anchorA);
        float distB = fabsf(i - anchorB);
        if (distA < mirrorRadius) level = max(level, (1.0f - distA / mirrorRadius) * 0.8f);
        if (distB < mirrorRadius) level = max(level, (1.0f - distB / mirrorRadius) * 0.8f);
      }
    }

    RGB8 target = scaledColor(bloomColor, level * v.bass);

    if (v.level > 0.15f) {
      float dist = fabsf(i - center);
      float outerBand = radius + 3.0f;
      if (dist >= radius && dist < outerBand) {
        RGB8 outer = audioEnergyColor(0.5f, v.bass * 0.3f, 0.0f);
        addClamp(target, scaledColor(outer, (1.0f - (dist - radius) / 3.0f) * v.level * 0.4f));
      }
    }

    if (v.highRange > 0.15f) {
      float dist = fabsf(i - center);
      float edgeDist = fabsf(dist - radius);
      if (edgeDist < 1.5f) {
        addClamp(target,
                  scaledColor(AUDIO_ACCENT_WHITE, v.highRange * BASS_BLOOM_OUTLINE_TRANSIENT_GAIN * (1.0f - edgeDist / 1.5f)));
      }
    }

    addClamp(target, RGB8{10, 6, 20}); // subtle atmospheric background, always present

    RGB8 finalColor = blendAudioColors(bloomTrail[i], target, blend);
    bloomTrail[i] = finalColor;
    buf[i] = finalColor;
  }
}

// ============================================================================
// SPECTRUM_WAVE -- a multicolor hue wave traveling across the full strip.
// Level -> amplitude, bass -> thickness/speed, transients/claps can
// reverse direction, loud peaks produce bright crests.
// ============================================================================
static float spectrumHueDeg = 0.0f;
static float spectrumDir = 1.0f;
static unsigned long spectrumLastMs = 0;

static void resetSpectrumWave() {
  spectrumHueDeg = 0.0f;
  spectrumDir = 1.0f;
  spectrumLastMs = 0;
}

static void applySpectrumWave(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  float dt = spectrumLastMs ? (now - spectrumLastMs) / 1000.0f : 0.02f;
  spectrumLastMs = now;

  if (v.clap) spectrumDir = -spectrumDir;

  float speed = SPECTRUM_WAVE_HUE_SPEED_DEG_PER_SEC * (1.0f + v.bass);
  spectrumHueDeg += speed * dt * spectrumDir;
  if (spectrumHueDeg > 360.0f) spectrumHueDeg -= 360.0f;
  if (spectrumHueDeg < 0.0f) spectrumHueDeg += 360.0f;

  float amplitude = SPECTRUM_WAVE_BASE_AMPLITUDE + v.level * SPECTRUM_WAVE_LEVEL_AMPLITUDE_GAIN;
  float thickness = 1.0f + v.bass * 2.0f;

  for (int i = 0; i < NUM_LEDS; i++) {
    float posDeg = (i * 360.0f / NUM_LEDS) / thickness;
    uint8_t hue = (uint8_t)(fmodf((spectrumHueDeg + posDeg), 360.0f) / 360.0f * 255.0f);
    RGB8 c = hueToRGB(hue);

    uint8_t wavePhase = (uint8_t)((int)(spectrumHueDeg) + i * (256 / NUM_LEDS));
    uint8_t waveSample = sine8(wavePhase);
    float brightness = constrain(0.3f + (waveSample / 255.0f) * amplitude, 0.05f, 1.0f);
    c = scaledColor(c, brightness);

    if (v.level > 0.7f && waveSample > 220) {
      c = blendAudioColors(c, AUDIO_ACCENT_WHITE, (v.level - 0.7f) / 0.3f * 0.4f);
    }
    buf[i] = c;
  }
}

// ============================================================================
// COLOR_FLOOD -- layered fill: bass fills from the left, the derived mid
// control fills from the right, transient/high control sparkles the
// leading edges. A clap briefly floods the whole range with a multicolor
// impact before fading.
// ============================================================================
static bool colorFloodClapActive = false;
static unsigned long colorFloodClapStart = 0;

static void resetColorFlood() { colorFloodClapActive = false; }

static void applyColorFlood(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  if (v.clap) {
    colorFloodClapActive = true;
    colorFloodClapStart = now;
  }

  float bassFill = constrain(v.bass * 1.3f, 0.0f, 1.0f) * NUM_LEDS;
  float midFill = constrain(v.midRange * 1.3f, 0.0f, 1.0f) * NUM_LEDS;

  RGB8 bassColor = audioEnergyColor(0.25f + v.bass * 0.4f, v.bass, 0.0f);
  RGB8 midColor = audioEnergyColor(0.55f + v.midRange * 0.3f, 0.0f, 0.0f);

  for (int i = 0; i < NUM_LEDS; i++) {
    RGB8 c = {0, 0, 0};
    if (i < (int)bassFill) {
      float edge = constrain(bassFill - i, 0.0f, 1.0f);
      c = scaledColor(bassColor, 0.5f + 0.5f * edge);
    }
    int fromRight = NUM_LEDS - 1 - i;
    if (fromRight < (int)midFill) {
      float edge = constrain(midFill - fromRight, 0.0f, 1.0f);
      addClamp(c, scaledColor(midColor, 0.5f + 0.5f * edge));
    }
    if (v.highRange > 0.2f) {
      bool nearBassEdge = fabsf(i - bassFill) < 2.0f;
      bool nearMidEdge = fabsf(fromRight - midFill) < 2.0f;
      if ((nearBassEdge || nearMidEdge) && random(0, 100) < (int)(v.highRange * 40)) {
        c = blendAudioColors(c, AUDIO_ACCENT_WHITE, 0.5f);
      }
    }
    buf[i] = c;
  }

  if (colorFloodClapActive) {
    unsigned long age = now - colorFloodClapStart;
    if (age >= COLOR_FLOOD_CLAP_FADE_MS) {
      colorFloodClapActive = false;
    } else {
      uint8_t phase = (uint8_t)((age * 255UL) / COLOR_FLOOD_CLAP_FADE_MS);
      float env = hump8(phase) / 255.0f;
      for (int i = 0; i < NUM_LEDS; i++) {
        RGB8 impact = audioEnergyColor((float)i / NUM_LEDS, 0.5f, 1.0f);
        addClamp(buf[i], scaledColor(impact, env * 0.8f));
      }
    }
  }
}

// ============================================================================
// COMET_BURST -- directional comets with fading tails, launched from
// either end. Normal peaks launch mid-speed comets, bass launches
// slower/wider ones, transients launch fast/narrow ones, and a clap
// launches a pair of opposing comets that cross the strip.
// ============================================================================
struct Comet {
  bool active;
  float position;
  float speed;
  unsigned long startMs;
  uint32_t lifetimeMs;
  RGB8 color;
};
static Comet comets[COMET_MAX_COUNT];
static unsigned long lastCometSpawnCheck = 0;

static void resetCometBurst() {
  for (auto &c : comets) c.active = false;
}

uint8_t getActiveCometCount() {
  uint8_t n = 0;
  for (auto &c : comets) if (c.active) n++;
  return n;
}

static Comet *freeCometSlot() {
  for (auto &c : comets) if (!c.active) return &c;
  return nullptr;
}

static void launchComet(bool fromLeft, float speed, float intensity, const AudioVisualState &v, unsigned long now) {
  Comet *c = freeCometSlot();
  if (!c) return;
  c->active = true;
  c->position = fromLeft ? 0.0f : (float)(NUM_LEDS - 1);
  c->speed = fromLeft ? speed : -speed;
  c->startMs = now;
  c->lifetimeMs = (uint32_t)((NUM_LEDS / fabsf(speed)) * 1000.0f) + 300;
  c->color = audioEnergyColor(constrain(intensity, 0.0f, 1.0f), v.bass, v.highRange);
}

static void applyCometBurst(RGB8 *buf, const AudioVisualState &v, unsigned long now) {
  if (v.clap) {
    launchComet(true, COMET_BASE_SPEED_LEDS_PER_SEC * COMET_TRANSIENT_SPEED_SCALE, 1.0f, v, now);
    launchComet(false, COMET_BASE_SPEED_LEDS_PER_SEC * COMET_TRANSIENT_SPEED_SCALE, 1.0f, v, now);
  } else if (v.highRange > 0.45f && now - lastCometSpawnCheck > 120) {
    lastCometSpawnCheck = now;
    if (random(0, 100) < (int)(v.highRange * 50)) {
      launchComet(random(0, 2) == 0, COMET_BASE_SPEED_LEDS_PER_SEC * COMET_TRANSIENT_SPEED_SCALE,
                  0.6f + 0.4f * v.highRange, v, now);
    }
  } else if (v.bass > 0.5f && now - lastCometSpawnCheck > 250) {
    lastCometSpawnCheck = now;
    if (random(0, 100) < (int)(v.bass * 30)) {
      launchComet(random(0, 2) == 0, COMET_BASE_SPEED_LEDS_PER_SEC * COMET_BASS_SPEED_SCALE, 0.3f + 0.3f * v.bass, v,
                  now);
    }
  }

  for (auto &c : comets) {
    if (!c.active) continue;
    unsigned long age = now - c.startMs;
    if (age >= c.lifetimeMs) { c.active = false; continue; }

    float pos = c.position + c.speed * (age / 1000.0f);
    if (pos < -COMET_TAIL_LEDS || pos > (float)(NUM_LEDS - 1) + COMET_TAIL_LEDS) { c.active = false; continue; }

    for (float t = 0.0f; t <= COMET_TAIL_LEDS; t += 1.0f) {
      float tailPos = pos - ((c.speed > 0) ? t : -t);
      int idx = (int)roundf(tailPos);
      if (idx < 0 || idx >= NUM_LEDS) continue;
      float tailFalloff = 1.0f - t / COMET_TAIL_LEDS;
      maxChannel(buf[idx], scaledColor(c.color, tailFalloff * tailFalloff));
    }
  }
}

// ============================================================================
// Dispatch
// ============================================================================
void resetAudioOverlayState(AudioOverlay overlay, unsigned long now) {
  (void)now;
  switch (overlay) {
    case AudioOverlay::PULSE: resetPulse(); break;
    case AudioOverlay::RIPPLE: resetRipple(); break;
    case AudioOverlay::SPARK: resetSpark(); break;
    case AudioOverlay::LIGHTNING: resetLightning(); break;
    case AudioOverlay::BASS_BLOOM: resetBassBloom(); break;
    case AudioOverlay::SPECTRUM_WAVE: resetSpectrumWave(); break;
    case AudioOverlay::COLOR_FLOOD: resetColorFlood(); break;
    case AudioOverlay::COMET_BURST: resetCometBurst(); break;
    default: break;
  }
}

void applyAudioOverlay(AudioOverlay overlay, RGB8 *buf, const AudioVisualState &audio, unsigned long now) {
  switch (overlay) {
    case AudioOverlay::OFF: return;
    case AudioOverlay::PULSE: applyPulse(buf, audio, now); break;
    case AudioOverlay::RIPPLE: applyRipple(buf, audio, now); break;
    case AudioOverlay::SPARK: applySpark(buf, audio, now); break;
    case AudioOverlay::LIGHTNING: applyLightning(buf, audio, now); break;
    case AudioOverlay::BASS_BLOOM: applyBassBloom(buf, audio, now); break;
    case AudioOverlay::SPECTRUM_WAVE: applySpectrumWave(buf, audio, now); break;
    case AudioOverlay::COLOR_FLOOD: applyColorFlood(buf, audio, now); break;
    case AudioOverlay::COMET_BURST: applyCometBurst(buf, audio, now); break;
    default: break;
  }
}
