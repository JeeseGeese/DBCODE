#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 4
#define BUTTON_MODE_PIN 10
#define BUTTON_MUTE_PIN 11
#define BUTTON_BRIGHTNESS_PIN 17
#define BUTTON4_PIN 5
#define NUM_LEDS 58
#define NUM_MODES 12
#define DEBOUNCE_DELAY_MS 40

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

const char *MODE_NAMES[NUM_MODES] = {
    "Off",
    "Solid Red",
    "Solid Green",
    "Solid Blue",
    "Solid White (dim)",
    "Forward Walking Pixel",
    "Reverse Walking Pixel",
    "Rainbow",
    "Theater Chase",
    "Breathing",
    "Twinkle",
    "Larson Scanner",
};

uint8_t currentMode = 0;
bool modeChanged = true;
bool muted = false;

// Conservative brightness levels only -- never full brightness, never
// exceeds the previously-established 15-20 safe range at the top end.
const uint8_t BRIGHTNESS_LEVELS[] = {10, 15, 20};
const uint8_t NUM_BRIGHTNESS_LEVELS = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);
uint8_t brightnessIndex = 1; // starts at 15, matching the previous fixed default

// --- Button debounce state ---
// Edge-triggered on a debounce-stable HIGH->LOW transition only, so a press
// fires exactly once no matter how long the button is held, and the pin
// must return to a stable HIGH (release) before another press can register.
// Each button gets its own independent instance of this state, so four
// buttons can be read every loop iteration without interfering with each
// other's debounce timing.
struct DebouncedButton {
  uint8_t pin;
  int lastRawReading = HIGH;
  int stableState = HIGH;
  unsigned long lastDebounceTime = 0;
  explicit DebouncedButton(uint8_t p) : pin(p) {}
};

DebouncedButton modeButton{BUTTON_MODE_PIN};
DebouncedButton muteButton{BUTTON_MUTE_PIN};
DebouncedButton brightnessButton{BUTTON_BRIGHTNESS_PIN};
DebouncedButton button4{BUTTON4_PIN};

// Returns true exactly once per debounced press (stable HIGH->LOW edge).
bool buttonPressedEdge(DebouncedButton &b) {
  int reading = digitalRead(b.pin);

  if (reading != b.lastRawReading) {
    b.lastDebounceTime = millis();
  }

  bool pressed = false;
  if ((millis() - b.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) {
        pressed = true;
      }
    }
  }

  b.lastRawReading = reading;
  return pressed;
}

// --- Per-mode animation state (reset whenever the mode changes) ---
unsigned long lastStepTime = 0;
uint16_t walkPos = 0;
uint16_t rainbowHue = 0;
uint8_t chaseOffset = 0;
float breathPhase = 0.0f;
uint8_t twinkleLevel[NUM_LEDS];
uint8_t larsonLevel[NUM_LEDS];
int larsonPos = 0;
int larsonDir = 1;

void printModeAnnouncement() {
  Serial.printf("[MODE] %d - %s\n", currentMode, MODE_NAMES[currentMode]);
}

void advanceMode() {
  currentMode = (currentMode + 1) % NUM_MODES;
  modeChanged = true;
  printModeAnnouncement();
}

void toggleMute() {
  muted = !muted;
  Serial.println(F("[BUTTON] Mute pressed"));
  if (muted) {
    strip.clear();
    strip.show();
    Serial.println(F("[SYSTEM] Muted"));
  } else {
    Serial.println(F("[SYSTEM] Unmuted"));
    modeChanged = true; // force a clean re-render of the current mode
  }
}

void cycleBrightness() {
  brightnessIndex = (brightnessIndex + 1) % NUM_BRIGHTNESS_LEVELS;
  strip.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
  Serial.println(F("[BUTTON] Brightness pressed"));
  Serial.printf("[SYSTEM] Brightness: %d\n", BRIGHTNESS_LEVELS[brightnessIndex]);
  // Adafruit_NeoPixel::setBrightness() rescales the stored pixel buffer
  // in place, which can drift on repeated calls -- force a full redraw
  // from canonical mode colors instead of relying on that rescale.
  modeChanged = true;
}

void resetModeState() {
  lastStepTime = millis();
  walkPos = 0;
  rainbowHue = 0;
  chaseOffset = 0;
  breathPhase = 0.0f;
  larsonPos = 0;
  larsonDir = 1;
  memset(twinkleLevel, 0, sizeof(twinkleLevel));
  memset(larsonLevel, 0, sizeof(larsonLevel));

  // Static (non-animated) modes render once here; animated modes render
  // on their own timing inside updateAnimation().
  switch (currentMode) {
    case 0: // Off
      strip.clear();
      strip.show();
      break;
    case 1: // Solid Red
      strip.fill(strip.Color(255, 0, 0));
      strip.show();
      break;
    case 2: // Solid Green
      strip.fill(strip.Color(0, 255, 0));
      strip.show();
      break;
    case 3: // Solid Blue
      strip.fill(strip.Color(0, 0, 255));
      strip.show();
      break;
    case 4: // Solid White (dim)
      strip.fill(strip.Color(255, 255, 255)); // dimmed by global BRIGHTNESS
      strip.show();
      break;
    default:
      strip.clear();
      strip.show();
      break;
  }
}

