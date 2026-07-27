#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "AudioAnalyzer.h"
#include "AudioOverlays.h"
#include "AudioVisualState.h"
#include "AutoShowcase.h"
#include "Config.h"
#include "Controls.h"
#include "LedEffects.h"
#include "VisualCue.h"
#include "MotorDriver.h"

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

static RGB8 frameBuffer[NUM_LEDS];
static unsigned long lastFrameTime = 0;
static bool wasMuted = false;

static unsigned long lastPowerWarningTime = 0;

// Real measured frame rate (updated once/second below); read by Controls.cpp's
// 'status' command.
float g_measuredFps = 0.0f;
static uint16_t frameCounter = 0;
static unsigned long lastFpsReportTime = 0;

// Estimates this frame's LED current draw and scales the buffer down in
// place if it exceeds LED_CURRENT_LIMIT_MA. This is a software estimate
// only (see Config.h) -- it does not replace correct electrical power
// design for the strip's actual supply.
static void applyPowerLimit(RGB8 *buf) {
  uint32_t channelSum = 0;
  for (int i = 0; i < NUM_LEDS; i++) channelSum += (uint32_t)buf[i].r + buf[i].g + buf[i].b;

  uint32_t estimatedMa = (uint32_t)NUM_LEDS * LED_IDLE_MA_PER_LED + (channelSum * LED_MAX_MA_PER_CHANNEL) / 255;
  if (estimatedMa <= LED_CURRENT_LIMIT_MA) return;

  float scale = (float)LED_CURRENT_LIMIT_MA / (float)estimatedMa;
  for (int i = 0; i < NUM_LEDS; i++) scaleClamp(buf[i], scale);

  unsigned long now = millis();
  if (now - lastPowerWarningTime >= POWER_WARNING_INTERVAL_MS) {
    lastPowerWarningTime = now;
    Serial.printf("[POWER] Throttling: estimated %lumA exceeds %umA limit, scaling by %.2f\n",
                  (unsigned long)estimatedMa, (unsigned)LED_CURRENT_LIMIT_MA, scale);
  }
}

void setup() {
  // Drive the motor pins to a known LOW/stopped state as the very first
  // action in setup(), before Serial/LED/mic init, so they never float.
  // Does not touch any LED/mic/button pin or timing.
  initMotor();

  Serial.begin(115200);
  uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 3000) delay(10);
  delay(300); // brief startup delay so a host-side terminal can attach

  Serial.println(F("[SYSTEM] Sunflower LED controller starting"));

  initLedEffects();

  strip.begin();
  // All brightness scaling is done explicitly in software below (see the
  // compose step in loop()), so the current power estimate reflects the
  // exact values transmitted. Keep the library's own scale at unity.
  strip.setBrightness(255);
  strip.clear();
  strip.show(); // LEDs start fully off during init

  bool micReady = initAudioAnalyzer();
  if (!micReady) {
    Serial.println(F("[SYSTEM] WARNING: microphone unavailable -- audio overlays will have no input"));
  }

  initControls();

  // Small one-shot startup verification: confirms the DRV8833 responds in
  // both directions right after the rest of Sunflower has initialized,
  // then leaves the motor stopped until commanded by future application
  // code.
  motorForwardMs(250);
  delay(250);
  motorReverseMs(250);
  Serial.println(F("[MOTOR] Initialization successful."));
  Serial.println(F("[MOTOR] Serial commands: 'f' = forward, 'k' = stop (single key, no Enter needed)"));

  lastFrameTime = millis();
  lastFpsReportTime = millis();

  Serial.println(F("[SYSTEM] Ready"));
}

// Live motor serial commands: 'f' = forward (continuous), 'k' = stop.
// Controls.cpp's pollSerialCommands() (called via updateControls() below)
// owns all other serial input via its own line buffer, so this peeks the
// next byte and only consumes it -- via Serial.read() -- when it matches
// one of these two reserved keys; every other byte is left untouched on
// the stream for Controls.cpp to read normally. 'f'/'k' were checked
// against Controls.cpp's full command set (n,p,o,x,+,-,m,d,h,g,r,b,a,c,v,
// plus the word commands "effects"/"overlays"/"status") and don't collide
// with any of it. Unlike Controls.cpp's line-buffered commands, these fire
// immediately on the single byte -- no Enter needed.
static void pollMotorSerialCommands() {
  if (Serial.available() <= 0) return;
  int c = Serial.peek();
  if (c == 'f' || c == 'F') {
    Serial.read();
    motorForward();
    Serial.println(F("[MOTOR] Forward"));
  } else if (c == 'k' || c == 'K') {
    Serial.read();
    motorStop();
    Serial.println(F("[MOTOR] Stop"));
  }
}

void loop() {
  unsigned long now = millis();

  pollMotorSerialCommands();
  updateControls(now);   // buttons + serial, non-blocking
  updateAudioAnalyzer();  // I2S capture + AudioFeatures; runs every iteration regardless of mute/frame pacing

  if (now - lastFrameTime < FRAME_INTERVAL_MS) return;
  lastFrameTime = now;

  // Rendering priority:
  //   1. muted            -> black (cue suppressed entirely, per spec)
  //   2. visual cue active -> cue frame, at its own fixed brightness cap
  //   3. otherwise        -> base effect + selected overlay, at user brightness
  // Base effects are time-derived (phase = f(now)) and any persistent
  // overlay/effect state (ripples, sparks, fireflies) ages off stored
  // timestamps, so skipping their render call while muted or mid-cue
  // never corrupts or resets anything -- it just isn't computed for
  // frames nobody would see anyway.
  if (isMuted()) {
    if (!wasMuted) {
      strip.clear();
      strip.show();
      wasMuted = true;
    }
    // else: already blanked: skip touching the strip entirely this frame.
  } else {
    wasMuted = false;

    updateAudioVisualState(getAudioFeatures(), now); // keeps 'v'/status fresh regardless of what's rendered below

    bool cueActive = renderVisualCue(now, frameBuffer);
    if (!cueActive) {
      BaseEffect effect = getCurrentBaseEffect();
      AudioOverlay overlay = getCurrentAudioOverlay();
      const AudioVisualState &audio = getAudioVisualState();

      // AUTO_SHOWCASE is not a real renderable effect (see LedEffects.h) --
      // it owns its own internal cycling/crossfade, implemented in
      // AutoShowcase.cpp, layered in here rather than inside LedEffects so
      // that module stays unaware of the higher-level showcase concept.
      if (effect == BaseEffect::AUTO_SHOWCASE) {
        updateAutoShowcase(now);
        renderAutoShowcase(frameBuffer, now);
      } else {
        renderBaseEffect(effect, frameBuffer, now);
      }

      if (overlay != AudioOverlay::OFF) applyAudioOverlay(overlay, frameBuffer, audio, now);
    }

    uint8_t brightnessRaw = cueActive ? VISUAL_CUE_BRIGHTNESS_RAW : getBrightnessRaw();
    float brightnessScale = brightnessRaw / 255.0f;
    for (int i = 0; i < NUM_LEDS; i++) scaleClamp(frameBuffer[i], brightnessScale);
    applyPowerLimit(frameBuffer);

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(frameBuffer[i].r, frameBuffer[i].g, frameBuffer[i].b));
    }
    strip.show(); // exactly one show() per rendered frame
  }

  frameCounter++;
  if (now - lastFpsReportTime >= 1000) {
    g_measuredFps = frameCounter * 1000.0f / (now - lastFpsReportTime);
    frameCounter = 0;
    lastFpsReportTime = now;
  }
}
