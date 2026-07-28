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
  MOVING,     // motor energized in motionDirection
  STOPPING,   // brief stop after a pulse (may lead into a second "curious" pulse)
  RELEASING,  // motor stopped, MotorPowerGuard settle/restore in progress
};

// A separate, small state machine for 'motion demo' (see TASK 10 in
// docs/EXPRESSIVE_MOTION_DEVELOPMENT.md) -- exposed for '?' and mutual
// exclusion with the movement-pulse machine above (only one may run at a
// time; the demo takes priority and pauses ordinary idle/audio movement
// for its short duration).
enum class MotionDemoPhase {
  IDLE,
  PREPARING,
  FORWARD,
  STOP1,
  REVERSE,
  STOP2,
  CURIOUS_PULSE,
  CURIOUS_STOP,
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

// For the '?' status command.
void printExpressiveMotionDebugState();
