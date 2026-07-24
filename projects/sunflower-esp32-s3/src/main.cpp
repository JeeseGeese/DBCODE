#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <driver/i2s.h>

#define LED_PIN 4
#define BUTTON_MODE_PIN 10
#define BUTTON_MUTE_PIN 11
#define BUTTON_BRIGHTNESS_PIN 17
#define BUTTON4_PIN 5
#define NUM_LEDS 58
#define NUM_MODES 12
#define DEBOUNCE_DELAY_MS 40

// --- INMP441 I2S microphone config ---
#define I2S_PORT I2S_NUM_0
#define I2S_BCLK_PIN 6
#define I2S_WS_PIN 7
// GPIO15 is the mic's SD/DATA pin as physically wired and hardware-verified
// (quiet/speech/clap response confirmed) -- not GPIO8, which was the
// originally planned pin before the board's actual wiring was traced.
#define I2S_DIN_PIN 15
#define I2S_SAMPLE_RATE 16000
#define MIC_PRINT_INTERVAL_MS 120        // ~8Hz, within the requested 5-10Hz
#define MIC_ZERO_BYTE_WARN_THRESHOLD 50  // consecutive empty i2s_read() calls
#define MIC_STUCK_WARN_MS 3000           // how long "stuck" must persist before warning
#define MIC_SATURATION_THRESHOLD 8300000 // near max of the 24-bit signed sample range

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

// --- INMP441 microphone diagnostic state ---
bool micReady = false;
bool micDiagnosticEnabled = false;
const char *MIC_CHANNEL_NAME = "LEFT"; // kept in sync with channel_format below

// Per-window accumulators (reset every MIC_PRINT_INTERVAL_MS)
uint32_t micBytesThisWindow = 0;
int32_t micRawMin = 0;
int32_t micRawMax = 0;
int32_t micPeakCorrected = 0;
int64_t micSumSquaresCorrected = 0;
uint32_t micSampleCount = 0;
unsigned long micLastPrintTime = 0;

// Streaming DC-blocker: slow-moving estimate of the signal's DC offset,
// subtracted from each sample before peak/RMS accumulation so a constant
// bias can't mask (or fake) real AC audio content.
float micDcEstimate = 0.0f;

unsigned long micZeroByteStreak = 0;
bool micZeroByteWarned = false;

unsigned long micLastNonZeroSampleTime = 0;
bool micZeroSampleWarned = false;

unsigned long micLastNonConstantTime = 0;
bool micStuckWarned = false;

unsigned long micLastNonSaturatedTime = 0;
bool micSaturatedWarned = false;

bool initMic() {
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      // INMP441 outputs on the LEFT I2S slot when its L/R pin is tied to
      // GND (it would output on RIGHT if L/R were tied to VDD instead).
      // Hardware-verified: LEFT produces correct quiet/speech/clap response.
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[MIC] ERROR: I2S driver install failed (err=%d)\n", (int)err);
    return false;
  }

  i2s_pin_config_t pin_config = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = I2S_BCLK_PIN,
      .ws_io_num = I2S_WS_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = I2S_DIN_PIN,
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[MIC] ERROR: I2S set pin failed (err=%d)\n", (int)err);
    return false;
  }

  Serial.println(F("[MIC] I2S initialization: SUCCESS"));
  Serial.printf("[MIC] Pins: BCLK=%d WS=%d DIN=%d\n", I2S_BCLK_PIN, I2S_WS_PIN, I2S_DIN_PIN);
  Serial.printf("[MIC] Sample rate: %d Hz\n", I2S_SAMPLE_RATE);
  Serial.println(F("[MIC] Bit depth: 32-bit I2S word (24-bit sample left-justified)"));
  Serial.printf("[MIC] Selected channel: %s\n", MIC_CHANNEL_NAME);

  return true;
}

// Called every print cycle (~120ms): resets only the per-window
// accumulators. Does NOT touch the multi-second "stuck" tracking
// timestamps below -- those must persist across many windows to detect a
// condition that holds for MIC_STUCK_WARN_MS.
void resetMicWindow(unsigned long now) {
  micBytesThisWindow = 0;
  micRawMin = INT32_MAX;
  micRawMax = INT32_MIN;
  micPeakCorrected = 0;
  micSumSquaresCorrected = 0;
  micSampleCount = 0;
  micLastPrintTime = now;
}

// Called once per diagnostic session (on enable): resets everything,
// including the longer-horizon "stuck" detection state and the DC
// tracker, so a fresh session doesn't inherit stale state.
void resetMicSession(unsigned long now) {
  resetMicWindow(now);
  micZeroByteStreak = 0;
  micZeroByteWarned = false;
  micLastNonZeroSampleTime = now;
  micZeroSampleWarned = false;
  micLastNonConstantTime = now;
  micStuckWarned = false;
  micLastNonSaturatedTime = now;
  micSaturatedWarned = false;
  micDcEstimate = 0.0f;
}

void toggleMicDiagnostic() {
  micDiagnosticEnabled = !micDiagnosticEnabled;

  if (micDiagnosticEnabled && !micReady) {
    Serial.println(F("[MIC] ERROR: I2S not initialized, cannot start diagnostic"));
    micDiagnosticEnabled = false;
    return;
  }

  if (micDiagnosticEnabled) {
    Serial.println(F("[MIC] Diagnostic enabled"));
    resetMicSession(millis());
  } else {
    Serial.println(F("[MIC] Diagnostic disabled"));
  }
}

