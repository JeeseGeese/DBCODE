#pragma once
// Dance Engine V1 -- non-blocking, microphone-driven PWM dancing.
//
//   AudioAnalyzer -> DanceEngine -> MotorDriver / PWM motor primitives
//
// DanceEngine decides target speed, direction, hold duration, when to
// reverse, ramp rate, and when to rest; MotorDriver remains responsible
// only for executing electrical motor commands (see MotorDriver.h's
// motorPWM*()/initMotorPWM()/deinitMotorPWM(), reused here unchanged --
// this module never sets up LEDC itself). Reads only AudioAnalyzer's
// existing AudioFeatures (see include/AudioAnalyzer.h) -- no second
// microphone-analysis pipeline, and never touches I2S/SharedI2S directly.
// Responds to microphone-analysis data regardless of whether the sound
// came from the onboard speaker or the environment -- it has no knowledge
// of SpeakerTest at all.
//
// Disabled by default; the user must explicitly send 'danceon'. See
// Config.h's "Dance Engine" section for every tunable constant.
//
// LED power handling: DanceEngine does not read or write LED mute state in
// any way, at any point (startup, ramping, sustained movement, or
// reversal) -- it never calls Controls.h's isMuted()/setMuted() and never
// calls MotorPowerGuard's requestMotorPower()/releaseMotorPower() either.
// LEDs are left exactly as the user set them for the entire dance session.
// An earlier revision added a brief (~150ms) DanceEngine-owned mute window
// around startup/reversal to reduce simultaneous motor+LED current draw;
// it was removed by explicit request -- see the DRV8833/LED shared-supply
// note in the README's Power section for the underlying hardware caveat
// this leaves unmanaged. MotorPowerGuard itself is completely unmodified
// and still used exactly as before by IDLE_SWAY, ExpressiveMotion, and the
// motor+LED diagnostics (their own protection is untouched).

#include <stdint.h>

// Superseded as of the Sunny Rev 10.1 architecture: MusicMotorController
// (see MusicMotorController.h) is now Sunny's sole production music-driven
// dancing engine. DanceEngine is retained ONLY for historical reference and
// rollback safety during the MusicMotorController physical-button
// integration's validation window -- see CURRENT_STATUS.md's "DanceEngine
// removal checklist." Leave at 0 for every normal build: initDanceEngine()/
// updateDanceEngine() are never called (see main.cpp), the dance*/danceon/
// danceoff serial commands and their help-text entry are unavailable (see
// Controls.cpp), and every function below becomes a cheap no-op/false stub
// (see DanceEngine.cpp) -- no initialization, no per-loop work, no motor-
// ownership claim. Flip to 1 only to temporarily bring the legacy engine
// back as a manual serial-only diagnostic for comparison/rollback.
#define ENABLE_LEGACY_DANCE_ENGINE 0

enum class DanceState {
  OFF,
  RESTING,
  STARTING,
  MOVING_FORWARD,
  MOVING_REVERSE,
  RAMPING_DOWN,
  COASTING_FOR_REVERSE,
};

enum class DanceDirection { FORWARD, REVERSE };

// Resets all state (OFF, no PWM attached). Call once during setup(),
// after initMotor()/initMotorPwmCalibration().
void initDanceEngine();

// Advances kick/ramp/hold/silence/reversal timing and, while enabled,
// the microphone-energy smoothing and (if running) the deterministic test
// sequence. Non-blocking; no delay() anywhere in this module. Must be
// called every loop() iteration -- energy smoothing and dancetest step
// timing update every call, while the motor-output/decision tick is
// internally rate-limited to ~DANCE_PWM_UPDATE_MS (see Config.h).
void updateDanceEngine(unsigned long now);

// True whenever DanceEngine currently owns the motor -- i.e. enabled
// (any state except OFF), including while quietly RESTING. Consulted
// by main.cpp's isAnyMotorDiagnosticActive() (which every other motor
// behavior/diagnostic already checks), so nothing else can start while
// DanceEngine is enabled, and DanceEngine itself refuses to enable while
// anything else owns the motor -- the same bidirectional mutual-exclusion
// pattern already used among the existing diagnostics.
bool isDanceEngineActive();

// --- Serial-facing commands (see Controls.cpp's dispatchCommand()) ---

// 'danceon': enables live microphone-driven dancing. Refused if another
// motor diagnostic, MotorPwmCalibration test, or expressive motion
// currently owns the motor. Preempts IDLE_SWAY (setMotorBehavior(OFF)) and,
// if ExpressiveMotionMode::AUDIO_REACTIVE is currently selected, turns it
// off first -- DanceEngine is the single audio-to-motor behavior path;
// nothing else should also be trying to drive the motor from microphone
// input at the same time.
void danceEngineEnable();

// 'danceoff': coasts the motor immediately, detaches PWM (GPIO8/GPIO9
// return to plain digital LOW via MotorDriver's deinitMotorPWM(), so no
// stale digitalWrite-based command can unexpectedly resume), and cancels
// every pending kick/ramp/hold/reversal/test. Never touches LED state.
// Does not automatically resume any previous behavior -- DanceEngine (and
// everything it preempted) only resumes when explicitly re-enabled/
// re-selected.
void danceEngineDisable();

// 'dancestatus': prints full status -- see DanceEngine.cpp for the exact
// field list.
void danceEnginePrintStatus();

// 'dancetest': starts the deterministic, non-blocking simulated-energy
// sequence (silence -> low -> medium -> high -> peak+strong transient ->
// medium -> silence) -- see DanceEngine.cpp for the exact step table.
// Auto-enables DanceEngine first if it is currently disabled (same refusal
// rules as danceon). Simulated values are fed through the exact same
// energy-smoothing/reversal-decision path live audio uses -- this never
// bypasses DanceEngine logic with direct canned motor commands.
void danceEngineStartTest();

// 'dancetestoff': cancels the deterministic test sequence (or a
// dancequiet/mid/high/peak override) and returns to live microphone input,
// if DanceEngine remains enabled. Does not disable DanceEngine itself.
void danceEngineStopTest();

// 'dancequiet'/'dancemid'/'dancehigh'/'dancepeak': temporarily override the
// energy input with a fixed simulated value (quiet/mid/high bands; peak
// additionally injects a one-shot strong transient, e.g. to demonstrate a
// reversal on demand). Each auto-enables DanceEngine first if disabled.
void danceEngineSimQuiet();
void danceEngineSimMid();
void danceEngineSimHigh();
void danceEngineSimPeak();

// Called from main.cpp's serviceEmergencyStop() ('k') and from the manual
// motor-command system's 'mstop' handler (Controls.cpp) -- immediately
// coasts, detaches PWM, and disables DanceEngine. Idempotent/silent no-op
// if not currently enabled. DanceEngine does NOT automatically re-enable
// after this; it stays OFF until an explicit 'danceon'.
void cancelDanceEngine();
