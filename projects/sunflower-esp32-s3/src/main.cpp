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
#include "MotorBehavior.h"
#include "MotorDriver.h"
#include "MotorPowerGuard.h"
#include "MotorPriorityMode.h"

// Temporary test-only serial interface for MotorBehavior (Task 5 of the
// motor bring-up plan, see docs/DRV8833_MOTOR_BRINGUP.md section 13). Set
// to 0 to compile out the '0'/'1'/'?' handling below entirely -- the
// underlying MotorBehavior module itself is unconditional production code
// and is unaffected by this flag.
#define ENABLE_MOTOR_BEHAVIOR_TEST 1

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

  // MotorBehavior starts in OFF (motor stopped) and is never auto-enabled
  // here -- IDLE_SWAY must be explicitly selected (see
  // ENABLE_MOTOR_BEHAVIOR_TEST commands below).
  initMotorPowerGuard();
  initMotorBehavior();
  initMotorPriorityMode();
#if ENABLE_MOTOR_BEHAVIOR_TEST
  Serial.println(F("[MOTOR BEHAVIOR] Serial commands: '0' = OFF, '1' = IDLE_SWAY, 'k' = emergency stop, '?' = state"));
  Serial.println(F("[MOTOR PRIORITY TEST] Serial command: '2' = boot-equivalent runtime motor priority test"));
#endif

  lastFrameTime = millis();
  lastFpsReportTime = millis();

  Serial.println(F("[SYSTEM] Ready"));
}

// BEGIN MOTOR PRIORITY TEST
// One-shot, non-blocking, boot-equivalent runtime test: requests
// MotorPriorityMode (LEDs muted + suspended, audio processing suspended,
// buttons/serial/emergency-stop still live), then reproduces the exact
// forward/stop/reverse timing already proven to work during the boot-time
// startup verification (motorForwardMs(250)/delay(250)/motorReverseMs(250)
// in setup()) -- but non-blocking, via polling, instead of delay(). Exists
// to establish whether recreating boot-equivalent peripheral conditions at
// runtime reproduces the successful boot movement, before considering
// motor voltage, PWM, or mechanical changes. See
// docs/DRV8833_MOTOR_BRINGUP.md.
enum class PriorityTestPhase {
  IDLE,
  PREPARING,
  FORWARD,
  STOP1,
  REVERSE,
  RELEASING,
};

PriorityTestPhase priorityTestPhase = PriorityTestPhase::IDLE;
unsigned long priorityTestPhaseStartMs = 0;

const char *priorityTestPhaseName(PriorityTestPhase p) {
  switch (p) {
    case PriorityTestPhase::IDLE: return "IDLE";
    case PriorityTestPhase::PREPARING: return "PREPARING";
    case PriorityTestPhase::FORWARD: return "FORWARD";
    case PriorityTestPhase::STOP1: return "STOP1";
    case PriorityTestPhase::REVERSE: return "REVERSE";
    case PriorityTestPhase::RELEASING: return "RELEASING";
  }
  return "UNKNOWN";
}

void startPriorityTest() {
  if (priorityTestPhase != PriorityTestPhase::IDLE) return;  // already running -- ignore re-trigger
  setMotorBehavior(MotorBehaviorMode::OFF);  // ensure IDLE_SWAY isn't concurrently driving the motor
  Serial.println(F("[MOTOR PRIORITY TEST] Preparing"));
  requestMotorPriority();
  priorityTestPhase = PriorityTestPhase::PREPARING;
  priorityTestPhaseStartMs = millis();
}

// Cancelable at every phase via 'k' -- see pollMotorSerialCommands() below.
void cancelPriorityTest() {
  if (priorityTestPhase == PriorityTestPhase::IDLE) return;
  releaseMotorPriorityImmediately();  // also stops the motor
  priorityTestPhase = PriorityTestPhase::IDLE;
  Serial.println(F("[MOTOR PRIORITY TEST] Cancelled"));
}

void updatePriorityTest() {
  if (priorityTestPhase == PriorityTestPhase::IDLE) return;
  unsigned long now = millis();
  unsigned long elapsed = now - priorityTestPhaseStartMs;

  switch (priorityTestPhase) {
    case PriorityTestPhase::IDLE:
      break;
    case PriorityTestPhase::PREPARING:
      if (isMotorPriorityReady()) {
        Serial.println(F("[MOTOR PRIORITY TEST] Forward"));
        motorForward();
        priorityTestPhase = PriorityTestPhase::FORWARD;
        priorityTestPhaseStartMs = now;
      }
      break;
    case PriorityTestPhase::FORWARD:
      if (elapsed >= 250) {
        motorStop();
        Serial.println(F("[MOTOR PRIORITY TEST] Stop"));
        priorityTestPhase = PriorityTestPhase::STOP1;
        priorityTestPhaseStartMs = now;
      }
      break;
    case PriorityTestPhase::STOP1:
      if (elapsed >= 250) {
        Serial.println(F("[MOTOR PRIORITY TEST] Reverse"));
        motorReverse();
        priorityTestPhase = PriorityTestPhase::REVERSE;
        priorityTestPhaseStartMs = now;
      }
      break;
    case PriorityTestPhase::REVERSE:
      if (elapsed >= 250) {
        motorStop();
        releaseMotorPriority();  // starts MotorPriorityMode's own 100ms settle wait
        priorityTestPhase = PriorityTestPhase::RELEASING;
        priorityTestPhaseStartMs = now;
      }
      break;
    case PriorityTestPhase::RELEASING:
      if (!isMotorPriorityActive()) {  // MotorPriorityMode's RELEASING->IDLE transition completed
        setMotorBehavior(MotorBehaviorMode::OFF);
        Serial.println(F("[MOTOR PRIORITY TEST] Complete"));
        priorityTestPhase = PriorityTestPhase::IDLE;
      }
      break;
  }
}
// END MOTOR PRIORITY TEST