void stepForwardWalk() {
  const unsigned long interval = 50;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.clear();
  strip.setPixelColor(walkPos, strip.Color(255, 90, 0)); // amber
  strip.show();
  walkPos = (walkPos + 1) % NUM_LEDS;
}

void stepReverseWalk() {
  const unsigned long interval = 50;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.clear();
  strip.setPixelColor(walkPos, strip.Color(255, 90, 0)); // amber
  strip.show();
  walkPos = (walkPos == 0) ? (NUM_LEDS - 1) : (walkPos - 1);
}

void stepRainbow() {
  const unsigned long interval = 40;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.rainbow(rainbowHue, 1, 255, 255, true);
  strip.show();
  rainbowHue += 200;
}

void stepTheaterChase() {
  const unsigned long interval = 100;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  strip.clear();
  for (int i = chaseOffset; i < NUM_LEDS; i += 3) {
    strip.setPixelColor(i, strip.Color(200, 0, 200)); // magenta
  }
  strip.show();
  chaseOffset = (chaseOffset + 1) % 3;
}

void stepBreathing() {
  const unsigned long interval = 20;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  // Sine-driven brightness scaling of a fixed color, magnitude only
  // (0..255 on the color channels themselves) -- global BRIGHTNESS stays
  // fixed, so peak output is still capped by strip.setBrightness().
  float level = (sinf(breathPhase) + 1.0f) * 0.5f; // 0..1
  uint8_t v = (uint8_t)(level * 255);
  strip.fill(strip.Color(v, v, v));
  strip.show();
  breathPhase += 0.06f;
  if (breathPhase > TWO_PI) breathPhase -= TWO_PI;
}

void stepTwinkle() {
  const unsigned long interval = 40;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  // Decay existing twinkles, occasionally ignite a new random pixel.
  for (int i = 0; i < NUM_LEDS; i++) {
    if (twinkleLevel[i] > 10) {
      twinkleLevel[i] -= 10;
    } else {
      twinkleLevel[i] = 0;
    }
  }
  if (random(0, 100) < 25) {
    int p = random(0, NUM_LEDS);
    twinkleLevel[p] = 255;
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t v = twinkleLevel[i];
    strip.setPixelColor(i, strip.Color(v, v, v));
  }
  strip.show();
}

void stepLarson() {
  const unsigned long interval = 30;
  if (millis() - lastStepTime < interval) return;
  lastStepTime = millis();

  for (int i = 0; i < NUM_LEDS; i++) {
    larsonLevel[i] = (uint8_t)(larsonLevel[i] * 0.75f);
  }
  larsonLevel[larsonPos] = 255;

  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(larsonLevel[i], 0, 0)); // red
  }
  strip.show();

  larsonPos += larsonDir;
  if (larsonPos >= NUM_LEDS - 1 || larsonPos <= 0) {
    larsonDir = -larsonDir;
  }
}

void updateAnimation() {
  switch (currentMode) {
    case 5: stepForwardWalk(); break;
    case 6: stepReverseWalk(); break;
    case 7: stepRainbow(); break;
    case 8: stepTheaterChase(); break;
    case 9: stepBreathing(); break;
    case 10: stepTwinkle(); break;
    case 11: stepLarson(); break;
    default: break; // modes 0-4 are static, already rendered on entry
  }
}

void setup() {
  Serial.begin(115200);

  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) {
    delay(10);
  }
  delay(300); // brief startup delay so a host-side terminal can attach

  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MUTE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BRIGHTNESS_PIN, INPUT_PULLUP);
  pinMode(BUTTON4_PIN, INPUT_PULLUP);

  strip.begin();
  strip.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
  strip.clear();
  strip.show();

  Serial.println(F("[SYSTEM] LED controller started"));
  Serial.println(F("[BUTTON] GPIO10 using INPUT_PULLUP (Mode)"));
  Serial.println(F("[BUTTON] GPIO11 using INPUT_PULLUP (Mute)"));
  Serial.println(F("[BUTTON] GPIO17 using INPUT_PULLUP (Brightness)"));
  Serial.println(F("[BUTTON] GPIO5 using INPUT_PULLUP (Button 4)"));

  currentMode = 0;
  modeChanged = true;
  printModeAnnouncement(); // one-time startup announcement; advanceMode()
                            // handles every subsequent mode-change print
}

void loop() {
  if (buttonPressedEdge(modeButton)) advanceMode();
  if (buttonPressedEdge(muteButton)) toggleMute();
  if (buttonPressedEdge(brightnessButton)) cycleBrightness();
  if (buttonPressedEdge(button4)) Serial.println(F("[BUTTON] Button 4 pressed"));

  if (muted) return; // suppress rendering only; button state above still updates

  if (modeChanged) {
    modeChanged = false;
    resetModeState();
  }

  updateAnimation();
}
