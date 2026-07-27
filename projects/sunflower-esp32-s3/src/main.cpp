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
  Serial.println(F("[MOTOR BREAKAWAY] Serial command: '3' = aggressive breakaway test (jolt + 1500ms drive x2 cycles)"));
#endif

  lastFrameTime = millis();
  lastFpsReportTime = millis();

  Serial.println(F("[SYSTEM] Ready"));
}

// Forward declaration: BreakawayPhase/breakawayPhase (full MOTOR BREAKAWAY
// TEST implementation is below, after MOTOR PRIORITY TEST) is needed here
// because the two tests are mutually exclusive with each other --
// startPriorityTest() below checks breakawayPhase, and startBreakawayTest()
// (further down) checks priorityTestPhase right back.
enum class BreakawayPhase {
  IDLE,
  PREPARING,
  FORWARD_JOLT,        // reverse 150ms -- opposite-direction jolt for the forward cycle
  FORWARD_JOLT_STOP,   // stop 100ms
  FORWARD_DRIVE,       // forward 1500ms -- full digital drive
  FORWARD_DRIVE_STOP,  // stop 500ms
  REVERSE_JOLT,        // forward 150ms -- opposite-direction jolt for the reverse cycle
  REVERSE_JOLT_STOP,   // stop 100ms
  REVERSE_DRIVE,       // reverse 1500ms -- full digital drive
  REVERSE_DRIVE_STOP,  // stop 500ms
  RELEASING,
};
BreakawayPhase breakawayPhase = BreakawayPhase::IDLE;

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
  // Mutually exclusive with the '3' breakaway test (defined below) -- both
  // use MotorPriorityMode and drive the motor directly.
  if (priorityTestPhase != PriorityTestPhase::IDLE || breakawayPhase != BreakawayPhase::IDLE) return;
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

// BEGIN MOTOR BREAKAWAY TEST
// Temporary diagnostic, separate from and does not replace the MOTOR
// PRIORITY TEST above ('2'). Physical result that motivated this: the
// runtime priority test still buzzes without moving from a dead stop, but
// a small manual flick of the output gear lets it start -- consistent
// with static friction, gearbox position sensitivity, mechanical
// preload, or insufficient breakaway torque, not an electrical problem.
// This test does NOT increase electrical stall torque -- digitalWrite
// HIGH/LOW is already full-power drive, same as every other test in this
// project. It attempts to reproduce the manual flick in software: a brief
// opposite-direction "jolt" to take up gear lash / unseat a sticky
// position, followed by a long (1500ms) full-power drive pulse in the
// intended direction so any resulting movement is easy to see. Repeated
// buzzing without movement even with the jolt remains a hardware/
// mechanical finding, not a reason to lengthen the pulse further. See
// docs/DRV8833_MOTOR_BRINGUP.md.
//
// Uses MotorPriorityMode exactly as the '2' test does (LEDs muted +
// suspended, audio processing suspended, buttons/serial/emergency-stop
// still live) -- mutually exclusive with the '2' test (see
// startBreakawayTest()/startPriorityTest()'s guards). BreakawayPhase and
// breakawayPhase itself are forward-declared above, before MOTOR PRIORITY
// TEST, since that test's start function needs to check this one's state.
constexpr uint32_t BREAKAWAY_JOLT_MS = 150;
constexpr uint32_t BREAKAWAY_JOLT_STOP_MS = 100;
constexpr uint32_t BREAKAWAY_DRIVE_MS = 1500;  // below the 2000ms max-energized safeguard
constexpr uint32_t BREAKAWAY_DRIVE_STOP_MS = 500;
constexpr int BREAKAWAY_TOTAL_CYCLES = 2;
// Local defensive backstop mirroring MotorBehavior.cpp's IDLE_SWAY safety
// net (that one is private to MotorBehavior.cpp and not reusable here) --
// should never actually trigger given BREAKAWAY_DRIVE_MS=1500 is already
// well under this, but guards against any future edit to the timing
// constants above accidentally exceeding it.
constexpr uint32_t BREAKAWAY_MAX_ENERGIZED_MS = 2000;

// breakawayPhase itself is declared above (forward declaration before
// MOTOR PRIORITY TEST) -- only its supporting state lives here.
unsigned long breakawayPhaseStartMs = 0;
unsigned long breakawayEnergizedSinceMs = 0;  // 0 when not energized
int breakawayCycle = 0;                        // 1 or 2 while running

