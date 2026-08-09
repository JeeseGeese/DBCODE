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
// BEGIN HARDWARE TEST
#include "HardwareTest.h"
// END HARDWARE TEST
// BEGIN MIC RETEST
#include "MicRetest.h"
// END MIC RETEST
#include "BehaviorEngine.h"
#include "ExpressiveMotion.h"
#include "MotorBehavior.h"
#include "MotorDriver.h"
#include "MotorPowerGuard.h"
#include "MotorPriorityMode.h"
#include "MotorPwmCalibration.h"
#include "DanceEngine.h"
#include "MusicMotorController.h"
#include "SharedI2S.h"
#include "SpeakerTest.h"
#include <esp_system.h>

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
// design for the strip's actual supply. Iterates buf[0..NUM_LEDS) of
// ACTUAL composed pixel values (post-effect, post-overlay, post-brightness
// when called from loop() below) -- not a flat worst-case assumption.
//
// Not `static` as of the 2026-08-08 HWTEST power-safety fix: HardwareTest.cpp
// calls this SAME function (via an `extern` declaration, documented cross-
// file coupling for one function, the same lightweight-coupling pattern
// already used for g_measuredFps above) so the boot-time hardware test uses
// the identical current-limiting path as normal rendering, rather than a
// second implementation that could silently drift from this one.
void applyPowerLimit(RGB8 *buf) {
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
  // V1.1 power/brownout observability (see docs/current/POWER.md) --
  // esp_reset_reason() is a standard, read-only ESP-IDF call; this only
  // reports the reason the ROM bootloader already determined before Serial
  // was available, it does not change reset/brownout behavior in any way.
  // The ROM bootloader's own "rst:0x.. (BROWNOUT_RST)" line (printed before
  // this firmware's Serial.begin()) remains the earliest available signal;
  // this line makes the same fact visible AFTER Serial is up, in every
  // captured log, without needing the raw ROM boot text.
  {
    esp_reset_reason_t resetReason = esp_reset_reason();
    const char *resetReasonName;
    switch (resetReason) {
      case ESP_RST_POWERON: resetReasonName = "POWERON"; break;
      case ESP_RST_EXT: resetReasonName = "EXT"; break;
      case ESP_RST_SW: resetReasonName = "SW (esp_restart)"; break;
      case ESP_RST_PANIC: resetReasonName = "PANIC"; break;
      case ESP_RST_INT_WDT: resetReasonName = "INT_WDT"; break;
      case ESP_RST_TASK_WDT: resetReasonName = "TASK_WDT"; break;
      case ESP_RST_WDT: resetReasonName = "WDT"; break;
      case ESP_RST_DEEPSLEEP: resetReasonName = "DEEPSLEEP"; break;
      case ESP_RST_BROWNOUT: resetReasonName = "BROWNOUT"; break;
      case ESP_RST_SDIO: resetReasonName = "SDIO"; break;
      case ESP_RST_UNKNOWN:
      default: resetReasonName = "UNKNOWN"; break;
    }
    Serial.printf("[SYSTEM] Reset reason: %s (%d)\n", resetReasonName, (int)resetReason);
    if (resetReason == ESP_RST_BROWNOUT) {
      Serial.println(F("[SYSTEM] WARNING: this boot followed a brownout reset"));
    }
  }
  // Revision 9 -- printed once at every boot so a captured serial log can
  // always be matched back to the exact firmware that produced it (see
  // Config.h's "Firmware identity" comment -- added after a physical-test
  // log was mistaken for a then-current build when it actually predated
  // several revisions).
  Serial.printf("[SYSTEM] Firmware: %s\n", FIRMWARE_REVISION_TAG);
  Serial.printf("[SYSTEM] Build: %s\n", FIRMWARE_BUILD_IDENTIFIER);
  Serial.printf("[SYSTEM] MusicMotor feature revision: %s\n", FIRMWARE_MUSIC_MOTOR_FEATURE_REVISION);
  Serial.printf("[SYSTEM] MusicMotor genre profile: EDM_DUBSTEP (relative drop detection default: %s)\n",
                MUSIC_MOTOR_RELATIVE_DROP_ENABLED_DEFAULT ? "ON" : "OFF");

  initLedEffects();

  strip.begin();
  // All brightness scaling is done explicitly in software below (see the
  // compose step in loop()), so the current power estimate reflects the
  // exact values transmitted. Keep the library's own scale at unity.
  strip.setBrightness(255);
  strip.clear();
  strip.show(); // LEDs start fully off during init

  // One shared full-duplex I2S bus (I2S_NUM_0) serves both the microphone
  // (RX) and speaker (TX) -- see include/SharedI2S.h. This is the single
  // i2s_driver_install()/i2s_set_pin() call for that port; neither
  // initAudioAnalyzer() nor initSpeakerTest() below configures the driver
  // themselves, only initSharedI2S() does.
  bool sharedI2SReady = initSharedI2S();
  if (!sharedI2SReady) {
    Serial.println(F("[SYSTEM] WARNING: shared I2S bus unavailable -- microphone and speaker will both be inactive"));
  }

  bool micReady = initAudioAnalyzer();
  if (!micReady) {
    Serial.println(F("[SYSTEM] WARNING: microphone unavailable -- audio overlays will have no input"));
  }

  initSpeakerTest();

  initControls();

  // BEGIN HARDWARE TEST
  // Temporary bring-up verification -- see include/HardwareTest.h for scope
  // and removal instructions. Runs once, blocking, before the normal loop()
  // begins; does not alter any state loop() depends on.
  runHardwareTestSequence(strip);
  // END HARDWARE TEST

  // BEGIN MIC RETEST
  // Dedicated mic-only diagnostic -- see include/MicRetest.h for scope and
  // removal instructions. Runs once, blocking, after the HardwareTest
  // sequence above and before the normal loop() begins.
  runMicRetest();
  // END MIC RETEST

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
  initMotorPwmCalibration();
#if ENABLE_LEGACY_DANCE_ENGINE
  initDanceEngine();  // superseded by MusicMotorController -- see DanceEngine.h
#endif
  initMusicMotorController();
  initExpressiveMotion();  // resets to ExpressiveMotionMode::OFF -- user must explicitly enable
  initBehaviorEngine();    // resets to BehaviorState::MANUAL -- user must explicitly select a state
#if ENABLE_MOTOR_BEHAVIOR_TEST
  Serial.println(F("[MOTOR BEHAVIOR] Serial commands: '0' = OFF, '1' = IDLE_SWAY, 'k' = emergency stop, '?' = state"));
  Serial.println(F("[MOTOR PRIORITY TEST] Serial command: '2' = boot-equivalent runtime motor priority test"));
  Serial.println(F("[MOTOR BREAKAWAY] Serial command: '3' = aggressive breakaway test (jolt + 1500ms drive x2 cycles)"));
  Serial.println(F("[MOTOR LED POWER] Serial command: '4' = cycle experimental motor-motion LED brightness (0/4/8/12/16)"));
  Serial.println(F("[MOTOR+LED TEST] Serial command: '5' = experimental motor+dim-LED coexistence test (forward+reverse)"));
  Serial.println(F("[LED MAP] Serial command: '6' = LED index mapping tool (color test + interactive index walk)"));
  Serial.println(F("[AUDIO LOG] Serial command: '7' = toggle continuous audio serial output (default OFF)"));
  Serial.println(F("[SPEAKER] Word commands (Enter-terminated): 't' = 440Hz test tone, 's' = 440Hz square wave"));
  Serial.println(F("[SPEAKER] Word commands: low/mid/high/sweep/melody/beep/noise/loud -- diagnostic suite, see README"));
  Serial.println(F("[SPEAKER] Word commands: music1/music2/music3/music4 (loop), stopmusic -- see README"));
  Serial.println(F("[MOTION] Word commands: 'motion' = cycle mode, 'motion off/idle/audio/status/demo' -- see README"));
  Serial.println(F("[BEHAVIOR] Word commands: 'behavior' = status+help, 'behavior "
                    "manual/idle/curious/listening/pondering/excited/sleeping/next/status/demo' -- see README"));
  Serial.println(F("[MOTOR PWM TEST] Word commands: 'mf'/'mr' select direction, 'm20'..'m100' run at that %, "
                    "'mstop' coast+cancel, 'mramp'/'mcycle' automatic tests, 'mkick' toggle startup kick, "
                    "'mstatus' full status -- see README"));
#if ENABLE_LEGACY_DANCE_ENGINE
  Serial.println(F("[DANCE ENGINE] Word commands: 'danceon'/'danceoff' enable/disable live mic-driven dancing "
                    "(80-100% active range), 'dancestatus' full status, 'dancetest'/'dancetestoff' deterministic "
                    "simulated sequence, 'dancequiet'/'dancemid'/'dancehigh'/'dancepeak' simulated overrides -- see README"));
#endif
  Serial.println(F("[MUSIC MOTOR] Word commands: 'musicmotor on'/'musicmotor off' enable/disable music-reactive "
                    "dance-phrase movement (slow sway -> bass hit -> hip shake -> slowdown), 'musicmotor status' "
                    "full status, 'musicmotor slow/fast/hitthreshold/beatthreshold/accel/hold/decel <value>' "
                    "temporary tuning -- see README"));
#endif

  lastFrameTime = millis();
  lastFpsReportTime = millis();

  Serial.println(F("[SYSTEM] Ready"));
}

