// BEGIN HARDWARE TEST
// See include/HardwareTest.h for scope and removal instructions.
#include "HardwareTest.h"

#include <Arduino.h>

#include "AudioAnalyzer.h"
#include "Config.h"

namespace {

void showSolid(Adafruit_NeoPixel &strip, uint8_t r, uint8_t g, uint8_t b, const char *label) {
  Serial.printf("[HWTEST] LED: %s\n", label);
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, strip.Color(r, g, b));
  strip.show();
  delay(1000);
}

void showRainbow(Adafruit_NeoPixel &strip) {
  Serial.println(F("[HWTEST] LED: RAINBOW"));
  unsigned long start = millis();
  while (millis() - start < 1000) {
    uint16_t hueBase = (uint16_t)(((millis() - start) * 65536UL) / 1000UL);
    for (int i = 0; i < NUM_LEDS; i++) {
      uint16_t hue = hueBase + (uint32_t)(i * 65536UL) / NUM_LEDS;
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
    }
    strip.show();
    delay(20);
  }
}

}  // namespace

void runHardwareTestSequence(Adafruit_NeoPixel &strip) {
  Serial.println(F("[HWTEST] ==================================="));
  Serial.println(F("[HWTEST] Hardware verification sequence start"));
  Serial.printf("[HWTEST] LED count configured: %d (expected 58)\n", NUM_LEDS);

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