// Live motor + MotorBehavior-test serial commands: 'f' = forward
// (continuous), 'k' = stop, '0'/'1'/'2'/'?' = MotorBehavior test commands
// (see below). Controls.cpp's pollSerialCommands() (called via
// updateControls() below) owns all other serial input via its own line
// buffer, so this peeks the next byte and only consumes it -- via
// Serial.read() -- when it matches one of these reserved keys; every
// other byte is left untouched on the stream for Controls.cpp to read
// normally. Checked against Controls.cpp's full command set
// (n,p,o,x,+,-,m,d,h,g,r,b,a,c,v, plus the word commands
// "effects"/"overlays"/"status") and don't collide with any of it.
// Unlike Controls.cpp's line-buffered commands, these fire immediately on
// the single byte -- no Enter needed.
//
// IMPORTANT: this is a `while` loop, not a single check -- it drains
// *every* consecutive reserved byte before returning, not just one. A
// single-check version (checking one byte, then returning, relying on the
// next loop() iteration to check again) has a real race: if two reserved
// bytes arrive in the same USB burst (e.g. '2' then 'k', as the
// boot-equivalent MOTOR PRIORITY TEST followed immediately by an
// emergency stop), consuming only the first one leaves the second sitting
// in the buffer -- and Controls.cpp's OWN pollSerialCommands() (a `while`
// loop, called later in the same loop() iteration via updateControls())
// will greedily drain it into its line buffer first, silently losing it
// from this interceptor's view forever. Discovered during validation of
// 'k' cancelling the MOTOR PRIORITY TEST at every phase -- draining here
// is what makes that guarantee hold when commands arrive back-to-back,
// not just when hand-typed with natural gaps between keystrokes.
//
// 'k' also calls stopMotorBehavior() (not just motorStop()) so it is a
// true immediate-stop for both raw MotorDriver use and any active
// MotorBehavior -- this is a deliberate substitute for the 's' key
// originally requested for MotorBehavior's emergency stop: 's' is the
// first letter of Controls.cpp's existing "status" word-command, and this
// peek-based interceptor would otherwise steal that leading byte and break
// "status" while ENABLE_MOTOR_BEHAVIOR_TEST is on. 'k' was already
// established, unused elsewhere, and reusing it avoids that regression.
// 'k' also cancels the MOTOR PRIORITY TEST (see above) at any phase.
//
// '2' was substituted for the originally-requested 'p': 'p' is
// Controls.cpp's existing "previous base effect" single-char command
// (case 'p': advanceBaseEffect(-1)), and this interceptor would otherwise
// steal that byte and break it. '2' extends the existing 0/1
// MotorBehavior-test numeric convention instead.
static void pollMotorSerialCommands() {
  while (Serial.available() > 0) {
    int c = Serial.peek();
    if (c == 'f' || c == 'F') {
      Serial.read();
      motorForward();
      Serial.println(F("[MOTOR] Forward"));
    } else if (c == 'k' || c == 'K') {
      Serial.read();
      cancelPriorityTest();
      stopMotorBehavior();
      Serial.println(F("[MOTOR] Stop"));
      Serial.println(F("[MOTOR BEHAVIOR] Emergency stop"));
    }
#if ENABLE_MOTOR_BEHAVIOR_TEST
    else if (c == '0') {
      Serial.read();
      setMotorBehavior(MotorBehaviorMode::OFF);
      Serial.println(F("[MOTOR BEHAVIOR] OFF"));
    } else if (c == '1') {
      Serial.read();
      setMotorBehavior(MotorBehaviorMode::IDLE_SWAY);
      Serial.println(F("[MOTOR BEHAVIOR] IDLE_SWAY"));
    } else if (c == '2') {
      Serial.read();
      startPriorityTest();
    } else if (c == '?') {
      Serial.read();
      printMotorBehaviorDebugState();
      printMotorPriorityDebugState();
      Serial.printf("[MOTOR PRIORITY TEST] phase=%s\n", priorityTestPhaseName(priorityTestPhase));
    }
#endif
    else {
      break;  // not one of ours -- leave it (and everything after) for Controls.cpp
    }
  }
}

void loop() {
  unsigned long now = millis();

  pollMotorSerialCommands();
  updateMotorPowerGuard();     // non-blocking; must tick every iteration regardless of MotorBehavior mode
  updateMotorPriorityMode();   // non-blocking; must tick every iteration regardless of test state
  updatePriorityTest();        // non-blocking; no-op when the priority test isn't running
  updateMotorBehavior();  // non-blocking; no-op when OFF
  updateControls(now);   // buttons + serial, non-blocking -- always runs, even during MotorPriorityMode,
                          // so buttons/serial/emergency-stop stay live (Task 3 requirement)
  // Suspended during MotorPriorityMode (boot-equivalent runtime motor
  // test) so mic reads/RMS processing don't compete with the motor for
  // system resources, mirroring the quiet state present during the
  // boot-time verification. AudioFeatures simply stay at their last
  // computed values for the (short, bounded) suspension window --
  // harmless, since rendering/overlays are also suspended (LEDs muted)
  // for the same window. See include/MotorPriorityMode.h.
  if (!isAudioProcessingSuspended()) {
    updateAudioAnalyzer();  // I2S capture + AudioFeatures; otherwise runs every iteration regardless of mute/frame pacing
  }

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