// Forward declaration: BreakawayPhase/breakawayPhase (full MOTOR BREAKAWAY
// TEST implementation is below, after MOTOR PRIORITY TEST) is needed here
// because the two tests are mutually exclusive with each other --
// startPriorityTest() below checks breakawayPhase, and startBreakawayTest()
// (further down) checks priorityTestPhase right back. MotorLedTestPhase and
// LedMapPhase (full implementations further below, after MOTOR BREAKAWAY
// TEST) are forward-declared here for the same reason -- all four tests
// drive the motor and/or write LEDs directly and must be mutually
// exclusive with each other.
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

enum class MotorLedTestPhase {
  IDLE,
  PREPARING,
  FORWARD,
  STOP1,
  REVERSE,
  STOP2,
  RELEASING,
};
MotorLedTestPhase motorLedTestPhase = MotorLedTestPhase::IDLE;

// LED index mapping tool ('6' -- see BEGIN LED INDEX MAPPING TOOL below).
// Replaced an earlier assumption-based row test; forward-declared here for
// the same mutual-exclusion reason as BreakawayPhase above.
enum class LedMapPhase {
  IDLE,
  COLOR_RED,
  COLOR_GREEN,
  COLOR_BLUE,
  COLOR_OFF,          // brief gap before entering INDEX_WALK
  INDEX_WALK,          // interactive -- no auto-timer, waits for n/p/j/r/c/x
  CANDIDATE_ROW1,
  CANDIDATE_ROW1_OFF,
  CANDIDATE_ROW2,
  CANDIDATE_ROW2_OFF,
  CANDIDATE_ROW3,
  CANDIDATE_ROW3_OFF,
};
LedMapPhase ledMapPhase = LedMapPhase::IDLE;
bool isLedMapActive() { return ledMapPhase != LedMapPhase::IDLE; }

// Forward declaration: serviceEmergencyStop() (full definition further
// below, alongside the emergency-stop latch and the central serial
// dispatcher) is needed here because updatePriorityTest()/
// updateBreakawayTest()/updateMotorLedTest() below each call it once at
// the top of their own body, defensively re-servicing an already-latched
// 'k' immediately before that call's state-transition logic runs (see
// docs/DRV8833_MOTOR_BRINGUP.md, "command-5 emergency-stop investigation",
// TASK 4).
void serviceEmergencyStop();

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
  // Mutually exclusive with the '3' breakaway test, the '5' motor+LED
  // coexistence test, the '6' LED map, expressive motion/'motion demo', and
  // the manual PWM calibration test ('mf'/'mr'/'m##'/'mramp'/'mcycle' --
  // see include/MotorPwmCalibration.h) -- all drive the motor and/or write
  // LEDs directly.
  if (priorityTestPhase != PriorityTestPhase::IDLE || breakawayPhase != BreakawayPhase::IDLE ||
      motorLedTestPhase != MotorLedTestPhase::IDLE || isLedMapActive() || isExpressiveMotionMoving() ||
      isMotorPwmCalibrationActive() || isDanceEngineActive() || isMusicMotorControllerActive()) {
    return;
  }
  setMotorBehavior(MotorBehaviorMode::OFF);  // ensure IDLE_SWAY isn't concurrently driving the motor
  Serial.println(F("[MOTOR PRIORITY TEST] Preparing"));
  requestMotorPriority();
  priorityTestPhase = PriorityTestPhase::PREPARING;
  priorityTestPhaseStartMs = millis();
}

// Cancelable at every phase via 'k' -- see pollSerialDispatcher() below.
void cancelPriorityTest() {
  if (priorityTestPhase == PriorityTestPhase::IDLE) return;
  releaseMotorPriorityImmediately();  // also stops the motor
  priorityTestPhase = PriorityTestPhase::IDLE;
  Serial.println(F("[MOTOR PRIORITY TEST] Cancelled"));
}