const char *breakawayPhaseName(BreakawayPhase p) {
  switch (p) {
    case BreakawayPhase::IDLE: return "IDLE";
    case BreakawayPhase::PREPARING: return "PREPARING";
    case BreakawayPhase::FORWARD_JOLT: return "FORWARD_JOLT";
    case BreakawayPhase::FORWARD_JOLT_STOP: return "FORWARD_JOLT_STOP";
    case BreakawayPhase::FORWARD_DRIVE: return "FORWARD_DRIVE";
    case BreakawayPhase::FORWARD_DRIVE_STOP: return "FORWARD_DRIVE_STOP";
    case BreakawayPhase::REVERSE_JOLT: return "REVERSE_JOLT";
    case BreakawayPhase::REVERSE_JOLT_STOP: return "REVERSE_JOLT_STOP";
    case BreakawayPhase::REVERSE_DRIVE: return "REVERSE_DRIVE";
    case BreakawayPhase::REVERSE_DRIVE_STOP: return "REVERSE_DRIVE_STOP";
    case BreakawayPhase::RELEASING: return "RELEASING";
  }
  return "UNKNOWN";
}

void breakawayForward() {
  motorForward();
  breakawayEnergizedSinceMs = millis();
}
void breakawayReverse() {
  motorReverse();
  breakawayEnergizedSinceMs = millis();
}
void breakawayStop() {
  motorStop();
  breakawayEnergizedSinceMs = 0;
}

void startBreakawayTest() {
  // Mutually exclusive with the '2' priority test -- both use
  // MotorPriorityMode and drive the motor directly; running either while
  // the other is active would fight over both.
  if (breakawayPhase != BreakawayPhase::IDLE || priorityTestPhase != PriorityTestPhase::IDLE) return;
  setMotorBehavior(MotorBehaviorMode::OFF);  // ensure IDLE_SWAY isn't concurrently driving the motor
  Serial.println(F("[MOTOR BREAKAWAY] Preparing"));
  requestMotorPriority();
  breakawayCycle = 1;
  breakawayPhase = BreakawayPhase::PREPARING;
  breakawayPhaseStartMs = millis();
}

// Cancelable at every phase via 'k' -- see pollMotorSerialCommands() below.
// motorStop() (via breakawayStop()/releaseMotorPriorityImmediately(),
// which itself calls motorStop() first) always happens before peripherals
// are restored.
void cancelBreakawayTest() {
  if (breakawayPhase == BreakawayPhase::IDLE) return;
  breakawayStop();
  releaseMotorPriorityImmediately();  // stops the motor (redundant with above, harmless) then restores LEDs/audio
  breakawayPhase = BreakawayPhase::IDLE;
  breakawayEnergizedSinceMs = 0;
  Serial.println(F("[MOTOR BREAKAWAY] Cancelled"));
}

