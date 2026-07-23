#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 4
#define NUM_LEDS 58
#define BRIGHTNESS 15

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void printStage(const char *name) {
  Serial.print(F("[STAGE] "));
  Serial.println(name);
}

void fillForward(uint32_t color, const char *label, uint16_t stepDelayMs) {
  printStage(label);
  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, color);
    strip.show();
    delay(stepDelayMs);
  }
}

void fillBackward(uint32_t color, const char *label, uint16_t stepDelayMs) {
  printStage(label);
  strip.clear();
  for (int i = NUM_LEDS - 1; i >= 0; i--) {
    strip.setPixelColor(i, color);
    strip.show();
    delay(stepDelayMs);
  }
}

void setup() {
  Serial.begin(115200);

  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) {
    delay(10);
  }
  delay(500); // let host-side terminal attach

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("   WS2812B 58-LED CHAIN TEST"));
  Serial.println(F("========================================"));
  Serial.printf("LED_PIN:    %d\n", LED_PIN);
  Serial.printf("NUM_LEDS:   %d\n", NUM_LEDS);
  Serial.printf("Brightness: %d\n", BRIGHTNESS);
  Serial.println(F("----------------------------------------"));

  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
  printStage("All LEDs OFF");
  delay(300);

  printStage("LED 0 RED");
  strip.clear();
  strip.setPixelColor(0, strip.Color(255, 0, 0));
  strip.show();
  delay(1000);

  printStage("LED 57 GREEN");
  strip.clear();
  strip.setPixelColor(NUM_LEDS - 1, strip.Color(0, 255, 0));
  strip.show();
  delay(1000);

  printStage("Dim white pixel chase (0 -> 57)");
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.clear();
    strip.setPixelColor(i, strip.Color(255, 255, 255)); // dimmed by global BRIGHTNESS
    strip.show();
    delay(30);
  }
  strip.clear();
  strip.show();

  fillForward(strip.Color(255, 0, 0), "Red fill (0 -> 57)", 20);
  fillBackward(strip.Color(0, 255, 0), "Green fill (57 -> 0)", 20);

  printStage("Dim solid BLUE");
  strip.fill(strip.Color(0, 0, 255));
  strip.show();
  delay(2000);

  printStage("Slow rainbow animation (forever)");
}

void loop() {
  static uint16_t hue = 0;
  strip.rainbow(hue, 1, 255, 255, true);
  strip.show();
  hue += 200;
  delay(40);
}