void updatePriorityTest() {
  if (priorityTestPhase == PriorityTestPhase::IDLE) return;
  serviceEmergencyStop();  // defensive re-check before this transition -- see TASK 4 comment above
  if (priorityTestPhase == PriorityTestPhase::IDLE) return;  // may have just been cancelled by the line above
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
  // Mutually exclusive with the '2' priority test, the '5' motor+LED
  // coexistence test, the '6' LED map, expressive motion/'motion demo', and
  // the manual PWM calibration test -- all drive the motor and/or write
  // LEDs directly; running more than one at a time would fight over both.
  if (breakawayPhase != BreakawayPhase::IDLE || priorityTestPhase != PriorityTestPhase::IDLE ||
      motorLedTestPhase != MotorLedTestPhase::IDLE || isLedMapActive() || isExpressiveMotionMoving() ||
      isMotorPwmCalibrationActive() || isDanceEngineActive() || isMusicMotorControllerActive()) {
    return;
  }
  setMotorBehavior(MotorBehaviorMode::OFF);  // ensure IDLE_SWAY isn't concurrently driving the motor
  Serial.println(F("[MOTOR BREAKAWAY] Preparing"));
  requestMotorPriority();
  breakawayCycle = 1;
  breakawayPhase = BreakawayPhase::PREPARING;
  breakawayPhaseStartMs = millis();
}

// Cancelable at every phase via 'k' -- see pollSerialDispatcher() below.
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
  serviceEmergencyStop();  // defensive re-check before this transition -- see TASK 4 comment above
  if (breakawayPhase == BreakawayPhase::IDLE) return;  // may have just been cancelled by the line above
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

// BEGIN MOTOR+LED COEXISTENCE TEST
// Experimental: runs one forward + one reverse motor movement while LEDs
// stay ON (not muted) at a low, user-selectable brightness, via
// MotorPowerGuard's DIM_DURING_MOTION mode (see MotorPowerGuard.h and the
// render-loop branch in loop() above). The currently-selected base
// effect/overlay/mute state are left completely alone -- MotorLedPowerMode
// resets to the safe FULL_MUTE default on completion or cancellation, so
// '2'/'3' are unaffected by having run this. See
// docs/DRV8833_MOTOR_BRINGUP.md. Does NOT claim physical validation of
// motor+LED coexistence -- the user must observe LED flicker/pulsing/color
// corruption, motor slowdown, resets/brownouts, and audio instability
// directly. MotorLedTestPhase/motorLedTestPhase are forward-declared above
// (before MOTOR PRIORITY TEST) for the same mutual-exclusion reason as
// BreakawayPhase.
//
// KNOWN RELIABILITY FINDING (see docs/DRV8833_MOTOR_BRINGUP.md section 21):
// 'k' was observed to intermittently fail to cancel this specific test
// (~50% miss rate in isolated repeated trials, specifically during the
// FORWARD phase) -- the LED ROW TEST ('6'), which uses the identical 'k'
// interceptor plumbing, cancelled reliably (5/5) in the same test
// methodology. The likely cause: this is the only test in the codebase
// that keeps full-rate LED rendering running (strip.show() every
// ~FRAME_INTERVAL_MS) *during* motor engagement -- every other motor test
// mutes/suspends LEDs entirely for that window. Even when 'k' is missed,
// this test's own bounded per-phase timing (250ms segments) always
// self-terminates and returns everything to IDLE/FULL_MUTE on its own --
// the motor was never observed to run away or exceed the 2000ms
// safeguard -- but do not treat 'k' as an instantaneous guarantee for
// THIS test until this is root-caused. Keep power/reset accessible during
// physical testing.
unsigned long motorLedTestPhaseStartMs = 0;

void startMotorLedTest() {
  // Mutually exclusive with '2'/'3'/'6', expressive motion, and the manual
  // PWM calibration test -- see their own guards above.
  if (motorLedTestPhase != MotorLedTestPhase::IDLE || priorityTestPhase != PriorityTestPhase::IDLE ||
      breakawayPhase != BreakawayPhase::IDLE || isLedMapActive() || isExpressiveMotionMoving() ||
      isMotorPwmCalibrationActive() || isDanceEngineActive() || isMusicMotorControllerActive()) {
    return;
  }
  setMotorBehavior(MotorBehaviorMode::OFF);  // ensure IDLE_SWAY isn't concurrently driving the motor
  Serial.println(F("[MOTOR+LED TEST] Preparing"));
  setMotorLedPowerMode(MotorLedPowerMode::DIM_DURING_MOTION);
  requestMotorPriority();
  Serial.println(F("[MOTOR+LED TEST] Dim LEDs active"));
  motorLedTestPhase = MotorLedTestPhase::PREPARING;
  motorLedTestPhaseStartMs = millis();
}

// Cancelable at every phase via 'k' -- see pollSerialDispatcher() below.
void cancelMotorLedTest() {
  if (motorLedTestPhase == MotorLedTestPhase::IDLE) return;
  releaseMotorPriorityImmediately();  // also stops the motor
  setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);  // restore the safe default for future '2'/'3' runs
  motorLedTestPhase = MotorLedTestPhase::IDLE;
  Serial.println(F("[MOTOR+LED TEST] Cancelled"));
}

void updateMotorLedTest() {
  if (motorLedTestPhase == MotorLedTestPhase::IDLE) return;
  serviceEmergencyStop();  // defensive re-check before this transition -- see TASK 4 comment above
  if (motorLedTestPhase == MotorLedTestPhase::IDLE) return;  // may have just been cancelled by the line above
  unsigned long now = millis();
  unsigned long elapsed = now - motorLedTestPhaseStartMs;

  switch (motorLedTestPhase) {
    case MotorLedTestPhase::IDLE:
      break;
    case MotorLedTestPhase::PREPARING:
      if (isMotorPriorityReady()) {  // waits out both the settle delay AND the brightness ramp-down
        Serial.println(F("[MOTOR+LED TEST] Forward"));
        motorForward();
        motorLedTestPhase = MotorLedTestPhase::FORWARD;
        motorLedTestPhaseStartMs = now;
      }
      break;
    case MotorLedTestPhase::FORWARD:  // below the 2000ms max-energized safeguard (matches the proven boot timing)
      if (elapsed >= 250) {
        motorStop();
        motorLedTestPhase = MotorLedTestPhase::STOP1;
        motorLedTestPhaseStartMs = now;
      }
      break;
    case MotorLedTestPhase::STOP1:
      if (elapsed >= 250) {
        Serial.println(F("[MOTOR+LED TEST] Reverse"));
        motorReverse();
        motorLedTestPhase = MotorLedTestPhase::REVERSE;
        motorLedTestPhaseStartMs = now;
      }
      break;
    case MotorLedTestPhase::REVERSE:
      if (elapsed >= 250) {
        motorStop();
        motorLedTestPhase = MotorLedTestPhase::STOP2;
        motorLedTestPhaseStartMs = now;
      }
      break;
    case MotorLedTestPhase::STOP2:
      if (elapsed >= 250) {
        Serial.println(F("[MOTOR+LED TEST] Restoring"));
        releaseMotorPriority();  // starts MotorPriorityMode's own 100ms settle wait, then the brightness ramp-up
        motorLedTestPhase = MotorLedTestPhase::RELEASING;
        motorLedTestPhaseStartMs = now;
      }
      break;
    case MotorLedTestPhase::RELEASING:
      if (!isMotorPriorityActive()) {  // settle + ramp-up both complete
        setMotorLedPowerMode(MotorLedPowerMode::FULL_MUTE);  // restore the safe default for future '2'/'3' runs
        setMotorBehavior(MotorBehaviorMode::OFF);
        Serial.println(F("[MOTOR+LED TEST] Complete"));
        motorLedTestPhase = MotorLedTestPhase::IDLE;
      }
      break;
  }
}
// END MOTOR+LED COEXISTENCE TEST

