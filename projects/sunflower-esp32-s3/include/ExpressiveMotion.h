#pragma once
#include <stdint.h>
// Non-blocking expressive-motion controller: coordinates gentle idle
// motion and audio-reactive motion using MotorDriver (via direct
// motorForward()/motorReverse()/motorStop() calls, the same pattern
// already used by every motor diagnostic in main.cpp) and
// MotorPowerGuard's DIM_DURING_MOTION mode for LED coexistence. Never
// touches GPIO8/GPIO9 directly -- MotorDriver remains the sole owner.
// Disabled by default; the user must explicitly enable it. See
// docs/EXPRESSIVE_MOTION_DEVELOPMENT.md for the full design writeup.
//
// Development-branch feature (feature/expressive-motion-v1) -- does not
// touch the v1.0.0-tagged baseline's own commands/behavior.

enum class ExpressiveMotionMode {
  OFF,
  IDLE_ALIVE,
  AUDIO_REACTIVE,
};

// Internal movement-pulse phase, exposed read-only for the '?' status
// command and for external mutual-exclusion checks.
enum class ExpressiveMotionPhase {
  IDLE,       // resting; counting down to the next eligible movement
  PREPARING,  // MotorPowerGuard requested, waiting for READY
  MOVING,     // motor energized -- executing a MOVE step of the current pattern
  STOPPING,   // motor stopped -- executing a STOP step of the current pattern
  RELEASING,  // motor stopped, MotorPowerGuard settle/restore in progress
};

// A named, non-blocking sequence of motor pulses/stops/direction changes
// (see ExpressiveMotion.cpp's PatternStep tables) -- internal to this
// module; nothing outside ExpressiveMotion.cpp branches on which pattern
// is running, only on ExpressiveMotionPhase above. See
// docs/EXPRESSIVE_MOTION_DEVELOPMENT.md for what each one does and why.
enum class ExpressivePattern {
  NONE,
  GENTLE_SWAY,
  MEDIUM_SWAY,
  LONG_LEAN,
  DOUBLE_TWITCH,
  FORWARD_REVERSE_NOD,
  EXCITED_TRIPLE,
  DRAMATIC_SWEEP,
  AUDIO_ACTIVE_PULSE,
  AUDIO_STRONG_BURST,
  AUDIO_CLAP_RECOIL,
  SETTLE,
};

// A separate, small state machine for 'motion demo' (see
// docs/EXPRESSIVE_MOTION_DEVELOPMENT.md) -- exposed for '?' and mutual
// exclusion with the movement-pulse machine above (only one may run at a
// time; the demo takes priority and pauses ordinary idle/audio movement
// for its duration). Walks a fixed sequence of ExpressivePatterns (see
// ExpressiveMotion.cpp's DEMO_SEQUENCE), reusing the same per-step engine
// ordinary movement uses, with a visible pause between each demonstrated
// pattern.
enum class MotionDemoPhase {
  IDLE,
  PREPARING,
  RUNNING_PATTERN,       // executing the current step of the current demo pattern
  INTER_PATTERN_PAUSE,   // safe stop interval between demonstrated patterns
  RELEASING,
};

enum class AudioActivityBand {
  QUIET,
  ACTIVE,
  STRONG,
};

// Implemented in main.cpp, which has visibility into every motor/LED
// diagnostic's phase state ('2'/'3'/'5'/'6'). True while any of them is
// active. ExpressiveMotion checks this before starting any movement or
//'motion demo', and main.cpp's own diagnostic start-guards check
// isExpressiveMotionMoving() (above) right back -- the same bidirectional
// mutual-exclusion pattern already used among the four diagnostics
// themselves.
bool isAnyMotorDiagnosticActive();

// Implemented in main.cpp, right beside isAnyMotorDiagnosticActive() above
// -- mirrors its exact membership so the two never drift apart. Returns the
// display name of whichever motor-owning diagnostic/behavior is currently
// blocking a new activation (e.g. "MotorPwmCalibration"), or nullptr if
// isAnyMotorDiagnosticActive() would return false. Used by Controls.cpp's
// setUserAudioModeEnabled() to report which owner blocked an Audio Mode
// enable request.
const char *currentMotorOwnerName();

void initExpressiveMotion();

// Must be called every loop() iteration (main.cpp gates this: skipped,
// via pauseExpressiveMotion(), whenever any motor/LED diagnostic --
// '2'/'3'/'5'/'6' -- is active). Non-blocking; no delay().
void updateExpressiveMotion(unsigned long now);

// Cycles DISABLED -> IDLE_ALIVE -> AUDIO_REACTIVE -> DISABLED. Used by the
// 'motion'/'motion next' word command.
void cycleExpressiveMotionMode();
void setExpressiveMotionMode(ExpressiveMotionMode mode);
ExpressiveMotionMode getExpressiveMotionMode();

// True whenever expressive motion (ordinary movement OR 'motion demo') is
// actively holding/requesting motor power right now -- i.e. NOT simply
// "enabled but resting". main.cpp's diagnostic start-guards ('2'/'3'/'5'/
// '6') check this so they never start while expressive motion is mid-pulse.
bool isExpressiveMotionMoving();

// Freezes expressive motion's timers and, if it's mid-pulse, releases the
// motor/MotorPowerGuard immediately and returns to a safe idle state --
// called by main.cpp every iteration a motor/LED diagnostic is active, so
// expressive motion never fights one of them for the motor. Idempotent.
// Does NOT change the selected mode (unlike the emergency-stop path
// below) -- ordinary movement resumes once the diagnostic ends.
void pauseExpressiveMotion();

// Emergency-stop handling (called from main.cpp's serviceEmergencyStop()):
// stops the motor and releases MotorPowerGuard immediately if mid-pulse,
// cancels 'motion demo' if running, and forces the mode to DISABLED --
// expressive movement must stay inhibited until explicitly re-enabled,
// unlike the diagnostics above (which simply return to IDLE, ready for
// the next explicit command). Idempotent.
void cancelExpressiveMotion();

// 'motion demo' -- see docs/EXPRESSIVE_MOTION_DEVELOPMENT.md TASK 10.
// Refuses to start while any motor/LED diagnostic ('2'/'3'/'5'/'6') is
// active, or while a demo is already running. Saves the current
// ExpressiveMotionMode and restores it on completion (not on emergency
// cancel -- see cancelExpressiveMotion() above).
void startMotionDemo();
bool isMotionDemoActive();

// Requests a specific pattern to run immediately -- for use by a
// higher-level coordinator (see include/BehaviorEngine.h) that wants to
// choose WHICH pattern plays rather than letting IDLE_ALIVE/AUDIO_REACTIVE's
// own internal selection choose. Runs through the exact same safety-checked
// step engine as any other pattern (MotorPowerGuard coordination, the
// MOTION_MAX_ENERGIZED_MS backstop, mandatory stops between direction
// changes) -- BehaviorEngine never touches the motor or MotorPowerGuard
// directly. Works independently of the currently selected
// ExpressiveMotionMode -- in particular, it works while mode==OFF -- so a
// caller driving its own movement schedule never has to fight mode's own
// automatic idle-timer/audio-trigger selection for the same pattern engine.
// Returns false (refused outright, never queued) if a pattern is already
// running, 'motion demo' is active, a motor diagnostic ('2'/'3'/'5'/'6') is
// active, or IDLE_SWAY is currently selected -- callers should retry later
// rather than assume the request queues.
bool requestExpressivePattern(ExpressivePattern pattern);

// For the '?' status command.
void printExpressiveMotionDebugState();