void updateBreakawayTest() {
  if (breakawayPhase == BreakawayPhase::IDLE) return;
  unsigned long now = millis();
  unsigned long elapsed = now - breakawayPhaseStartMs;

  // Defensive backstop -- see BREAKAWAY_MAX_ENERGIZED_MS above.
  if (breakawayEnergizedSinceMs != 0 && (now - breakawayEnergizedSinceMs) >= BREAKAWAY_MAX_ENERGIZED_MS) {
    Serial.println(F("[MOTOR BREAKAWAY] Safety: max energized runtime exceeded -- cancelling"));
    cancelBreakawayTest();
    return;
  }

  switch (breakawayPhase) {
    case BreakawayPhase::IDLE:
      break;
    case BreakawayPhase::PREPARING:
      if (isMotorPriorityReady()) {
        Serial.printf("[MOTOR BREAKAWAY] Cycle %d forward jolt\n", breakawayCycle);
        breakawayReverse();  // opposite-direction jolt for the forward cycle
        breakawayPhase = BreakawayPhase::FORWARD_JOLT;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::FORWARD_JOLT:
      if (elapsed >= BREAKAWAY_JOLT_MS) {
        breakawayStop();
        breakawayPhase = BreakawayPhase::FORWARD_JOLT_STOP;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::FORWARD_JOLT_STOP:
      if (elapsed >= BREAKAWAY_JOLT_STOP_MS) {
        Serial.printf("[MOTOR BREAKAWAY] Cycle %d forward drive\n", breakawayCycle);
        breakawayForward();
        breakawayPhase = BreakawayPhase::FORWARD_DRIVE;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::FORWARD_DRIVE:
      if (elapsed >= BREAKAWAY_DRIVE_MS) {
        breakawayStop();
        breakawayPhase = BreakawayPhase::FORWARD_DRIVE_STOP;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::FORWARD_DRIVE_STOP:
      if (elapsed >= BREAKAWAY_DRIVE_STOP_MS) {
        Serial.printf("[MOTOR BREAKAWAY] Cycle %d reverse jolt\n", breakawayCycle);
        breakawayForward();  // opposite-direction jolt for the reverse cycle
        breakawayPhase = BreakawayPhase::REVERSE_JOLT;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::REVERSE_JOLT:
      if (elapsed >= BREAKAWAY_JOLT_MS) {
        breakawayStop();
        breakawayPhase = BreakawayPhase::REVERSE_JOLT_STOP;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::REVERSE_JOLT_STOP:
      if (elapsed >= BREAKAWAY_JOLT_STOP_MS) {
        Serial.printf("[MOTOR BREAKAWAY] Cycle %d reverse drive\n", breakawayCycle);
        breakawayReverse();
        breakawayPhase = BreakawayPhase::REVERSE_DRIVE;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::REVERSE_DRIVE:
      if (elapsed >= BREAKAWAY_DRIVE_MS) {
        breakawayStop();
        breakawayPhase = BreakawayPhase::REVERSE_DRIVE_STOP;
        breakawayPhaseStartMs = now;
      }
      break;
    case BreakawayPhase::REVERSE_DRIVE_STOP:
      if (elapsed >= BREAKAWAY_DRIVE_STOP_MS) {
        if (breakawayCycle < BREAKAWAY_TOTAL_CYCLES) {
          breakawayCycle++;
          Serial.printf("[MOTOR BREAKAWAY] Cycle %d forward jolt\n", breakawayCycle);
          breakawayReverse();
          breakawayPhase = BreakawayPhase::FORWARD_JOLT;
          breakawayPhaseStartMs = now;
        } else {
          releaseMotorPriority();  // starts MotorPriorityMode's own 100ms settle wait
          breakawayPhase = BreakawayPhase::RELEASING;
          breakawayPhaseStartMs = now;
        }
      }
      break;
    case BreakawayPhase::RELEASING:
      if (!isMotorPriorityActive()) {  // MotorPriorityMode's RELEASING->IDLE transition completed
        setMotorBehavior(MotorBehaviorMode::OFF);
        Serial.println(F("[MOTOR BREAKAWAY] Complete"));
        breakawayPhase = BreakawayPhase::IDLE;
      }
      break;
  }
}
// END MOTOR BREAKAWAY TEST

// Live motor + MotorBehavior-test serial commands: 'f' = forward
// (continuous), 'k' = stop, '0'/'1'/'2'/'3'/'?' = MotorBehavior test
// commands (see below). Controls.cpp's pollSerialCommands() (called via
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
// MotorBehavior-test numeric convention instead. '3' (checked against the
// full command map above, including 'f'/'k'/'0'/'1'/'2'/'?' already
// reserved here -- free) continues that same convention for the MOTOR
// BREAKAWAY TEST.
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
      cancelBreakawayTest();
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
    } else if (c == '3') {
      Serial.read();
      startBreakawayTest();
    } else if (c == '?') {
      Serial.read();
      printMotorBehaviorDebugState();
      printMotorPriorityDebugState();
      Serial.printf("[MOTOR PRIORITY TEST] phase=%s\n", priorityTestPhaseName(priorityTestPhase));
      Serial.printf("[MOTOR BREAKAWAY] active=%d phase=%s cycle=%d elapsedMs=%lu\n",
                    breakawayPhase != BreakawayPhase::IDLE ? 1 : 0, breakawayPhaseName(breakawayPhase),
                    breakawayCycle, (unsigned long)(millis() - breakawayPhaseStartMs));
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
  updateBreakawayTest();       // non-blocking; no-op when the breakaway test isn't running
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