// BEGIN LED INDEX MAPPING TOOL (serial command '6')
// Replaces an earlier "row identification test" that assumed the logical
// LED_ROW_1/2/3 ranges (Config.h) already matched the physical wiring
// order -- the user reported command 6 did not correctly identify
// physical rows/colors, so that assumption is now treated as UNVERIFIED
// (see docs/DRV8833_MOTOR_BRINGUP.md for the full writeup). This tool
// determines the real mapping instead of assuming it:
//   Phase A (COLOR_RED/GREEN/BLUE/OFF): all physically-expected LEDs lit
//     red, then green, then blue, ~1s each -- checks whether the
//     configured NEO_GRB pixel order actually produces the expected color
//     on the physical strip.
//   Phase B (INDEX_WALK): exactly one logical index lit at a time,
//     interactively stepped by the user, so they can record where each
//     index physically lands.
//   Phase C (CANDIDATE_ROW1/2/3): reachable from within Phase B via 'c' --
//     an OPTIONAL, clearly-labeled check of the candidate LED_ROW_1/2/3
//     ranges. Never claimed as confirmed.
// Bypasses the normal frameBuffer render pipeline entirely for its whole
// duration (see isLedMapActive() gating loop()'s render section above),
// so there is exactly one strip.show() owner active at any moment. No
// explicit "restore previous state" bookkeeping is needed: this tool never
// touches frameBuffer, base effect, overlay, brightness, or mute state --
// the very next normal frame after it ends recomposes frameBuffer from
// scratch and writes it to `strip` as usual. No second NeoPixel object.
constexpr uint8_t LED_MAP_DIM_LEVEL = 22;  // low brightness per channel, out of 255
unsigned long ledMapPhaseStartMs = 0;
uint16_t ledMapCurrentIndex = 0;

void ledMapShowAll(uint8_t r, uint8_t g, uint8_t b) {
  strip.clear();
  for (uint16_t i = 0; i < PHYSICAL_LED_COUNT; i++) strip.setPixelColor(i, strip.Color(r, g, b));
  strip.show();
}

