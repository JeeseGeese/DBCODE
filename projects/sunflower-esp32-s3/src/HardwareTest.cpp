// BEGIN HARDWARE TEST
// See include/HardwareTest.h for scope and removal instructions.
#include "HardwareTest.h"

#include <Arduino.h>

#include "AudioAnalyzer.h"
#include "Config.h"
#include "LedEffects.h"  // RGB8, scaleClamp()

// applyPowerLimit() is defined (not `static`) in main.cpp -- see that
// file's comment on why this is deliberately shared rather than
// duplicated. Fixed 2026-08-08 (Sunny V1.1 LED power-safety audit): HWTEST
// previously wrote raw, unscaled full-255-per-channel frames directly via
// strip.setPixelColor()/show(), bypassing BOTH normal brightness scaling
// and this power estimator entirely -- a genuine unprotected high-current
// SOLID WHITE frame at boot, coincident with at least one observed
// brownout. This does not by itself prove LED current caused that
// brownout (see docs/current/POWER.md -- root cause remains open), but the
// bypass itself was a real gap worth closing regardless.
void applyPowerLimit(RGB8 *buf);

namespace {

// Builds a flat-color frame in a local buffer, runs it through the SAME
// applyPowerLimit() the normal render loop uses, then writes the (possibly
// scaled-down) result to the strip -- same safe path, not a second one.
void showSolid(Adafruit_NeoPixel &strip, uint8_t r, uint8_t g, uint8_t b, const char *label) {
  Serial.printf("[HWTEST] LED: %s (requested %u,%u,%u)\n", label, (unsigned)r, (unsigned)g, (unsigned)b);
  RGB8 buf[NUM_LEDS];
  for (int i = 0; i < NUM_LEDS; i++) buf[i] = RGB8{r, g, b};
  applyPowerLimit(buf);  // prints its own "[POWER] Throttling: ..." line if this frame needed scaling
  Serial.printf("[HWTEST] LED: %s (actual %u,%u,%u after power limiter)\n", label, (unsigned)buf[0].r,
                (unsigned)buf[0].g, (unsigned)buf[0].b);
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(buf[i].r, buf[i].g, buf[i].b));
  strip.show();
  delay(1000);
}

void showRainbow(Adafruit_NeoPixel &strip) {
  Serial.println(F("[HWTEST] LED: RAINBOW (power-limited, same path as solid colors)"));
  unsigned long start = millis();
  RGB8 buf[NUM_LEDS];
  while (millis() - start < 1000) {
    uint16_t hueBase = (uint16_t)(((millis() - start) * 65536UL) / 1000UL);
    for (int i = 0; i < NUM_LEDS; i++) {
      uint16_t hue = hueBase + (uint32_t)(i * 65536UL) / NUM_LEDS;
      uint32_t packed = strip.gamma32(strip.ColorHSV(hue));
      buf[i] = RGB8{(uint8_t)(packed >> 16), (uint8_t)(packed >> 8), (uint8_t)packed};
    }
    applyPowerLimit(buf);
    for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(buf[i].r, buf[i].g, buf[i].b));
    strip.show();
    delay(20);
  }
}

}  // namespace

void runHardwareTestSequence(Adafruit_NeoPixel &strip) {
  Serial.println(F("[HWTEST] ==================================="));
  Serial.println(F("[HWTEST] Hardware verification sequence start"));
  Serial.printf("[HWTEST] LED count configured: %d (expected 36)\n", NUM_LEDS);
  // Explicit, per the V1.1 HWTEST power-safety fix: every LED frame below
  // goes through the SAME applyPowerLimit() estimator/scaler normal
  // rendering uses (LED_IDLE_MA_PER_LED=%u, LED_MAX_MA_PER_CHANNEL=%u) --
  // no raw full-brightness frame is ever written unprotected.
  Serial.printf("[HWTEST] LED power limiter: LED_CURRENT_LIMIT_MA=%u (same estimator as normal rendering)\n",
                (unsigned)LED_CURRENT_LIMIT_MA);

  showSolid(strip, 255, 0, 0, "SOLID RED");
  showSolid(strip, 0, 255, 0, "SOLID GREEN");
  showSolid(strip, 0, 0, 255, "SOLID BLUE");
  showSolid(strip, 255, 255, 255, "SOLID WHITE");
  showRainbow(strip);

  strip.clear();
  strip.show();
  Serial.println(F("[HWTEST] LED sequence complete"));

  Serial.println(F("[HWTEST] Mic verification: speak, clap, or make noise near the mic now."));
  Serial.println(F("[HWTEST] Printing Peak / RMS / NoiseFloor for 20s..."));

  const unsigned long TEST_DURATION_MS = 20000;
  const unsigned long PRINT_INTERVAL_MS = 150;
  unsigned long testStart = millis();
  unsigned long lastPrint = 0;

  while (millis() - testStart < TEST_DURATION_MS) {
    updateAudioAnalyzer();
    unsigned long now = millis();
    if (now - lastPrint >= PRINT_INTERVAL_MS) {
      lastPrint = now;
      const AudioFeatures &f = getAudioFeatures();
      Serial.printf("[HWTEST] Peak=%.0f RMS=%.0f NoiseFloor=%.0f\n", f.peak, f.rms, getNoiseFloorEstimate());
    }
  }

  Serial.println(F("[HWTEST] Mic verification window complete"));
  Serial.println(F("[HWTEST] Hardware verification sequence end"));
  Serial.println(F("[HWTEST] ==================================="));
}
// END HARDWARE TEST