void updateMicDiagnostic() {
  if (!micDiagnosticEnabled) return;

  int32_t sampleBuf[128];
  size_t bytesRead = 0;
  // A short but non-zero timeout: a pure 0-tick poll can race the DMA's
  // buffer-ready signal and falsely report 0 bytes even when the
  // peripheral is working correctly. This stays short enough to keep the
  // button/animation loop responsive.
  esp_err_t err = i2s_read(I2S_PORT, sampleBuf, sizeof(sampleBuf), &bytesRead, pdMS_TO_TICKS(20));

  if (err == ESP_OK && bytesRead > 0) {
    micZeroByteStreak = 0;
    micZeroByteWarned = false;
    micBytesThisWindow += bytesRead;

    int samples = bytesRead / sizeof(int32_t);
    bool windowHasNonZero = false;
    bool windowHasNonSaturated = false;

    for (int i = 0; i < samples; i++) {
      // INMP441 left-justifies a 24-bit sample in the 32-bit I2S word;
      // shift out the zero-padded low byte to recover the true value.
      int32_t s = sampleBuf[i] >> 8;

      if (s < micRawMin) micRawMin = s;
      if (s > micRawMax) micRawMax = s;

      // Slow exponential DC tracker, then use the corrected (AC-only)
      // value for peak/RMS so a constant offset can't hide real audio.
      micDcEstimate += ((float)s - micDcEstimate) * 0.01f;
      float corrected = (float)s - micDcEstimate;
      int32_t correctedMag = (int32_t)fabsf(corrected);

      if (correctedMag > micPeakCorrected) micPeakCorrected = correctedMag;
      micSumSquaresCorrected += (int64_t)(corrected * corrected);
      micSampleCount++;

      int32_t mag = (s < 0) ? -s : s;
      if (mag != 0) windowHasNonZero = true;
      if (mag < MIC_SATURATION_THRESHOLD) windowHasNonSaturated = true;
    }

    if (windowHasNonZero) {
      micLastNonZeroSampleTime = millis();
      micZeroSampleWarned = false;
    }
    if (windowHasNonSaturated) {
      micLastNonSaturatedTime = millis();
      micSaturatedWarned = false;
    }
  } else {
    micZeroByteStreak++;
    if (micZeroByteStreak > MIC_ZERO_BYTE_WARN_THRESHOLD && !micZeroByteWarned) {
      Serial.println(F("[MIC] WARN: I2S reads returning 0 bytes repeatedly - check wiring/init"));
      micZeroByteWarned = true;
    }
  }

  unsigned long now = millis();
  if (now - micLastPrintTime >= MIC_PRINT_INTERVAL_MS) {
    bool windowConstant = (micSampleCount > 0) && (micRawMin == micRawMax);
    if (!windowConstant) micLastNonConstantTime = now;

    if (micSampleCount > 0) {
      double meanSquare = (double)micSumSquaresCorrected / (double)micSampleCount;
      long rms = (long)sqrt(meanSquare);
      Serial.printf("[MIC] bytes=%lu rawMin=%ld rawMax=%ld peak=%ld rms=%ld\n",
                    (unsigned long)micBytesThisWindow, (long)micRawMin, (long)micRawMax,
                    (long)micPeakCorrected, rms);
    } else {
      Serial.printf("[MIC] bytes=%lu (no samples this window)\n", (unsigned long)micBytesThisWindow);
    }

    if (now - micLastNonZeroSampleTime > MIC_STUCK_WARN_MS && !micZeroSampleWarned) {
      Serial.println(F("[MIC] WARN: samples have been exactly zero for several seconds"));
      Serial.println(F("[MIC] HINT: clock/DMA is running but data is silent -- try the other"));
      Serial.println(F("[MIC]       I2S channel slot (LEFT vs RIGHT); this is the classic"));
      Serial.println(F("[MIC]       symptom of reading an inactive channel."));
      micZeroSampleWarned = true;
    }
    if (now - micLastNonConstantTime > MIC_STUCK_WARN_MS && !micStuckWarned) {
      Serial.println(F("[MIC] WARN: raw samples appear constant/stuck for several seconds"));
      micStuckWarned = true;
    }
    if (now - micLastNonSaturatedTime > MIC_STUCK_WARN_MS && !micSaturatedWarned) {
      Serial.println(F("[MIC] WARN: samples appear saturated (clipping) for several seconds"));
      micSaturatedWarned = true;
    }

    resetMicWindow(now);
  }
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
  Serial.println(F("[BUTTON] GPIO5 using INPUT_PULLUP (Button 4 - mic diagnostic toggle)"));

  micReady = initMic(); // initMic() itself prints full boot diagnostics

  currentMode = 0;
  modeChanged = true;
  printModeAnnouncement(); // one-time startup announcement; advanceMode()
                            // handles every subsequent mode-change print
}

void loop() {
  if (buttonPressedEdge(modeButton)) advanceMode();
  if (buttonPressedEdge(muteButton)) toggleMute();
  if (buttonPressedEdge(brightnessButton)) cycleBrightness();
  if (buttonPressedEdge(button4)) toggleMicDiagnostic();

  updateMicDiagnostic(); // independent of LED mute state -- always serviced

  if (muted) return; // suppress LED rendering only; buttons/mic above still update

  if (modeChanged) {
    modeChanged = false;
    resetModeState();
  }

  updateAnimation();
}