void ledMapShowRegion(const LedRegion &region, uint8_t r, uint8_t g, uint8_t b) {
  strip.clear();
  for (uint16_t i = region.start; i < region.start + region.count; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

void ledMapPrintWorksheetIntro() {
  Serial.println(F("[LED MAP] Record physical position for each index."));
  Serial.println(F("[LED MAP] Suggested notation:"));
  Serial.println(F("[LED MAP] index -> row, position, direction"));
  Serial.println(F("[LED MAP] Example: 0 -> row 2, leftmost"));
  Serial.println(
      F("[LED MAP] Controls: n=next p=previous j=jump+10 r=restart-at-0 c=candidate-row-test x=exit k=cancel"));
}

// idx is always < PHYSICAL_LED_COUNT (<= NUM_LEDS, see Config.h's
// static_assert) -- every caller below clamps before reaching here.
void ledMapGoToIndex(uint16_t idx) {
  ledMapCurrentIndex = idx;
  strip.clear();
  strip.setPixelColor(ledMapCurrentIndex, strip.Color(0, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL));  // cyan
  strip.show();
  Serial.printf("[LED MAP] INDEX %u\n", ledMapCurrentIndex);
}

void ledMapPrintCompletionReminder() {
  Serial.println(F("[LED MAP] Please report:"));
  Serial.println(F("[LED MAP]  - physical location of index 0"));
  Serial.println(F("[LED MAP]  - last index in first physical row"));
  Serial.println(F("[LED MAP]  - first index in second physical row"));
  Serial.println(F("[LED MAP]  - last index in second physical row"));
  Serial.println(F("[LED MAP]  - first index in third physical row"));
  Serial.println(F("[LED MAP]  - last responsive physical LED"));
  Serial.println(F("[LED MAP]  - whether each row runs left-to-right or right-to-left"));
  Serial.println(F("[LED MAP]  - whether red/green/blue were correct"));
}

void startLedMap() {
  // Mutually exclusive with '2'/'3'/'5', expressive motion, and the manual
  // PWM calibration test -- see their own guards above.
  if (isLedMapActive() || priorityTestPhase != PriorityTestPhase::IDLE || breakawayPhase != BreakawayPhase::IDLE ||
      motorLedTestPhase != MotorLedTestPhase::IDLE || isExpressiveMotionMoving() || isMotorPwmCalibrationActive() ||
      isDanceEngineActive() || isMusicMotorControllerActive()) {
    return;
  }
  Serial.println(F("[LED MAP] COLOR TEST: RED"));
  ledMapShowAll(LED_MAP_DIM_LEVEL, 0, 0);
  ledMapPhase = LedMapPhase::COLOR_RED;
  ledMapPhaseStartMs = millis();
}

// Cancelable at every phase via 'k' -- see pollSerialDispatcher() below.
void cancelLedMap() {
  if (!isLedMapActive()) return;
  strip.clear();
  strip.show();
  ledMapPhase = LedMapPhase::IDLE;
  Serial.println(F("[LED MAP] Cancelled"));
  ledMapPrintCompletionReminder();
}

// 'x' -- deliberate, non-emergency exit from within INDEX_WALK.
void exitLedMap() {
  if (!isLedMapActive()) return;
  strip.clear();
  strip.show();
  ledMapPhase = LedMapPhase::IDLE;
  Serial.println(F("[LED MAP] Exit -- restoring previous LED state"));
  ledMapPrintCompletionReminder();
}

// 'c' -- Phase C, reachable only from INDEX_WALK. Returns to INDEX_WALK
// (re-lighting whichever index was active before) once it completes.
void startLedMapCandidateRows() {
  if (ledMapPhase != LedMapPhase::INDEX_WALK) return;
  Serial.println(F("[LED MAP] Candidate ranges are unverified -- observe and report."));
  Serial.println(F("[LED MAP] CANDIDATE ROW 1: 0-9"));
  ledMapShowRegion(LED_ROW_1, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL);
  ledMapPhase = LedMapPhase::CANDIDATE_ROW1;
  ledMapPhaseStartMs = millis();
}

// Dispatched only while ledMapPhase == INDEX_WALK (see pollSerialDispatcher()
// below) -- n/p/j/r/c/x are otherwise ordinary Controls.cpp bytes.
void handleLedMapKey(char c) {
  if (ledMapPhase != LedMapPhase::INDEX_WALK) return;  // ignore controls during auto-timed phases A/C
  switch (c) {
    case 'n':
    case 'N':
      if (ledMapCurrentIndex + 1 < PHYSICAL_LED_COUNT) ledMapGoToIndex(ledMapCurrentIndex + 1);
      break;
    case 'p':
    case 'P':
      if (ledMapCurrentIndex > 0) ledMapGoToIndex(ledMapCurrentIndex - 1);
      break;
    case 'j':
    case 'J': {
      uint16_t target = (uint16_t)min((int)ledMapCurrentIndex + 10, (int)PHYSICAL_LED_COUNT - 1);
      ledMapGoToIndex(target);
      break;
    }
    case 'r':
    case 'R':
      ledMapGoToIndex(0);
      break;
    case 'c':
    case 'C':
      startLedMapCandidateRows();
      break;
    case 'x':
    case 'X':
      exitLedMap();
      break;
    default:
      break;  // ignore anything else while mapping owns the strip
  }
}

void updateLedMap() {
  if (!isLedMapActive()) return;
  serviceEmergencyStop();  // defensive re-check before any transition -- see TASK 4 comment above
  if (!isLedMapActive()) return;  // may have just been cancelled by the line above
  unsigned long now = millis();
  unsigned long elapsed = now - ledMapPhaseStartMs;

  switch (ledMapPhase) {
    case LedMapPhase::IDLE:
    case LedMapPhase::INDEX_WALK:
      break;  // interactive -- no auto-timer, waits for handleLedMapKey()
    case LedMapPhase::COLOR_RED:
      if (elapsed >= 1000) {
        Serial.println(F("[LED MAP] COLOR TEST: GREEN"));
        ledMapShowAll(0, LED_MAP_DIM_LEVEL, 0);
        ledMapPhase = LedMapPhase::COLOR_GREEN;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::COLOR_GREEN:
      if (elapsed >= 1000) {
        Serial.println(F("[LED MAP] COLOR TEST: BLUE"));
        ledMapShowAll(0, 0, LED_MAP_DIM_LEVEL);
        ledMapPhase = LedMapPhase::COLOR_BLUE;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::COLOR_BLUE:
      if (elapsed >= 1000) {
        strip.clear();
        strip.show();
        ledMapPhase = LedMapPhase::COLOR_OFF;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::COLOR_OFF:
      if (elapsed >= 300) {
        ledMapPrintWorksheetIntro();
        ledMapPhase = LedMapPhase::INDEX_WALK;
        ledMapGoToIndex(0);
      }
      break;
    case LedMapPhase::CANDIDATE_ROW1:
      if (elapsed >= 1000) {
        strip.clear();
        strip.show();
        ledMapPhase = LedMapPhase::CANDIDATE_ROW1_OFF;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::CANDIDATE_ROW1_OFF:
      if (elapsed >= 300) {
        Serial.println(F("[LED MAP] CANDIDATE ROW 2: 10-19"));
        ledMapShowRegion(LED_ROW_2, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL);
        ledMapPhase = LedMapPhase::CANDIDATE_ROW2;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::CANDIDATE_ROW2:
      if (elapsed >= 1000) {
        strip.clear();
        strip.show();
        ledMapPhase = LedMapPhase::CANDIDATE_ROW2_OFF;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::CANDIDATE_ROW2_OFF:
      if (elapsed >= 300) {
        Serial.println(F("[LED MAP] CANDIDATE ROW 3: 20-41"));
        ledMapShowRegion(LED_ROW_3, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL, LED_MAP_DIM_LEVEL);
        ledMapPhase = LedMapPhase::CANDIDATE_ROW3;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::CANDIDATE_ROW3:
      if (elapsed >= 1000) {
        strip.clear();
        strip.show();
        ledMapPhase = LedMapPhase::CANDIDATE_ROW3_OFF;
        ledMapPhaseStartMs = now;
      }
      break;
    case LedMapPhase::CANDIDATE_ROW3_OFF:
      if (elapsed >= 300) {
        ledMapPhase = LedMapPhase::INDEX_WALK;
        ledMapGoToIndex(ledMapCurrentIndex);  // back to whichever index was active before Phase C
      }
      break;
  }
}
// END LED INDEX MAPPING TOOL

// Implements ExpressiveMotion.h's forward-declared query -- see that
// header's comment. True while any of the four motor/LED diagnostics is
// active; ExpressiveMotion checks this before starting a movement or
// 'motion demo', mirroring the mutual-exclusion checks the four
// diagnostics' own start functions make against isExpressiveMotionMoving()
// above.
bool isAnyMotorDiagnosticActive() {
  return priorityTestPhase != PriorityTestPhase::IDLE || breakawayPhase != BreakawayPhase::IDLE ||
         motorLedTestPhase != MotorLedTestPhase::IDLE || isLedMapActive() || isMotorPwmCalibrationActive() ||
         isDanceEngineActive() || isMusicMotorControllerActive();
}

// Implements ExpressiveMotion.h's forward-declared query -- see that
// header's comment. Mirrors isAnyMotorDiagnosticActive()'s exact membership
// and order; keep the two in sync if a new motor owner is ever added.
const char *currentMotorOwnerName() {
  if (priorityTestPhase != PriorityTestPhase::IDLE) return "MotorPriorityMode";
  if (breakawayPhase != BreakawayPhase::IDLE) return "Breakaway test";
  if (motorLedTestPhase != MotorLedTestPhase::IDLE) return "Motor+LED test";
  if (isLedMapActive()) return "LED index map";
  if (isMotorPwmCalibrationActive()) return "MotorPwmCalibration";
  if (isDanceEngineActive()) return "DanceEngine";
  if (isMusicMotorControllerActive()) return "MusicMotorController";
  return nullptr;
}

// ============================================================================
// Emergency-stop latch (see docs/DRV8833_MOTOR_BRINGUP.md, "command-5
// emergency-stop investigation")
// ============================================================================
// 'k' sets this the instant the byte is read by pollSerialDispatcher(),
// rather than the byte's handler running an ad-hoc, one-off sequence of
// calls inline. serviceEmergencyStop() is the single place that performs
// the FULL stop (every test's cancel function, MotorBehavior, MotorDriver,
// and the pending Controls.cpp line buffer) and is the only thing that
// clears the latch -- it is never cleared just because one subsystem
// (e.g. cancelPriorityTest()) decided there was nothing for it to do.
// Idempotent and cheap, so it's safe to call defensively from multiple
// points (see TASK 4 in the investigation: serviced before/after
// strip.show() and at the top of every motor-state-transition update
// function, in addition to the instant it's set) without needing to
// reason about double-stopping.
static volatile bool emergencyStopLatched = false;

void serviceEmergencyStop() {
  if (!emergencyStopLatched) return;
  cancelPriorityTest();
  cancelBreakawayTest();
  cancelMotorLedTest();
  cancelLedMap();
  motorCalEmergencyCancel();  // coasts + detaches PWM + cancels every pending motor-test state; idempotent
  cancelDanceEngine();       // coasts + detaches PWM + disables DanceEngine; idempotent, never auto-resumes
  cancelMusicMotorController();  // coasts + detaches PWM + disables MusicMotorController; idempotent
  cancelExpressiveMotion();  // also forces expressive motion to DISABLED -- see its own comment
  stopBehaviorEngine();      // forces BehaviorState to MANUAL -- does not itself touch the motor (see its own comment)
  stopSpeakerTest();         // immediately ends any playing speaker test tone, returns to digital silence
  stopMotorBehavior();
  motorStop();  // explicit backstop even though every path above already stops the motor via its own cancel
  // Defensive no-op in the common case: pollSerialDispatcher() now only
  // triggers 'k' while isSerialLinePending() is false (see its own
  // comment), so there's normally nothing pending here to discard. Kept as
  // a harmless backstop in case emergencyStopLatched is ever set from
  // another path in the future.
  clearPendingSerialLine();
  emergencyStopLatched = false;
  Serial.println(F("[MOTOR] Stop"));
  Serial.println(F("[MOTOR BEHAVIOR] Emergency stop"));
}

void triggerEmergencyStop() {
  emergencyStopLatched = true;
  serviceEmergencyStop();  // single-threaded synchronous loop -- "next executable point" is now
}

// ============================================================================
// Central serial dispatcher -- the ONLY place in the whole program that
// calls Serial.read()/available() (see docs/DRV8833_MOTOR_BRINGUP.md,
// "command-5 emergency-stop investigation"). This replaces an earlier
// design where this function only *peeked* reserved bytes (f/k/0-6/?) and
// left everything else on the stream for Controls.cpp's OWN independent
// `while (Serial.available())` loop to read. That worked most of the time,
// but proved to have a real race: the two loops ran at different points
// within the same loop() iteration, and a byte (specifically 'k') arriving
// in the gap between them could be read by Controls.cpp's loop first --
// silently absorbed into its Enter-terminated line buffer (no dispatch,
// since no '\n' follows a lone 'k') and never seen by this dispatcher at
// all. Proven with temporary instrumentation: a counter in Controls.cpp
// that should always read zero (nothing there should ever see a reserved
// byte) instead incremented in lockstep with every observed 'k'-miss
// during the '5' motor+LED test, while this dispatcher's own 'k' counter
// stayed flat on those same misses -- i.e. the byte was never lost at the
// UART/USB level, it was read by the *other* consumer. strip.show()'s own
// duration was also measured and stayed constant (~2.16ms) regardless of
// whether cancellation succeeded, ruling it out as the direct cause.
//
// Fix: exactly one owner reads Serial now. Every byte is read here, then
// either handled directly (the same reserved set as before: f/k/0-6/?) or
// forwarded exactly once to Controls.cpp's feedSerialByte() -- which no
// longer touches Serial itself (see Controls.cpp). This removes the race
// structurally rather than narrowing its window: there is no longer a
// second reader for a byte to be stolen by, at any point in the loop()
// iteration.
//
// 'k' triggers the emergency-stop latch (see triggerEmergencyStop() above
// loop()) rather than acting inline -- see that function's comment for why
// a latch, not just an inline call sequence, is the safer shape here. It is
// checked in the same position every other reserved single-char command
// occupies -- i.e. only once isSerialLinePending() below confirms no word
// command is currently mid-type -- NOT unconditionally first as an earlier
// revision of this comment described; see the isSerialLinePending() check
// itself for why that changed (a 'k' inside a legitimately in-progress
// command like "musicmotor peakthreshold" must not fire emergency-stop).
//
// This is still a `while` loop draining every consecutive byte in one
// pass (not a single check per loop() iteration), which is what makes
// back-to-back input like "5k" or "2k" deterministic: both bytes are
// processed, in order, within the same call, rather than one potentially
// waiting for the next loop() iteration (during which something else
// could still observe/act on intermediate state).
//
// Reserved set: f/k/0/1/2/3/4/5/6/7/? (checked against Controls.cpp's full
// command set -- n,p,o,x,+,-,m,d,h,g,r,b,a,c,v,t,s, plus the word commands
// effects/overlays/status/motion/behavior/low/mid/high/sweep/melody/beep/
// noise/loud -- with no collisions; two deliberate substitutions from what
// was originally requested are documented in README.md's Serial commands
// section: 's'->'k' and 'p'->'2'. 't'/'s' themselves moved from this
// dispatcher's own reserved set into Controls.cpp's Enter-terminated
// single-char switch after being found to intercept the first byte of any
// word command starting with the same letter -- see Controls.cpp's own
// comment at those two case labels).
uint32_t g_interceptorTotalBytesSeen = 0;
uint32_t g_interceptorKSeen = 0;
uint32_t g_maxShowUs = 0;
uint32_t g_maxLoopUs = 0;

static void pollSerialDispatcher() {
  while (Serial.available() > 0) {
    int c = Serial.read();  // the single Serial.read() call site in the entire program
    g_interceptorTotalBytesSeen++;

    // REVISED (see docs/DRV8833_MOTOR_BRINGUP.md, "command-5 emergency-stop
    // investigation" for the original single-owner-Serial fix, still fully
    // in effect below): 'k' used to be checked before anything else,
    // unconditionally, even mid-word -- but that meant a 'k' that is
    // legitimately part of an in-progress multi-character command (e.g.
    // "musicmotor peakthreshold 0.65", which contains a 'k' inside "peak")
    // fired emergency-stop mid-type, corrupting the command AND stopping
    // the motor for no reason the user intended. 'k' is now checked in
    // exactly the same position every OTHER reserved single-char command
    // already occupied ('f', '0'-'7', '?') -- i.e. it only acts as
    // emergency-stop when no line is currently pending (see
    // isSerialLinePending() immediately below). This does not weaken 'k':
    // a standalone 'k', or a 'k' typed before any word has started
    // accumulating, is completely unaffected -- both are still serviced
    // the instant the byte is read, with no Enter required. It also still
    // fires while the LED-map tool owns the strip (see isLedMapActive()
    // below), since that branch only runs once isSerialLinePending() is
    // confirmed false. What changes is only the specific case of 'k'
    // appearing as a character WITHIN an already-in-progress line -- that
    // now belongs to the line, exactly like the 'f' inside "effects"
    // already did (see the isSerialLinePending() comment below).
    if (isSerialLinePending()) {
      // If Controls.cpp is mid-word (e.g. typing "effects" or "musicmotor
      // peakthreshold 0.65"), every byte -- including 'k' -- belongs to
      // that line until '\n' terminates it, even if it would otherwise
      // match a reserved single-char command below. This restores a
      // property the original peek-based interceptor had by accident (it
      // gave up entirely, handing off the *rest* of a burst to
      // Controls.cpp, the moment it saw the first non-reserved byte) and
      // that a naive "check every byte independently" version of this
      // dispatcher lost: without it, "effects" broke, because its second
      // character is 'f' -- a reserved byte -- and got intercepted as a
      // motor-forward command instead of being forwarded, corrupting the
      // word into "eects". There's still only one Serial.read() call site
      // (this one), so this doesn't reintroduce the two-consumer race.
      feedSerialByte((char)c);
      continue;
    }

    if (c == 'k' || c == 'K') {
      g_interceptorKSeen++;
      triggerEmergencyStop();
      continue;
    }

    // While the LED index mapping tool owns the strip (INDEX_WALK phase
    // only -- see isLedMapActive()/handleLedMapKey() above), n/p/j/r/c/x
    // control it instead of their normal Controls.cpp meaning: '2'/'3'/'5'
    // already refuse to start while it's active, and forwarding unrelated
    // bytes to Controls.cpp while the mapping tool has exclusive LED
    // control would be confusing. Every other byte is simply ignored here
    // (not forwarded) for the same reason. isSerialLinePending() is always
    // false whenever this tool is active (every byte while it's active is
    // consumed here, never handed to feedSerialByte()), so moving the 'k'
    // check above this one doesn't change 'k''s ability to interrupt it.
    if (isLedMapActive()) {
      handleLedMapKey((char)c);
      continue;
    }

    if (c == 'f' || c == 'F') {
      motorForward();
      Serial.println(F("[MOTOR] Forward"));
    }
#if ENABLE_MOTOR_BEHAVIOR_TEST
    else if (c == '0') {
      setMotorBehavior(MotorBehaviorMode::OFF);
      Serial.println(F("[MOTOR BEHAVIOR] OFF"));
    } else if (c == '1') {
      setMotorBehavior(MotorBehaviorMode::IDLE_SWAY);
      Serial.println(F("[MOTOR BEHAVIOR] IDLE_SWAY"));
    } else if (c == '2') {
      startPriorityTest();
    } else if (c == '3') {
      startBreakawayTest();
    } else if (c == '4') {
      cycleMotorDimTestLevel();
    } else if (c == '5') {
      startMotorLedTest();
    } else if (c == '6') {
      startLedMap();
    } else if (c == '7') {
      setAudioLogEnabled(!isAudioLogEnabled());
    } else if (c == '?') {
      printMotorBehaviorDebugState();
      printMotorPriorityDebugState();
      printMotorLedPowerDebugState();
      Serial.printf("[MOTOR PRIORITY TEST] phase=%s\n", priorityTestPhaseName(priorityTestPhase));
      Serial.printf("[MOTOR BREAKAWAY] active=%d phase=%s cycle=%d elapsedMs=%lu\n",
                    breakawayPhase != BreakawayPhase::IDLE ? 1 : 0, breakawayPhaseName(breakawayPhase),
                    breakawayCycle, (unsigned long)(millis() - breakawayPhaseStartMs));
      Serial.printf("[MOTOR+LED TEST] active=%d phase=%d\n", motorLedTestPhase != MotorLedTestPhase::IDLE ? 1 : 0,
                    (int)motorLedTestPhase);
      Serial.printf("[LED MAP] active=%d phase=%d index=%u\n", isLedMapActive() ? 1 : 0, (int)ledMapPhase,
                    ledMapCurrentIndex);
      Serial.printf("[LED CONFIG] NUM_LEDS=%d PHYSICAL_LED_COUNT=%u row1=[%u,%u) row2=[%u,%u) row3=[%u,%u)\n", NUM_LEDS,
                    PHYSICAL_LED_COUNT, LED_ROW_1.start, LED_ROW_1.start + LED_ROW_1.count, LED_ROW_2.start,
                    LED_ROW_2.start + LED_ROW_2.count, LED_ROW_3.start, LED_ROW_3.start + LED_ROW_3.count);
      // Task 1 (audio serial-output toggle): processing/overlay/logging
      // are three separate concepts -- see AudioAnalyzer.h's
      // setAudioLogEnabled() comment.
      Serial.printf("[AUDIO STATUS] micReady=%d processingSuspended=%d overlayEnabled=%d logEnabled=%d\n",
                    isMicReady() ? 1 : 0, isAudioProcessingSuspended() ? 1 : 0, isAudioOverlayEnabled() ? 1 : 0,
                    isAudioLogEnabled() ? 1 : 0);
      printExpressiveMotionDebugState();
      printBehaviorStatus();
      printSharedI2SStatus();
      printSpeakerTestStatus();
      // Lightweight, permanent diagnostics (see the dispatcher's own
      // comment above) -- cheap counters/max-trackers, not per-frame
      // prints, so they don't perturb the timing they observe.
      Serial.printf("[DEBUG SERIAL] interceptorBytesSeen=%lu interceptorKSeen=%lu maxShowUs=%lu maxLoopUs=%lu\n",
                    (unsigned long)g_interceptorTotalBytesSeen, (unsigned long)g_interceptorKSeen,
                    (unsigned long)g_maxShowUs, (unsigned long)g_maxLoopUs);
    }
#endif
    else {
      feedSerialByte((char)c);  // not one of ours -- forward to Controls.cpp's line parser
    }
  }
}

void loop() {
  unsigned long loopStartUs = micros();  // lightweight diagnostic -- see g_maxLoopUs / pollSerialDispatcher()'s comment
  unsigned long now = millis();

  pollSerialDispatcher();       // the single Serial owner -- see its own comment for why
  updateMotorPowerGuard();     // non-blocking; must tick every iteration regardless of MotorBehavior mode
  updateMotorPriorityMode();   // non-blocking; must tick every iteration regardless of test state
  updatePriorityTest();        // non-blocking; no-op when the priority test isn't running
  updateBreakawayTest();       // non-blocking; no-op when the breakaway test isn't running
  updateMotorLedTest();        // non-blocking; no-op when the motor+LED coexistence test isn't running
  updateLedMap();              // non-blocking; no-op when the LED index mapping tool isn't running
  updateMotorPwmCalibration(); // non-blocking; no-op when the manual PWM calibration test isn't running
  // Expressive motion never runs concurrently with a motor/LED diagnostic
  // -- pauseExpressiveMotion() releases the motor/MotorPowerGuard
  // immediately if it happened to be mid-pulse and holds it there
  // (re-rolling fresh idle timing) for as long as a diagnostic is active,
  // matching every diagnostic's own isExpressiveMotionMoving() guard.
  if (isAnyMotorDiagnosticActive()) {
    pauseExpressiveMotion();
  } else {
    updateExpressiveMotion(now);  // non-blocking; no-op while ExpressiveMotionMode::OFF
  }
  // Always runs, even during a diagnostic: it only ever calls
  // ExpressiveMotion's own requestExpressivePattern(), which self-refuses
  // while any diagnostic is active (the Behavior Engine's scheduler simply
  // retries a little later -- see BEHAVIOR_MOVEMENT_RETRY_MS) -- no
  // separate pause/skip gate is needed here.
  updateBehaviorEngine(now, getAudioFeatures());  // non-blocking; no-op while BehaviorState::MANUAL
  updateMotorBehavior();  // non-blocking; no-op when OFF
  updateControls(now);   // buttons -- non-blocking, always runs, even during MotorPriorityMode, so
                          // buttons/emergency-stop stay live (Task 3 requirement); serial is handled
                          // above by pollSerialDispatcher(), not by updateControls() itself anymore
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
  // Reads AudioAnalyzer's AudioFeatures (no second mic pipeline) -- placed
  // right after updateAudioAnalyzer() so it sees the freshest values each
  // iteration. Non-blocking; no-op while DISABLED. Mutual exclusion with
  // every other motor behavior/diagnostic is enforced at 'danceon' and by
  // isAnyMotorDiagnosticActive() above, so this never needs a pause gate
  // the way ExpressiveMotion does -- nothing else can become active while
  // DanceEngine owns the motor. Superseded by MusicMotorController --
  // compiled out entirely unless ENABLE_LEGACY_DANCE_ENGINE is set.
#if ENABLE_LEGACY_DANCE_ENGINE
  updateDanceEngine(now);
#endif
  // Same reasoning as updateDanceEngine() above -- reads AudioAnalyzer
  // directly, mutual exclusion enforced at 'musicmotor on' and by
  // isAnyMotorDiagnosticActive(), no pause gate needed.
  updateMusicMotorController(now);
  // Same suspension window as the microphone above -- minimizes system
  // load during MotorPriorityMode's boot-equivalent timing test. Harmless
  // either way: tx_desc_auto_clear keeps transmitting silence on the
  // shared I2S bus even while this call is skipped.
  if (!isAudioProcessingSuspended()) {
    updateSpeakerTest(now);
  }

  if (now - lastFrameTime < FRAME_INTERVAL_MS) {
    unsigned long loopUs = micros() - loopStartUs;
    if (loopUs > g_maxLoopUs) g_maxLoopUs = loopUs;
    return;
  }
  lastFrameTime = now;

  // Service serial once more immediately before preparing the LED frame --
  // defense in depth on top of the single-owner dispatcher fix above (see
  // its comment): shrinks the worst-case latency between a byte arriving
  // and being serviced even further, in particular for 'k', which acts
  // through the latch (serviceEmergencyStop()) regardless of when it's
  // observed.
  pollSerialDispatcher();

  // Rendering priority:
  //   0. LED map active -> owns `strip` directly this frame (see
  //      updateLedMap() above); skip the normal pipeline entirely so
  //      there's never more than one strip.show() owner in a frame.
  //   1. muted            -> black (cue suppressed entirely, per spec)
  //   2. visual cue active -> cue frame, at its own fixed brightness cap
  //   3. otherwise        -> base effect + selected overlay, at user brightness
  // Base effects are time-derived (phase = f(now)) and any persistent
  // overlay/effect state (ripples, sparks, fireflies) ages off stored
  // timestamps, so skipping their render call while muted or mid-cue
  // never corrupts or resets anything -- it just isn't computed for
  // frames nobody would see anyway.
  if (isLedMapActive()) {
    // Intentionally empty: updateLedMap() (called above) already owns
    // every strip.setPixelColor()/show() call for this frame.
  } else if (isMuted()) {
    if (!wasMuted) {
      strip.clear();
      strip.show();
      wasMuted = true;
    }
    // else: already blanked: skip touching the strip entirely this frame.
  } else {
    wasMuted = false;

    // Experimental motor+LED coexistence test (see MotorPowerGuard's
    // MotorLedPowerMode / docs/DRV8833_MOTOR_BRINGUP.md): while active,
    // the base effect keeps rendering completely normally below -- only
    // the audio overlay (highest-current, least predictable component) is
    // suppressed, and the final brightness is substituted below. Nothing
    // here is frozen, replaced, or reset.
    bool dimActive = isDimRenderActive();

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

      if (overlay != AudioOverlay::OFF && !dimActive) applyAudioOverlay(overlay, frameBuffer, audio, now);
    }

    uint8_t brightnessRaw =
        cueActive ? VISUAL_CUE_BRIGHTNESS_RAW : (dimActive ? getMotorDimBrightnessRaw() : getBrightnessRaw());
    float brightnessScale = brightnessRaw / 255.0f;
    for (int i = 0; i < NUM_LEDS; i++) scaleClamp(frameBuffer[i], brightnessScale);
    applyPowerLimit(frameBuffer);

    for (int i = 0; i < NUM_LEDS; i++) {
      strip.setPixelColor(i, strip.Color(frameBuffer[i].r, frameBuffer[i].g, frameBuffer[i].b));
    }
    pollSerialDispatcher();  // service immediately before strip.show() -- see TASK 4 comment above
    unsigned long showStartUs = micros();
    strip.show(); // exactly one show() per rendered frame
    unsigned long showUs = micros() - showStartUs;
    if (showUs > g_maxShowUs) g_maxShowUs = showUs;
    pollSerialDispatcher();  // and immediately after -- see TASK 4 comment above
  }

  frameCounter++;
  if (now - lastFpsReportTime >= 1000) {
    g_measuredFps = frameCounter * 1000.0f / (now - lastFpsReportTime);
    frameCounter = 0;
    lastFpsReportTime = now;
  }

  unsigned long loopUs = micros() - loopStartUs;
  if (loopUs > g_maxLoopUs) g_maxLoopUs = loopUs;
}
