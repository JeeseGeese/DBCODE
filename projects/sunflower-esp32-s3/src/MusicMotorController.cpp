#include "MusicMotorController.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "AudioAnalyzer.h"
#include "Config.h"
#include "DanceEngine.h"       // isDanceEngineActive()/cancelDanceEngine() -- a choreographed dance and
                                // music-reactive movement are never allowed to drive the motor at once
#include "ExpressiveMotion.h"  // isAnyMotorDiagnosticActive()/isExpressiveMotionMoving() -- implemented in
                                // main.cpp, declared here per the existing project convention (see
                                // DanceEngine.cpp/MotorPwmCalibration.cpp for the same pattern); also
                                // getExpressiveMotionMode()/setExpressiveMotionMode() so enabling this
                                // controller can turn off the old AUDIO_REACTIVE motor pulses.
#include "MotorBehavior.h"     // setMotorBehavior(OFF) -- preempts IDLE_SWAY, matching every other owner
#include "MotorDriver.h"       // motorPWM*()/initMotorPWM()/deinitMotorPWM() -- the only pin-touching layer

// NOTE: deliberately no Controls.h / MotorPowerGuard.h include -- this module
// never reads or writes LED mute state and never requests MotorPowerGuard,
// by explicit design (see the header comment and DanceEngine's own
// physically-validated LED-coexistence result).

namespace {

float sanitizeFloat(float v, float fallback, float lo, float hi) {
  if (isnan(v) || isinf(v)) return fallback;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// The ONE place a 0-100 percent (the "M" scale already used by
// MotorPwmCalibration/DanceEngine) converts to raw 8-bit PWM duty (0-255) --
// never duplicated elsewhere in this file.
uint8_t percentToMotorPwm(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)((uint16_t)percent * 255 / 100);
}

uint32_t randomRangeU32(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

const char *dirName(MusicMotorDirection d) { return d == MusicMotorDirection::FORWARD ? "FORWARD" : "REVERSE"; }
const char *dirLetter(MusicMotorDirection d) { return d == MusicMotorDirection::FORWARD ? "F" : "R"; }
MusicMotorDirection oppositeOf(MusicMotorDirection d) {
  return d == MusicMotorDirection::FORWARD ? MusicMotorDirection::REVERSE : MusicMotorDirection::FORWARD;
}

const char *stateName(MusicMotorState s) {
  switch (s) {
    case MusicMotorState::OFF: return "DISABLED";
    case MusicMotorState::SILENT: return "SILENT";
    case MusicMotorState::INTENSITY_SWAY: return "INTENSITY_SWAY";
    case MusicMotorState::BASS_ACCENT: return "BASS_ACCENT";
    case MusicMotorState::HIP_SHAKE: return "HIP_SHAKE";
    case MusicMotorState::REVERSE_HIP_SHAKE: return "REVERSE_HIP_SHAKE";
    case MusicMotorState::EXTENDED_SPIN: return "EXTENDED_SPIN";
    case MusicMotorState::SUSTAINED_DRIVE: return "SUSTAINED_DRIVE";
    case MusicMotorState::DECELERATING: return "DECELERATING";
    case MusicMotorState::MUSICAL_RAMP_DOWN: return "MUSICAL_RAMP_DOWN";
  }
  return "UNKNOWN";
}

// Matches the MusicIntensityBand enum's own BAND_* names verbatim in all
// diagnostics -- distinguishes these band-identity strings at a glance from
// plain state/direction names elsewhere in the same log line.
const char *intensityBandName(MusicIntensityBand b) {
  switch (b) {
    case MusicIntensityBand::BAND_QUIET: return "BAND_QUIET";
    case MusicIntensityBand::BAND_LOW: return "BAND_LOW";
    case MusicIntensityBand::BAND_MEDIUM: return "BAND_MEDIUM";
    case MusicIntensityBand::BAND_HIGH: return "BAND_HIGH";
    case MusicIntensityBand::BAND_PEAK: return "BAND_PEAK";
  }
  return "UNKNOWN";
}

const char *beatActionName(MusicMotorBeatAction a) {
  switch (a) {
    case MusicMotorBeatAction::NONE: return "NONE";
    case MusicMotorBeatAction::ACCENT_CURRENT_DIRECTION: return "ACCENT_CURRENT_DIRECTION";
    case MusicMotorBeatAction::REVERSE_DIRECTION: return "REVERSE";
    case MusicMotorBeatAction::START_HIP_SHAKE: return "HIP_SHAKE";
    case MusicMotorBeatAction::START_REVERSE_HIP_SHAKE: return "REVERSE_HIP_SHAKE";
    case MusicMotorBeatAction::START_EXTENDED_SPIN: return "EXTENDED_SPIN";
    case MusicMotorBeatAction::START_SUSTAINED_DRIVE: return "SUSTAINED_DRIVE";
  }
  return "UNKNOWN";
}

enum class ReversalRejectReason { NONE, HOLD_TIME, COOLDOWN, ALREADY_REVERSING, POST_SPIN_HOLD };
const char *reasonName(ReversalRejectReason r) {
  switch (r) {
    case ReversalRejectReason::NONE: return "none";
    case ReversalRejectReason::HOLD_TIME: return "minimum direction-hold time not yet elapsed";
    case ReversalRejectReason::COOLDOWN: return "reversal cooldown still active";
    case ReversalRejectReason::ALREADY_REVERSING: return "already coasting into a reversal";
    case ReversalRejectReason::POST_SPIN_HOLD: return "post-spin direction hold still active";
  }
  return "unknown";
}

// ============================================================================
// State
// ============================================================================
MusicMotorState state = MusicMotorState::OFF;
bool pwmReady = false;

MusicMotorDirection currentDirection = MusicMotorDirection::FORWARD;
MusicMotorDirection pendingDirection = MusicMotorDirection::FORWARD;
bool coastingForReversal = false;
unsigned long coastEndMs = 0;

uint8_t currentSpeedPercent = 0;

unsigned long directionStartMs = 0;
unsigned long lastReversalMs = 0;

// --- INTENSITY_SWAY bookkeeping ---
unsigned long swayDeadlineMs = 0;   // periodic LOW-band-only reversal timer
bool accentActive = false;
unsigned long accentEndMs = 0;
uint8_t currentAccentPercent = 0;

// --- shared time-based ramp (used by BASS_ACCENT's accel, DECELERATING's
// decel, and EXTENDED_SPIN's accel -- only one is ever in flight at a time
// given the sequential state machine, see applyRampTick()) ---
uint8_t rampFromPercent = 0;
uint8_t rampToPercent = 0;
unsigned long rampStartMs = 0;
uint32_t rampDurationMs = 0;
bool accelRampStarted = false;  // meaningful only while state == BASS_ACCENT
bool spinRampStarted = false;   // meaningful only while state == EXTENDED_SPIN

// --- HIP_SHAKE bookkeeping ---
unsigned long hipShakeEnteredMs = 0;
unsigned long hipShakeDeadlineMs = 0;

// --- EXTENDED_SPIN bookkeeping ---
unsigned long spinEnteredMs = 0;
unsigned long spinDeadlineMs = 0;
unsigned long lastSpinMs = 0;     // for the spin cooldown
unsigned long lastSpinEndMs = 0;  // for the post-spin direction-hold gate

// --- Revision 7: SUSTAINED_DRIVE bookkeeping -- see MusicMotorController.h's
// and Config.h's own "Revision 7" comments for the full design. ---
MusicMotorDirection sustainedDriveDirection = MusicMotorDirection::FORWARD;
unsigned long sustainedDriveEnteredMs = 0;  // phrase start -- see the revision 8 "phrase" bookkeeping block below for sustainedDriveNextReviewMs (the revision 8 replacement for a fixed deadline)
uint8_t sustainedDriveTargetPercent = 0;  // live HIGH-tier-floored target, exposed for diagnostics only (see updateMusicMotorController())
// Consecutive-same-direction cap bookkeeping -- persists ACROSS entries
// (not reset per-entry), so "no more than
// MUSIC_MOTOR_SUSTAINED_DRIVE_MAX_CONSECUTIVE_SAME_DIRECTION consecutive
// same-direction drives" can actually be enforced.
MusicMotorDirection lastSustainedDriveDirection = MusicMotorDirection::FORWARD;
uint8_t consecutiveSustainedDriveSameDirectionCount = 0;
// Cooldown -- randomized once per EXIT (see exitSustainedDrive()); also
// seeded once at enable/reset (resetRuntimeState()) so the very first
// listening session doesn't start with zero cooldown "for free."
unsigned long sustainedDriveCooldownUntilMs = 0;
// Set by trySustainedDriveEntry() (a mutating helper called from
// selectBeatAction()), consumed by the caller entering it -- same handoff
// pattern as pendingReverseHipShakeHeavy.
MusicMotorDirection pendingSustainedDriveDirection = MusicMotorDirection::FORWARD;

// --- Revision 8: renewable performance-phrase bookkeeping -- see
// Config.h's own "Revision 8" comments and
// computeSustainedDriveContinuationDecision() below for how these are
// used. Distinct from the Revision 7 fields above: those describe the
// CURRENT direction segment / cross-phrase cap; these describe the PHRASE
// as a whole (which can outlive multiple direction segments via an
// in-phrase switch). ---
enum class SustainedDrivePhraseTier { SHORT, STANDARD, EXTENDED, RENEWABLE };
SustainedDrivePhraseTier sustainedDrivePhraseTier = SustainedDrivePhraseTier::STANDARD;
unsigned long sustainedDriveDirectionCommitStartMs = 0;  // when the CURRENT direction segment began (reset on entry AND on each in-phrase switch)
uint32_t sustainedDriveMinCommitmentMs = 0;               // tier-specific: MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS, or the SHORT tier's own rolled duration
unsigned long sustainedDriveNextReviewMs = 0;             // next review/promotion-check point
uint8_t sustainedDriveExtensionCount = 0;
uint32_t sustainedDriveLastExtensionMs = 0;
const char *sustainedDriveLastExtensionReason = "none";
unsigned long sustainedDriveLowEnergySinceMs = 0;         // 0 = effectiveBand() not currently below BAND_MEDIUM
unsigned long sustainedSwitchCooldownUntilMs = 0;         // ordinary cooldown between two in-phrase direction switches
uint8_t sustainedDriveSwitchCount = 0;                    // in-phrase switches this phrase (NOT the cross-phrase consecutive-direction count above)
const char *sustainedDriveExitReason = "none";            // last exit reason, for 'musicmotor status'
float previousSongEnergyForTrend = 0.0f;                  // sampled once per review -- see the energy-trend input to the continuation decision
unsigned long sustainedDrivePersistentEntryNextCheckMs = 0;  // rate limit for the persistent-energy (no-fresh-strong-hit) entry opportunity

// --- Revision 8: lifelike silence/stop bookkeeping -- see
// beginMusicalSilenceStop()/enterMusicalRampDown()/updateMusicalRampDown(). ---
enum class SilenceStopStyle { GRADUAL_RAMP_DOWN, DRAMATIC_ABRUPT_STOP };
SilenceStopStyle lastStopStyle = SilenceStopStyle::GRADUAL_RAMP_DOWN;
const char *lastStopStyleReason = "none";
unsigned long lastDropHoldActiveMs = 0;  // updated every tick dropHoldActive is true -- "was a drop recently active" for the sharp-cutoff stop-style tier

unsigned long belowSilenceThresholdSinceMs = 0;  // 0 = not currently in the QUIET band

// --- Revision 5: sustained (non-transient) drop-hold qualification -- 0
// means "not currently continuously in BAND_HIGH or BAND_PEAK"; otherwise
// the millis() timestamp the current continuous HIGH/PEAK dwell began. See
// Config.h's MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS comment. ---
unsigned long sustainedHighSinceMs = 0;

unsigned long lastTickMs = 0;
unsigned long lastDiagPrintMs = 0;

// --- sustained drop-hold (choreography-permission signal, never overwrites
// the real measured intensityBand -- see effectiveBand()) ---
bool dropHoldActive = false;
unsigned long dropHoldStartMs = 0;  // when the CURRENT continuous hold session began
unsigned long dropHoldUntilMs = 0;

// --- wobble cue (lightweight fastEnergy-delta detector, eligibility signal
// only -- see Config.h's "wobby tone-shift response" comment) ---
float previousFastEnergyForWobble = 0.0f;
bool wobbleCueActive = false;  // this-tick flag, like beatDetectedThisTick
unsigned long lastWobbleMs = 0;
uint8_t wobbleActionCounter = 0;  // deterministic (not random) wobble->action alternation

// --- shared hip-shake start cooldown (covers both HIP_SHAKE and
// REVERSE_HIP_SHAKE -- see Config.h's MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MIN/MAX_MS) ---
unsigned long lastHipShakeStartMs = 0;
uint32_t hipShakeStartCooldownMs = 0;

// --- REVERSE_HIP_SHAKE bookkeeping ---
MusicMotorDirection reverseHipShakeOriginalDirection = MusicMotorDirection::FORWARD;
bool reverseHipShakeHeavy = false;
uint8_t reverseHipShakePhaseIndex = 0;
uint8_t reverseHipShakeTotalPhases = 0;
uint32_t reverseHipShakePhaseDurationMs = 0;
uint8_t reverseHipShakeTargetPercent = 0;
unsigned long reverseHipShakePhaseStartMs = 0;
bool reverseHipShakeCoasting = false;  // brief coast at the start of each phase (DRV8833 safety, same as any other reversal)
unsigned long reverseHipShakeCoastEndMs = 0;

// --- extended-spin manual duration override (see musicMotorSetSpinTimeMs()/pickSpinProfile()) ---
bool spinTimeManualOverride = false;
uint32_t spinTimeManualOverrideMs = 0;

// --- audio pipeline: raw -> {fastEnergy, songEnergy} -> baselineEnergy ->
// transientDelta -- see Config.h's "sustained song-intensity tracking"
// comment. Three independently-smoothed signals, not one shared EMA. ---
float rawEnergy = 0.0f;
float fastEnergy = 0.0f;      // fast attack/moderate release -- individual beats/immediate volume
float songEnergy = 0.0f;      // slow attack/slow release -- sustained section intensity
float baselineEnergy = 0.0f;  // very slow EMA of fastEnergy -- "recent normal level" for transient detection
float transientDelta = 0.0f;  // max(0, fastEnergy - baselineEnergy)

// --- Revision 5: musical-momentum signal -- see Config.h's "performanceEnergy"
// comment. Fast attack toward songEnergy (plus one-shot bumps on qualifying
// beat/strongHit events), slow multi-second release. Read by effectiveBand()
// below, which lets it LEND a higher band (for both intensityTargetPercent
// and choreography-action eligibility) than the real measured intensityBand
// -- never the reverse; diagnostics always print the true intensityBand
// separately, exactly like the pre-existing dropHoldActive lending. ---
float performanceEnergy = 0.0f;
MusicIntensityBand previousPerformanceBand = MusicIntensityBand::BAND_QUIET;  // for logPerformanceEvaluation()'s edge detection

MusicIntensityBand intensityBand = MusicIntensityBand::BAND_QUIET;
uint8_t intensityTargetPercent = 0;  // continuously slewed "what normal movement should be right now"

bool beatDetectedThisTick = false;
bool strongHitDetectedThisTick = false;
// Revision 6: true only on the exact tick intensityBand itself just
// transitioned (set in updateMusicMotorController() right after band
// classification, before selectBeatAction() runs later in the same tick)
// -- a "phrase boundary" exemption for the rotation-commitment gate below.
bool bandTransitionedThisTick = false;
MusicMotorBeatAction lastSelectedAction = MusicMotorBeatAction::NONE;
bool pendingReverseHipShakeHeavy = false;  // variant decided by selectBeatAction(), consumed by the caller entering it
// Set at every return point in selectBeatAction()/the wobble-action branch
// -- stable grep-friendly label for the branch that actually fired, read by
// the 'musicmotor debug' choreography diagnostic (logChoreographySelection()).
const char *lastSelectionReason = "";

// Edge + cooldown gating, independent for each tier -- see Config.h's
// MUSIC_MOTOR_BEAT_COOLDOWN_MS/MUSIC_MOTOR_STRONG_HIT_COOLDOWN_MS comment.
bool beatCooldownArmed = true;
unsigned long lastBeatMs = 0;
bool strongHitCooldownArmed = true;
unsigned long lastStrongHitMs = 0;

// Deterministic (not random) per-band modular counters used by
// selectBeatAction() -- see its own comment for exactly how each is used.
uint8_t lowStrongHitCounter = 0;
uint8_t mediumStrongHitCounter = 0;
uint8_t highStrongHitCounter = 0;
uint8_t peakStrongHitCounter = 0;

bool lastStopWasEmergency = false;

// --- Revision 4: detailed decision diagnostics ('musicmotor debug
// on'/'off'). Persists across musicmotor on/off within a session (an
// operator who just turned it on for a listening pass shouldn't have to
// re-enable it after a stop/start) -- only reset to OFF at true boot, in
// initMusicMotorController(). Purely gates EXTRA logging; never read by any
// decision-making code path. ---
bool debugLoggingEnabled = false;

// Generic rate-limit: a repeated identical reason (same category, same
// string) within MUSIC_MOTOR_DEBUG_RATE_LIMIT_MS is suppressed; a reason
// CHANGE always prints immediately regardless of timing. Each diagnostic
// category (dropHold/band/strongHit/choreography) gets its own instance so
// they don't interfere with each other's suppression.
struct DebugRateLimit {
  const char *lastReason = "";
  unsigned long lastPrintMs = 0;
  bool everPrinted = false;
};
DebugRateLimit dropHoldRateLimit;
DebugRateLimit bandRateLimit;
DebugRateLimit strongHitRateLimit;
DebugRateLimit choreographyRateLimit;
DebugRateLimit performanceRateLimit;
DebugRateLimit sustainedDriveRateLimit;
DebugRateLimit sustainedDriveContinuationRateLimit;
DebugRateLimit sustainedSwitchRateLimit;

// significant==true bypasses rate limiting entirely (always prints) -- use
// for actual state changes (STARTED/REFRESHED/a band transition/a strong
// hit that qualified/a non-default action selection). significant==false
// (typically a REJECTED/default outcome) is deduped unless the reason
// string changed or the interval elapsed.
bool debugShouldPrint(DebugRateLimit &rl, const char *reason, unsigned long now, bool significant) {
  if (significant) {
    rl.lastReason = reason;
    rl.lastPrintMs = now;
    rl.everPrinted = true;
    return true;
  }
  bool changed = !rl.everPrinted || strcmp(rl.lastReason, reason) != 0;
  bool intervalElapsed = (now - rl.lastPrintMs) >= MUSIC_MOTOR_DEBUG_RATE_LIMIT_MS;
  if (changed || intervalElapsed) {
    rl.lastReason = reason;
    rl.lastPrintMs = now;
    rl.everPrinted = true;
    return true;
  }
  return false;
}

// --- Runtime-tunable copies of the Config.h defaults (see musicMotorSet*()
// below / the 'musicmotor <param> <value>' commands). Config.h's constants
// remain the values these are reseeded from on every
// initMusicMotorController()/musicMotorEnable(). ---
uint8_t tunableLowMinPercent = MUSIC_MOTOR_SLOW_PERCENT_MIN;
uint8_t tunableLowMaxPercent = MUSIC_MOTOR_SLOW_PERCENT_MAX;
uint8_t tunableFastPercent = MUSIC_MOTOR_FAST_PERCENT;
float tunableBeatThreshold = MUSIC_MOTOR_BEAT_DELTA_THRESHOLD;
float tunableStrongHitThreshold = MUSIC_MOTOR_STRONG_HIT_DELTA_THRESHOLD;
uint32_t tunableAccelMs = MUSIC_MOTOR_ACCEL_MS;
uint32_t tunableHoldMinMs = MUSIC_MOTOR_FAST_HOLD_MIN_MS;
uint32_t tunableHoldMaxMs = MUSIC_MOTOR_FAST_HOLD_MAX_MS;
uint32_t tunableDecelMs = MUSIC_MOTOR_DECEL_MS;
float tunableLowThreshold = MUSIC_MOTOR_LOW_THRESHOLD;
float tunableMediumThreshold = MUSIC_MOTOR_MEDIUM_THRESHOLD;
float tunableHighThreshold = MUSIC_MOTOR_HIGH_THRESHOLD;
float tunablePeakThreshold = MUSIC_MOTOR_PEAK_THRESHOLD;
uint32_t tunableSpinCooldownMs = MUSIC_MOTOR_SPIN_COOLDOWN_MS;
uint32_t tunableMinRotationHoldMs = MUSIC_MOTOR_MIN_ROTATION_HOLD_MS;
// Revision 10 -- drop-phrase reversal tuning ('musicmotor switchchance/
// switchcooldown/switchlimit').
uint8_t tunableSwitchChancePercent = MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT;
uint32_t tunableDropPhraseSequenceCooldownMs = MUSIC_MOTOR_DROP_PHRASE_SEQUENCE_COOLDOWN_MS;
uint8_t tunableDropPhraseSwitchLimit = MUSIC_MOTOR_DROP_PHRASE_MAX_SUSTAINED_REVERSALS_PER_DROP;

// ============================================================================
// Revision 9 -- relative/song-adaptive musical-section (EDM/dubstep "drop")
// recognition. See Config.h's own Revision 9 comment block for the full
// rationale; see this file's "Revision 9" function section (below
// chooseRampDownDurationMs()) for the pure decision functions this state
// feeds. ADDITIVE: every one of these globals only ever creates NEW
// opportunities/floors layered alongside Revision 7/8's own absolute-band
// machinery -- none of them are read by any Revision <=8 decision function.
// ============================================================================

// Master A/B toggle -- 'musicmotor dropdetect on/off'. While false, none of
// this section's tracking/phase-machine/entry-escalation code runs at all
// (see updateRelativeDropTracking()'s own early return), giving a clean
// Revision-8-only baseline on the SAME firmware image.
bool relativeDropDetectionEnabled = MUSIC_MOTOR_RELATIVE_DROP_ENABLED_DEFAULT;

// --- rolling references/density (see Config.h's own comments) ---
float bassFastEnergy = 0.0f;
float bassBaselineEnergy = 0.0f;
float bassImpactDelta = 0.0f;          // max(0, bassFastEnergy - bassBaselineEnergy), this tick
float buildupEnergyReference = 0.0f;   // slow EMA of songEnergy -- "recent section" yardstick
float longSongEnergyReference = 0.0f;  // very slow EMA of songEnergy -- "this song so far" yardstick
float beatDensityScore = 0.0f;
float transientDensityScore = 0.0f;
float bassDensityScore = 0.0f;

// --- this-tick signal contributions + combined confidence, retained for
// diagnostics ('musicmotor status', rate-limited evaluation logging) ---
float dropEnergyRiseScore = 0.0f;
float dropSectionContrastScore = 0.0f;
float dropBassImpactScore = 0.0f;
float dropBassDensityScore = 0.0f;
float dropBeatDensityScore = 0.0f;
float dropTransientDensityScore = 0.0f;
float dropBuildupResolutionScore = 0.0f;
float dropSustainedEnergyScore = 0.0f;
float dropConfidence = 0.0f;

enum class MusicalSectionPhase { NEUTRAL, BUILDUP, DROP_ARMED, DROP_IMPACT, DROP_ACTIVE, DROP_RELEASE };
enum class DropConfidenceTier { NONE, POSSIBLE_DROP, CONFIRMED_DROP, MAJOR_DROP };

MusicalSectionPhase musicalSectionPhase = MusicalSectionPhase::NEUTRAL;
DropConfidenceTier dropConfidenceTier = DropConfidenceTier::NONE;
unsigned long musicalSectionPhaseEnteredMs = 0;   // when the CURRENT phase began
unsigned long dropActiveSinceMs = 0;              // 0 = not currently DROP_ACTIVE
unsigned long dropBelowSustainFloorSinceMs = 0;   // 0 = confidence currently >= ACTIVE_SUSTAIN_FLOOR (meaningful only while DROP_ACTIVE)
unsigned long dropRefractoryUntilMs = 0;          // no new BUILDUP re-arm before this
bool sustainedPhraseStartedThisDrop = false;      // reset every time a NEW DROP_ACTIVE section begins
unsigned long dropEntryNextCheckMs = 0;           // rate limit for the escalating sustained-drive entry opportunity
const char *lastSustainedFloorTierName = "NORMAL";  // NORMAL/PERFORMANCE/PEAK -- last SUSTAINED_DRIVE speed-floor tier selected

DebugRateLimit dropSignalRateLimit;

// --- Revision 9 session summary counters ('musicmotor summary'; reset on
// every 'musicmotor on', same lifecycle as the rest of resetRuntimeState()) ---
unsigned long summaryBandMs[5] = {0, 0, 0, 0, 0};  // indexed by (int)MusicIntensityBand
uint32_t summaryStrongHitCount = 0;
uint32_t summaryDropHoldStartCount = 0;
uint32_t summaryRelativeDropActiveCount = 0;       // times phase entered DROP_ACTIVE
uint32_t summaryDropsWithNoSustainedDrive = 0;     // DROP_ACTIVE sections that released with no phrase ever started
uint32_t summarySustainedDriveEntryCount = 0;
uint32_t summarySustainedDriveExtensionCount = 0;
uint32_t summarySustainedDriveSwitchCount = 0;
unsigned long summaryPhraseDurationSumMs = 0;
uint32_t summaryPhraseCount = 0;
unsigned long summaryMaxPhraseDurationMs = 0;
uint8_t summaryMaxTargetPercent = 0;
uint8_t summaryMaxAppliedPercent = 0;
uint32_t summaryRelativeEntryRejectAlreadyActive = 0;
uint32_t summaryRelativeEntryRejectSilent = 0;
uint32_t summaryRelativeEntryRejectCooldown = 0;
uint32_t summaryRelativeEntryRejectRollFailed = 0;

// ============================================================================
// Revision 10 -- physical choreography and dynamic-range refinement. See
// Config.h's own "Revision 10" comment block for the full rationale.
// Layered alongside Revision 9's drop DETECTION (confidence/phase machine
// above, untouched) -- this section only changes what happens with a
// section once it's already classified.
// ============================================================================

// 'musicmotor quietmotion on/off' -- toggles QUIET_BUILDUP motion
// specifically (independent of the broader 'musicmotor dropdetect' A/B
// toggle, which gates ALL of Revision 9/10's relative-drop machinery).
bool quietBuildupMotionEnabled = true;

// --- speed-authority cap (bounded lending) ---
unsigned long measuredQuietSinceMs = 0;  // 0 = intensityBand not currently QUIET
uint8_t lastAuthorityCapPercent = 255;   // 255 = uncapped this tick, for diagnostics
const char *lastAuthorityCapSource = "none";

// --- motion tier (choreography role classification) ---
enum class MotionTier { REST, QUIET_BUILDUP, MELLOW, GROOVE, HIGH_ENERGY, CONFIRMED_DROP_DRIVE, MAJOR_DROP_DRIVE };
MotionTier currentMotionTier = MotionTier::REST;

// --- movement duty cycle (QUIET_BUILDUP/MELLOW pulse/rest choreography) ---
bool motionDutyPulseOn = false;
unsigned long motionDutyWindowEndMs = 0;
unsigned long lastQuietBuildupMotionMs = 0;  // for summary/diagnostics -- last time a quiet-buildup pulse fired

// --- Revision 10 session summary counters (added to the Revision 9 set) ---
unsigned long summaryMellowMotionMs = 0;
unsigned long summaryGrooveMotionMs = 0;
unsigned long summaryConfirmedDropDriveMs = 0;
unsigned long summaryMajorDropDriveMs = 0;
uint32_t summaryQuietBuildupMotionCount = 0;
unsigned long summaryQuietOverCapMs = 0;  // time measured QUIET while authority exceeded the quiet cap -- should be ~0
unsigned long summaryLowOverCapMs = 0;    // time measured LOW while authority exceeded the mellow cap -- should be ~0

// --- drop choreography phrase vocabulary ---
enum class DropPhraseType { FULL_SUSTAIN, SUSTAINED_REVERSAL, DROP_BOOTY_SHAKE, DROP_PUNCH_AND_HOLD, DOUBLE_PUNCH, SUSTAIN_WITH_ACCENTS };
constexpr uint8_t MAX_DROP_PHRASE_STEPS = 4;
// Plain aggregate (no in-class member initializers -- this toolchain
// predates C++17's aggregate-with-default-member-initializers relaxation,
// and every construction site below uses brace-init) -- zero-value-
// initialized (dropPhraseSteps[] below) reads as {isPunch=false,
// direction=FORWARD(0), targetPercent=0, durationMs=0}, i.e. a harmless
// terminal-sustain-at-FORWARD-M0 placeholder before the first real
// selection.
struct DropPhraseStep {
  bool isPunch;  // false = terminal open-ended sustain step
  MusicMotorDirection direction;
  uint8_t targetPercent;
  uint32_t durationMs;  // meaningful only when isPunch
};
DropPhraseStep dropPhraseSteps[MAX_DROP_PHRASE_STEPS] = {};
uint8_t dropPhraseStepCount = 0;
uint8_t dropPhraseStepIndex = 0;
DropPhraseType currentDropPhraseType = DropPhraseType::FULL_SUSTAIN;
const char *lastDropPhraseSelectionReason = "none";
enum class DropPhraseTransitionPhase { DRIVING, DECEL, COAST };
DropPhraseTransitionPhase dropPhraseTransitionPhase = DropPhraseTransitionPhase::DRIVING;
unsigned long dropPhraseStepDeadlineMs = 0;
unsigned long dropPhraseSequenceReadyMs = 0;  // cooldown gate -- a new non-FULL_SUSTAIN sequence may not begin before this
DropPhraseType lastDropPhraseUsed = DropPhraseType::FULL_SUSTAIN;
bool hasLastDropPhrase = false;
// Per-drop bookkeeping -- reset on DROP_RELEASE (see updateRelativeDropTracking()).
uint8_t dropPhraseBoothShakesThisDrop = 0;
uint8_t dropPhraseSustainedReversalsThisDrop = 0;
uint8_t dropPhraseTotalDirectionChangesThisDrop = 0;

// --- Revision 10 drop-phrase summary counters ---
uint32_t summaryFullSustainCount = 0;
uint32_t summarySustainedReversalCount = 0;
uint32_t summaryDropBootyShakeCount = 0;
uint32_t summaryDropPunchAndHoldCount = 0;
uint32_t summaryDoublePunchCount = 0;
uint32_t summarySustainWithAccentsCount = 0;
unsigned long summaryConfirmedDropDirectionChangesSum = 0;
uint32_t summaryConfirmedDropCountForAvg = 0;
unsigned long summaryMajorDropDirectionChangesSum = 0;
uint32_t summaryMajorDropCountForAvg = 0;
uint8_t summaryMaxDirectionChangesInOneDrop = 0;
uint32_t summaryDropsUsingMultiplePhrases = 0;
uint32_t summaryRepeatedIdenticalPhraseSelections = 0;
uint32_t summarySafetyBlockedReversalCount = 0;
uint8_t dropPhraseDistinctPhrasesThisDrop = 0;  // bitmask-free simple count via lastDropPhraseUsed comparison at DROP_RELEASE time
bool dropPhraseUsedMoreThanOneTypeThisDrop = false;

// --- Revision 10.1: SUSTAINED_DRIVE invariant detector/recovery ---
unsigned long dropPhraseInvariantViolationSinceMs = 0;  // 0 = not currently observing a violation
uint32_t summarySustainedDriveStoppedInvariantCount = 0;
uint32_t summaryDropPhraseRecoveryCount = 0;
uint32_t summaryDropPhraseAbortCount = 0;
unsigned long summaryUnexpectedStoppedMs = 0;

// Forward declarations -- the state machine's enter*/update* functions form
// a cycle, so a few need to be visible before their own definition,
// matching the same pattern DanceEngine.cpp uses for ensureEnabledForTest().
void enterBassAccent(unsigned long now);
void enterHipShake(unsigned long now);
void enterReverseHipShake(unsigned long now, bool heavy);
void enterExtendedSpin(unsigned long now);
void enterSustainedDrive(unsigned long now, MusicMotorDirection direction);
void exitSustainedDrive(unsigned long now, const char *reason);
void performSustainedDriveExtension(unsigned long now);
void performSustainedDriveDirectionSwitch(unsigned long now, bool wasExceptional);
void enterDecelerating(unsigned long now);
void stopCleanly(unsigned long now);
void enterMusicalRampDown(unsigned long now, uint32_t durationMs);
void beginMusicalSilenceStop(unsigned long now);
MusicIntensityBand effectiveBand();
MusicIntensityBand computeRawCandidateBand(float energy, float lowT, float mediumT, float highT, float peakT);
MusicMotorDirection reverseHipShakePhaseDirection(uint8_t phaseIndex, MusicMotorDirection original);
bool canReverseNow(unsigned long now);
bool checkAndHandleSilenceTimeout(unsigned long now);
// Revision 10's mutating helpers (applyMotionDynamics()/
// advanceDropPhraseStepSequencer()/etc, defined early in the file so
// enterSustainedDrive()/updateSustainedDrive()/updateIntensitySway() can
// call them) need these three, which are only DEFINED much later.
void updateAppliedSpeedTowardTarget(unsigned long now);
void applyRampTick(unsigned long now);
uint8_t computeRawIntensityTargetPercent(MusicIntensityBand band, float energy);

void ensurePWMReady() {
  if (pwmReady) return;
  initMotorPWM(DANCE_PWM_FREQUENCY_HZ, DANCE_PWM_RESOLUTION_BITS);
  pwmReady = true;
}

void applyPwm(MusicMotorDirection dir, uint8_t percent) {
  uint8_t duty = percentToMotorPwm(percent);
  if (dir == MusicMotorDirection::FORWARD) motorPWMForward(duty);
  else motorPWMReverse(duty);
}

// Reported movement state for status/diagnostics -- surfaces the
// cross-cutting reversal-coast mechanism as "REVERSAL_COAST" without it
// being a real top-level `state` value (see MusicMotorController.h's enum
// comment for why).
const char *reportedStateName() {
  if (coastingForReversal) return "REVERSAL_COAST";
  return stateName(state);
}

// ----------------------------------------------------------------------------
// Shared reversal gate -- read-only (no side effects), so selectBeatAction()
// can consult it when deciding what action to take without yet committing
// to anything. tryRequestReversal() below calls this same function before
// actually performing the coast+flip -- every reversal-requesting caller
// (periodic LOW-band sway, beat-action REVERSE_DIRECTION, mid-hip-shake
// strong hits) is governed by these exact same rules.
// ----------------------------------------------------------------------------
bool checkReversalGate(unsigned long now, ReversalRejectReason &reason) {
  if (coastingForReversal) {
    reason = ReversalRejectReason::ALREADY_REVERSING;
    return false;
  }
  if (now - directionStartMs < MUSIC_MOTOR_MIN_DIRECTION_HOLD_MS) {
    reason = ReversalRejectReason::HOLD_TIME;
    return false;
  }
  if (now - lastReversalMs < MUSIC_MOTOR_REVERSAL_COOLDOWN_MS) {
    reason = ReversalRejectReason::COOLDOWN;
    return false;
  }
  if (now - lastSpinEndMs < MUSIC_MOTOR_POST_SPIN_DIRECTION_HOLD_MS) {
    reason = ReversalRejectReason::POST_SPIN_HOLD;
    return false;
  }
  reason = ReversalRejectReason::NONE;
  return true;
}

bool canReverseNow(unsigned long now) {
  ReversalRejectReason unused;
  return checkReversalGate(now, unused);
}

// ----------------------------------------------------------------------------
// Revision 6 -- PURE "favor continuation over interruption" gate (no
// Serial, no mutation) -- see Config.h's MUSIC_MOTOR_MIN_ROTATION_HOLD_MS
// comment for the full rationale. Layered ON TOP OF checkReversalGate()'s
// hardware-safety gate (canReverseNow()), never a replacement for it --
// callers must still check canReverseNow() separately. Exposed as a pure
// function so it can be exercised from host tests without any Arduino
// dependency, same pattern as computeDropHoldDecision()/
// computeStrongHitReason() above.
// ----------------------------------------------------------------------------
bool reversalCommitmentSatisfied(unsigned long now, unsigned long directionStartMs, bool strongAccent, bool phraseBoundary,
                                  uint32_t minRotationHoldMs) {
  if (strongAccent || phraseBoundary) return true;
  return (now - directionStartMs) >= minRotationHoldMs;
}

// Revision 5: gated on effectiveBand() (real intensityBand, lifted by
// dropHold/performanceEnergy exactly like every other choreography-
// eligibility check) rather than the raw measured intensityBand alone, so a
// strong hit arriving during a brief real dip inside an otherwise energetic
// phrase can still commit to a spin -- see effectiveBand()'s own comment.
bool canSpinNow(unsigned long now) {
  return ((int)effectiveBand() >= (int)MUSIC_MOTOR_SPIN_MIN_INTENSITY_LEVEL) &&
         (now - lastSpinMs >= tunableSpinCooldownMs) && (state != MusicMotorState::EXTENDED_SPIN);
}

// Shared start-cooldown gate for BOTH HIP_SHAKE and REVERSE_HIP_SHAKE (see
// their shared lastHipShakeStartMs/hipShakeStartCooldownMs) -- neither may
// retrigger the other back-to-back.
bool canStartHipShakeNow(unsigned long now) { return (now - lastHipShakeStartMs) >= hipShakeStartCooldownMs; }
void markHipShakeStarted(unsigned long now) {
  lastHipShakeStartMs = now;
  hipShakeStartCooldownMs = randomRangeU32(MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MIN_MS, MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MAX_MS);
}

// ----------------------------------------------------------------------------
// Drop hold -- a choreography-PERMISSION signal only. Never overwrites the
// real measured intensityBand (see maybePrintDiagnostic()/musicMotorPrintStatus(),
// which always report the true band); see effectiveBand() for
// the only place it actually influences a decision.
// ----------------------------------------------------------------------------
void startOrRefreshDropHold(unsigned long now) {
  if (!dropHoldActive) {
    dropHoldActive = true;
    dropHoldStartMs = now;
    dropHoldUntilMs = now + MUSIC_MOTOR_DROP_HOLD_INITIAL_MS;
    Serial.printf("[MUSIC MOTOR] drop hold started duration=%lums\n", (unsigned long)MUSIC_MOTOR_DROP_HOLD_INITIAL_MS);
    return;
  }
  unsigned long maxUntil = dropHoldStartMs + MUSIC_MOTOR_DROP_HOLD_MAX_MS;
  unsigned long candidate = now + MUSIC_MOTOR_DROP_HOLD_INITIAL_MS;
  unsigned long newUntil = (candidate < maxUntil) ? candidate : maxUntil;
  if (newUntil > dropHoldUntilMs) {
    dropHoldUntilMs = newUntil;
    Serial.printf("[MUSIC MOTOR] drop hold refreshed remaining=%lums\n", (unsigned long)(dropHoldUntilMs - now));
  }
}

void updateDropHold(unsigned long now) {
  if (!dropHoldActive) return;
  if ((long)(now - dropHoldUntilMs) >= 0) {
    dropHoldActive = false;
    Serial.println(F("[MUSIC MOTOR] drop hold expired reason=timeout_expired"));
  }
}

// Immediate cancellation path -- silence, disable, emergency stop, reset.
// Silent (no log) if a hold wasn't active, so this can be called
// unconditionally from every hard-stop path without spamming. `reason`
// uses the stable grep-friendly vocabulary (silence_cancel/disabled_cancel/
// emergency_stop_cancel) -- see hardStop()/stopCleanly()'s call sites.
void cancelDropHold(const char *reason) {
  if (!dropHoldActive) return;
  dropHoldActive = false;
  Serial.printf("[MUSIC MOTOR] drop hold cancelled reason=%s\n", reason);
}

// ----------------------------------------------------------------------------
// Revision 4 -- PURE decision-reason helper (no Serial, no side effects, no
// globals mutated) so it can be exercised from host tests without any
// Arduino dependency. Mirrors -- does not replace -- the REAL gating
// already in place at this function's only caller in
// updateMusicMotorController() and inside startOrRefreshDropHold() itself
// (the start-vs-refresh-vs-saturated arithmetic). Computed in PARALLEL with
// the real call, from the same inputs, purely for diagnostic reporting --
// never influences the real decision, so there is no risk of the diagnostic
// and the actual behavior diverging from a bug in this function.
//
// Revision 5: TWO independent qualification paths, either one sufficient
// (never both required) -- "do not make strong hits mandatory for...drop
// hold":
//   qualifiedViaHit      the original path -- a qualifying strong hit while
//                        the MEASURED band is BAND_PEAK.
//   qualifiedViaSustain  NEW -- intensityBand has continuously been
//                        BAND_HIGH or BAND_PEAK for at least
//                        MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS, regardless
//                        of any transient event -- addresses sustained/
//                        compressed-mastering sections whose real measured
//                        intensity genuinely reaches HIGH/PEAK but whose
//                        transient peaks are too weak/rare to reliably cross
//                        MUSIC_MOTOR_STRONG_HIT_DELTA_THRESHOLD.
// ----------------------------------------------------------------------------
enum class DropHoldOutcome { STARTED, REFRESHED, REJECTED };
struct DropHoldDecision {
  bool qualifyingBand;  // true iff measuredBand==PEAK && !silent (the strong-hit path's own band requirement)
  bool qualifyingHit;
  bool qualifiedViaSustain;
  DropHoldOutcome outcome;
  const char *reason;
};

DropHoldDecision computeDropHoldDecision(MusicIntensityBand measuredBand, bool strongHit, bool beat, bool silent,
                                          bool sustainedQualify, bool active, unsigned long startMs, unsigned long untilMs,
                                          unsigned long now) {
  DropHoldDecision d{};
  d.qualifyingBand = (measuredBand == MusicIntensityBand::BAND_PEAK) && !silent;
  d.qualifyingHit = strongHit;
  d.qualifiedViaSustain = sustainedQualify && !silent;
  bool qualifiedViaHit = d.qualifyingBand && strongHit;
  if (!qualifiedViaHit && !d.qualifiedViaSustain) {
    d.outcome = DropHoldOutcome::REJECTED;
    if (silent) {
      d.reason = "silent";
    } else if (!d.qualifyingBand && !sustainedQualify) {
      // Neither path's own band requirement is met at all -- keeps the
      // pre-revision-5 "band_not_peak" string for this, the ordinary
      // no-qualification-possible case (e.g. BAND_LOW), since neither a
      // strong hit at PEAK nor a HIGH/PEAK dwell is even in play yet.
      d.reason = "band_not_peak";
    } else if (d.qualifyingBand && !strongHit) {
      // Distinguishes "nothing detected at all this tick" from "an ordinary
      // beat fired but wasn't strong enough" -- both real, distinct
      // outcomes of the beat/strongHit detection block.
      d.reason = beat ? "strong_hit_false" : "beat_not_detected";
    } else {
      // measuredBand is HIGH/PEAK (or was, recently) but the continuous
      // dwell hasn't reached MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS yet.
      d.reason = "sustain_not_confirmed";
    }
    return d;
  }
  if (!active) {
    d.outcome = DropHoldOutcome::STARTED;
    d.reason = qualifiedViaHit ? "start_qualified_hit" : "start_qualified_sustained";
    return d;
  }
  // Mirrors startOrRefreshDropHold()'s own refresh arithmetic exactly.
  unsigned long maxUntil = startMs + MUSIC_MOTOR_DROP_HOLD_MAX_MS;
  unsigned long candidate = now + MUSIC_MOTOR_DROP_HOLD_INITIAL_MS;
  unsigned long newUntil = (candidate < maxUntil) ? candidate : maxUntil;
  if (newUntil > untilMs) {
    d.outcome = DropHoldOutcome::REFRESHED;
    d.reason = qualifiedViaHit ? "refresh_qualified_hit" : "refresh_qualified_sustained";
  } else {
    // Already active and already at (or past) the 4000ms continuous cap --
    // a further qualifying hit/sustain exists but genuinely changes nothing.
    d.outcome = DropHoldOutcome::REJECTED;
    d.reason = "already_active_no_refresh";
  }
  return d;
}

const char *dropHoldOutcomeName(DropHoldOutcome o) {
  switch (o) {
    case DropHoldOutcome::STARTED: return "STARTED";
    case DropHoldOutcome::REFRESHED: return "REFRESHED";
    case DropHoldOutcome::REJECTED: return "REJECTED";
  }
  return "UNKNOWN";
}

void logDropHoldEvaluation(unsigned long now, bool sustainedQualify) {
  if (!debugLoggingEnabled) return;
  DropHoldDecision d = computeDropHoldDecision(intensityBand, strongHitDetectedThisTick, beatDetectedThisTick,
                                                state == MusicMotorState::SILENT, sustainedQualify, dropHoldActive,
                                                dropHoldStartMs, dropHoldUntilMs, now);
  bool significant = d.outcome != DropHoldOutcome::REJECTED;
  if (!debugShouldPrint(dropHoldRateLimit, d.reason, now, significant)) return;
  unsigned long remainingMs = (dropHoldActive && dropHoldUntilMs > now) ? (unsigned long)(dropHoldUntilMs - now) : 0UL;
  Serial.printf(
      "[MUSIC MOTOR] dropHold evaluation measuredBand=%s effectiveBand=%s raw=%.2f fast=%.2f song=%.2f baseline=%.2f "
      "transient=%.2f strongHit=%d beat=%d sustainedQualify=%d active=%d remainingMs=%lu qualifyingBand=%d "
      "qualifyingHit=%d result=%s reason=%s\n",
      intensityBandName(intensityBand), intensityBandName(effectiveBand()), (double)rawEnergy,
      (double)fastEnergy, (double)songEnergy, (double)baselineEnergy, (double)transientDelta,
      strongHitDetectedThisTick ? 1 : 0, beatDetectedThisTick ? 1 : 0, d.qualifiedViaSustain ? 1 : 0,
      dropHoldActive ? 1 : 0, remainingMs, d.qualifyingBand ? 1 : 0, d.qualifyingHit ? 1 : 0,
      dropHoldOutcomeName(d.outcome), d.reason);
}

// The band choreography selection AND the live intensityTargetPercent
// actually use -- identical to the real measured intensityBand except that
// it can be LENT a higher band by either of two independent signals, never
// lowered by them:
//   - a drop hold lends HIGH-band eligibility to a MEDIUM reading (per the
//     original explicit requirement: "reverse hip shakes remain eligible...
//     even when the measured band briefly falls to MEDIUM")
//   - performanceEnergy (revision 5) lends whatever band ITS OWN magnitude
//     would imply (via the same hysteresis-free computeRawCandidateBand()
//     used by the band-evaluation diagnostic) -- this is what keeps motion
//     "sustained through energetic phrases" across a brief real dip, and
//     what produces the gradual multi-second wind-down: as performanceEnergy
//     decays, the band it lends decays right along with it, one step at a
//     time, rather than the motor snapping straight down to whatever the
//     real (already-lower) measured band calls for.
// Used for action selection (selectBeatAction()/canSpinNow()) AND for
// intensityTargetPercent (see updateMusicMotorController()) -- every
// diagnostic/status field always prints the real measured intensityBand
// separately alongside this one (labeled "effective band").
MusicIntensityBand effectiveBand() {
  MusicIntensityBand eff = intensityBand;
  if (dropHoldActive && intensityBand == MusicIntensityBand::BAND_MEDIUM) eff = MusicIntensityBand::BAND_HIGH;
  MusicIntensityBand perfBand =
      computeRawCandidateBand(performanceEnergy, tunableLowThreshold, tunableMediumThreshold, tunableHighThreshold, tunablePeakThreshold);
  if ((int)perfBand > (int)eff) eff = perfBand;
  return eff;
}

// ----------------------------------------------------------------------------
// Revision 7 -- SUSTAINED_DRIVE eligibility + weighted entry decision. PURE
// (no Serial, no globals mutated, no random() -- every random draw is an
// explicit parameter) so it can be exercised from host tests without any
// Arduino dependency, same pattern as computeDropHoldDecision() above.
// Mirrors -- does not replace -- the real gating at this function's only
// caller, trySustainedDriveEntry() (mutating, does the actual random()
// rolls and calls this).
// ----------------------------------------------------------------------------
uint8_t sustainedDriveWeightPercent(MusicIntensityBand effBand) {
  switch (effBand) {
    case MusicIntensityBand::BAND_MEDIUM: return MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_MEDIUM_PERCENT;
    case MusicIntensityBand::BAND_HIGH: return MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_HIGH_PERCENT;
    case MusicIntensityBand::BAND_PEAK: return MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_PEAK_PERCENT;
    default: return 0;  // QUIET/LOW are never eligible regardless of weight
  }
}

enum class SustainedDriveOutcome { STARTED, REJECTED };
struct SustainedDriveDecision {
  bool bandEligible;
  bool cooldownReady;
  bool weightRoll;  // only meaningful when bandEligible && cooldownReady && !alreadyActive
  SustainedDriveOutcome outcome;
  const char *reason;
};

// `weightRollPercent0to99` is an explicit draw in [0,99] (mirrors
// `random(100)`); rolls succeed when the draw is LESS than the band's
// weight percent (e.g. weight=12 succeeds for draws 0..11 -- exactly a 12%
// chance).
SustainedDriveDecision computeSustainedDriveDecision(MusicIntensityBand effBand, bool silent, bool alreadyActive,
                                                      unsigned long now, unsigned long cooldownUntilMs,
                                                      uint8_t weightRollPercent0to99) {
  SustainedDriveDecision d{};
  d.bandEligible = ((int)effBand >= (int)MusicIntensityBand::BAND_MEDIUM) && !silent;
  d.cooldownReady = (long)(now - cooldownUntilMs) >= 0;
  if (alreadyActive) {
    d.outcome = SustainedDriveOutcome::REJECTED;
    d.reason = "already_active";
    return d;
  }
  if (!d.bandEligible) {
    d.outcome = SustainedDriveOutcome::REJECTED;
    d.reason = silent ? "silent" : "band_below_medium";
    return d;
  }
  if (!d.cooldownReady) {
    d.outcome = SustainedDriveOutcome::REJECTED;
    d.reason = "cooldown_active";
    return d;
  }
  d.weightRoll = weightRollPercent0to99 < sustainedDriveWeightPercent(effBand);
  if (!d.weightRoll) {
    d.outcome = SustainedDriveOutcome::REJECTED;
    d.reason = "weight_roll_failed";
    return d;
  }
  d.outcome = SustainedDriveOutcome::STARTED;
  d.reason = "weight_roll_succeeded";
  return d;
}

// Direction selection -- "normally prefer the current direction... FLIP
// only occasionally, when a strong accent justifies it," THEN apply the
// max-consecutive-same-direction cap (forced flip unless a continuing
// PEAK/drop section overrides it). `flipRollPercent0to99` gates the accent
// flip (mirrors `random(100)` against MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT);
// `safeToFlip` is the caller's canReverseNow() hardware-safety check --
// this function NEVER chooses a flip that isn't safe, for either reason
// (accent or cap): if the cap would require an unsafe flip, the cap is left
// unsatisfied (the caller/log treats this as "capped" via
// consecutiveCapForcedButUnsafe, but the direction itself falls back to
// whatever was otherwise chosen -- never an unsafe reversal).
struct SustainedDriveDirectionChoice {
  MusicMotorDirection direction;
  bool accentFlipped;
  bool capForcedFlip;
  bool capForcedButUnsafe;  // cap wanted to force a flip but safeToFlip was false -- direction left as-is
};

SustainedDriveDirectionChoice chooseSustainedDriveDirection(MusicMotorDirection currentDirection, bool strongAccent,
                                                              uint8_t flipRollPercent0to99, bool safeToFlip,
                                                              MusicMotorDirection lastDirection,
                                                              uint8_t consecutiveSameDirectionCount,
                                                              bool consecutiveCapOverride) {
  SustainedDriveDirectionChoice c{};
  c.direction = currentDirection;
  if (strongAccent && safeToFlip && flipRollPercent0to99 < MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT) {
    c.direction = oppositeOf(currentDirection);
    c.accentFlipped = true;
  }
  bool wouldRepeat = (c.direction == lastDirection) &&
                      (consecutiveSameDirectionCount >= MUSIC_MOTOR_SUSTAINED_DRIVE_MAX_CONSECUTIVE_SAME_DIRECTION);
  if (wouldRepeat && !consecutiveCapOverride) {
    if (safeToFlip) {
      c.direction = oppositeOf(c.direction);
      c.capForcedFlip = true;
    } else {
      c.capForcedButUnsafe = true;
    }
  }
  return c;
}

const char *phraseTierName(SustainedDrivePhraseTier t) {
  switch (t) {
    case SustainedDrivePhraseTier::SHORT: return "SHORT";
    case SustainedDrivePhraseTier::STANDARD: return "STANDARD";
    case SustainedDrivePhraseTier::EXTENDED: return "EXTENDED";
    case SustainedDrivePhraseTier::RENEWABLE: return "RENEWABLE";
  }
  return "UNKNOWN";
}

// ----------------------------------------------------------------------------
// Revision 8 -- renewable performance phrase. All PURE (no Serial, no
// globals mutated, no random() -- explicit draw parameters), same testing
// philosophy as every other decision helper in this file.
// ----------------------------------------------------------------------------

struct SustainedDriveTimeRange {
  uint32_t minMs;
  uint32_t maxMs;
};

// Initial review point range, by effective band at entry -- PEAK and an
// active drop hold share the widest range (both are "the biggest
// moments"). See Config.h's MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_* comment.
SustainedDriveTimeRange sustainedDriveReviewRange(MusicIntensityBand effBand, bool dropHoldActiveNow) {
  if (effBand == MusicIntensityBand::BAND_PEAK || dropHoldActiveNow) {
    return {MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_PEAK_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_PEAK_MAX_MS};
  }
  if (effBand == MusicIntensityBand::BAND_HIGH) {
    return {MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_HIGH_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_HIGH_MAX_MS};
  }
  return {MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_MEDIUM_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_MEDIUM_MAX_MS};
}

// Extension range -- same band-tiering as above, evaluated fresh at EACH
// review (by the effective band AT THAT REVIEW), so a phrase can move
// between ranges as the music itself moves between bands. See Config.h's
// MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_* comment.
SustainedDriveTimeRange sustainedDriveExtensionRange(MusicIntensityBand effBand, bool dropHoldActiveNow) {
  if (effBand == MusicIntensityBand::BAND_PEAK || dropHoldActiveNow) {
    return {MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_PEAK_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_PEAK_MAX_MS};
  }
  if (effBand == MusicIntensityBand::BAND_HIGH) {
    return {MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_HIGH_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_HIGH_MAX_MS};
  }
  return {MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_MEDIUM_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_MEDIUM_MAX_MS};
}

// Entry TIER choice (SHORT vs. long-form) -- see Config.h's
// MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_*_PERCENT comment.
// `majorTransient` boosts the short-tier weight (an explosive-burst
// opportunity); an active drop hold instead HALVES it (favor long-form
// even more strongly during a confirmed drop, per "PEAK/DropHold favor
// standard or extended phrases"). `tierRollPercent0to99` mirrors
// `random(100)`.
SustainedDrivePhraseTier chooseSustainedDriveEntryTier(MusicIntensityBand effBand, bool dropHoldActiveNow, bool majorTransient,
                                                        uint8_t tierRollPercent0to99) {
  uint16_t shortWeight;
  switch (effBand) {
    case MusicIntensityBand::BAND_PEAK: shortWeight = MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_PEAK_PERCENT; break;
    case MusicIntensityBand::BAND_HIGH: shortWeight = MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_HIGH_PERCENT; break;
    default: shortWeight = MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_MEDIUM_PERCENT; break;
  }
  if (dropHoldActiveNow) shortWeight /= 2;
  if (majorTransient) shortWeight += MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_MAJOR_TRANSIENT_BOOST_PERCENT;
  if (shortWeight > 100) shortWeight = 100;
  return (tierRollPercent0to99 < shortWeight) ? SustainedDrivePhraseTier::SHORT : SustainedDrivePhraseTier::STANDARD;
}

// Direct sustained-direction switch qualification -- see Config.h's
// "Direct sustained-direction switching" comment. Evaluated ONLY on a
// qualifying STRONG HIT (the caller never calls this for an ordinary beat)
// -- "ordinary beats must never trigger this transition" is therefore an
// architectural invariant of the CALL SITE, not something this function
// itself needs to re-derive from a beat/strongHit flag.
struct SustainedSwitchQualification {
  bool qualifies;
  bool exceptionalBypass;  // qualified via the exceptional PEAK+dropHold early-bypass path, not the ordinary cooldown
  const char *reason;
};

SustainedSwitchQualification computeSustainedSwitchQualification(MusicIntensityBand measuredBand, bool dropHoldActiveNow,
                                                                   unsigned long now, unsigned long directionCommitStartMs,
                                                                   unsigned long switchCooldownUntilMs, bool safeToFlip) {
  SustainedSwitchQualification q{};
  if (!safeToFlip) {
    q.reason = "unsafe";
    return q;
  }
  bool strongAccent = (measuredBand == MusicIntensityBand::BAND_PEAK) || dropHoldActiveNow;
  if (!strongAccent) {
    q.reason = "no_strong_accent";
    return q;
  }
  unsigned long heldMs = now - directionCommitStartMs;
  bool exceptional = (measuredBand == MusicIntensityBand::BAND_PEAK) && dropHoldActiveNow;
  if (exceptional && heldMs >= MUSIC_MOTOR_SUSTAINED_SWITCH_EXCEPTIONAL_MIN_MS) {
    q.qualifies = true;
    q.exceptionalBypass = true;
    q.reason = "exceptional_peak_drophold";
    return q;
  }
  if ((long)(now - switchCooldownUntilMs) < 0) {
    q.reason = "cooldown_active";
    return q;
  }
  q.qualifies = true;
  q.reason = "cooldown_ready";
  return q;
}

// The renewable-phrase continuation decision -- the ONE function that
// decides what happens at a review point (or on a qualifying mid-review
// switch opportunity). See Config.h's "Revision 8" comment block for the
// full rationale of each input.
enum class SustainedDriveContinuationDecision {
  CONTINUE_UNTIL_REVIEW,
  EXTEND_SAME_DIRECTION,
  SWITCH_SUSTAINED_DIRECTION,
  EXIT_TO_NORMAL,
  EXIT_FOR_SILENCE,
  EXIT_FOR_SAFETY,
};

struct SustainedDriveContinuationInputs {
  MusicIntensityBand effectiveBandNow = MusicIntensityBand::BAND_QUIET;
  bool dropHoldActiveNow = false;
  bool energyTrendRising = false;
  bool recentBeatActivity = false;
  bool recentStrongHitActivity = false;
  bool genuinelySilent = false;
  bool motorSafetyOk = true;
  unsigned long nowMs = 0;
  unsigned long directionCommitStartMs = 0;
  uint32_t minCommitmentMs = 0;
  unsigned long lowEnergySinceMs = 0;  // 0 = not currently low
  // Revision 9 -- true while a CONFIRMED_DROP or MAJOR_DROP relative
  // section is DROP_ACTIVE. One more OR'd source of continuation support
  // alongside effectiveBandNow/energyTrendRising/recent beat-or-hit
  // activity -- "extension rules must let relative performance sections
  // support renewable extensions even at MEDIUM."
  bool musicalSectionActive = false;
};

struct SustainedDriveContinuationResult {
  SustainedDriveContinuationDecision decision;
  const char *reason;
};

const char *sustainedDriveContinuationDecisionName(SustainedDriveContinuationDecision d) {
  switch (d) {
    case SustainedDriveContinuationDecision::CONTINUE_UNTIL_REVIEW: return "CONTINUE_UNTIL_REVIEW";
    case SustainedDriveContinuationDecision::EXTEND_SAME_DIRECTION: return "EXTEND_SAME_DIRECTION";
    case SustainedDriveContinuationDecision::SWITCH_SUSTAINED_DIRECTION: return "SWITCH_SUSTAINED_DIRECTION";
    case SustainedDriveContinuationDecision::EXIT_TO_NORMAL: return "EXIT_TO_NORMAL";
    case SustainedDriveContinuationDecision::EXIT_FOR_SILENCE: return "EXIT_FOR_SILENCE";
    case SustainedDriveContinuationDecision::EXIT_FOR_SAFETY: return "EXIT_FOR_SAFETY";
  }
  return "UNKNOWN";
}

// `reviewDue` -- now >= the phrase's next scheduled review point.
// `switchQualifies` -- the caller's computeSustainedSwitchQualification()
// result was qualifying (independent of reviewDue -- a switch can fire on
// a qualifying strong hit between reviews, per "phrase review point
// reached after a long directional commitment" being only ONE of several
// listed qualifying conditions, not the sole trigger).
//
// Priority order (first match wins), matching Config.h's documented
// precedence: safety > silence > minimum commitment > low-energy-grace
// expiry > switch > review (extend-or-exit) > (nothing due yet). This is
// deliberately NOT re-evaluated every loop iteration by its caller -- see
// updateSustainedDrive()'s own early-return before ever calling this.
SustainedDriveContinuationResult computeSustainedDriveContinuationDecision(const SustainedDriveContinuationInputs &in,
                                                                            bool reviewDue, bool switchQualifies) {
  SustainedDriveContinuationResult r{};
  if (!in.motorSafetyOk) {
    r.decision = SustainedDriveContinuationDecision::EXIT_FOR_SAFETY;
    r.reason = "safety";
    return r;
  }
  if (in.genuinelySilent) {
    r.decision = SustainedDriveContinuationDecision::EXIT_FOR_SILENCE;
    r.reason = "silent";
    return r;
  }
  if ((in.nowMs - in.directionCommitStartMs) < in.minCommitmentMs) {
    r.decision = SustainedDriveContinuationDecision::CONTINUE_UNTIL_REVIEW;
    r.reason = "min_commitment_active";
    return r;
  }
  if (in.lowEnergySinceMs != 0 && (in.nowMs - in.lowEnergySinceMs) >= MUSIC_MOTOR_SUSTAINED_DRIVE_LOW_GRACE_MS) {
    r.decision = SustainedDriveContinuationDecision::EXIT_TO_NORMAL;
    r.reason = "low_energy_grace_expired";
    return r;
  }
  if (switchQualifies) {
    r.decision = SustainedDriveContinuationDecision::SWITCH_SUSTAINED_DIRECTION;
    r.reason = "switch_qualified";
    return r;
  }
  if (!reviewDue) {
    r.decision = SustainedDriveContinuationDecision::CONTINUE_UNTIL_REVIEW;
    r.reason = "not_due";
    return r;
  }
  // "Do not exit solely because the initial review timer expired...while
  // music remained strongly qualified" -- a review with support present
  // always extends; only a review with NO support ends the phrase.
  bool supportsContinuation = ((int)in.effectiveBandNow >= (int)MusicIntensityBand::BAND_MEDIUM) || in.energyTrendRising ||
                               in.recentBeatActivity || in.recentStrongHitActivity || in.musicalSectionActive;
  if (supportsContinuation) {
    r.decision = SustainedDriveContinuationDecision::EXTEND_SAME_DIRECTION;
    r.reason = "energy_supports_extension";
    return r;
  }
  r.decision = SustainedDriveContinuationDecision::EXIT_TO_NORMAL;
  r.reason = "energy_no_longer_supports_phrase";
  return r;
}

const char *stopStyleName(SilenceStopStyle s) {
  return s == SilenceStopStyle::DRAMATIC_ABRUPT_STOP ? "DRAMATIC_ABRUPT_STOP" : "GRADUAL_RAMP_DOWN";
}

// ----------------------------------------------------------------------------
// Revision 8 addition -- lifelike silence stop-style decision. PURE; the
// caller (beginMusicalSilenceStop()) supplies the random draw and the
// already-computed "was a drop recently active" context. "Do not rely only
// on a random percentage" -- sharpCutoffContext (real musical evidence, not
// just a coin flip) selects WHICH percentage applies; see Config.h's
// MUSIC_MOTOR_ABRUPT_STOP_*_PERCENT comment.
// ----------------------------------------------------------------------------
SilenceStopStyle chooseSilenceStopStyle(bool sharpCutoffContext, uint8_t abruptRollPercent0to99) {
  uint8_t abruptChance = sharpCutoffContext ? MUSIC_MOTOR_ABRUPT_STOP_SHARP_CUTOFF_PERCENT : MUSIC_MOTOR_ABRUPT_STOP_NORMAL_PERCENT;
  return (abruptRollPercent0to99 < abruptChance) ? SilenceStopStyle::DRAMATIC_ABRUPT_STOP : SilenceStopStyle::GRADUAL_RAMP_DOWN;
}

// Ramp-down duration -- "lower-energy movement may stop in ~1-2s; high-
// energy sustained movement may take ~2-4s to settle." `durationRollMs` is
// the caller's already-randomized pick from whichever range applies (mirrors
// randomRangeU32()'s contract -- kept as an explicit parameter for purity).
uint32_t chooseRampDownDurationMs(bool wasHighEnergy, uint32_t lowRangeDurationMs, uint32_t highRangeDurationMs) {
  return wasHighEnergy ? highRangeDurationMs : lowRangeDurationMs;
}

// ----------------------------------------------------------------------------
// Revision 9 -- relative/song-adaptive musical-section (EDM/dubstep "drop")
// recognition. Same PURE/testable philosophy as every decision helper above:
// no Serial, no globals mutated, no random() in the scoring/classification/
// phase-transition functions themselves -- only the mutating wrapper
// (updateRelativeDropTracking(), further below) touches globals or Serial.
// See Config.h's "Revision 9" comment block for the full rationale and every
// constant referenced here.
// ----------------------------------------------------------------------------
const char *musicalSectionPhaseName(MusicalSectionPhase p) {
  switch (p) {
    case MusicalSectionPhase::NEUTRAL: return "NEUTRAL";
    case MusicalSectionPhase::BUILDUP: return "BUILDUP";
    case MusicalSectionPhase::DROP_ARMED: return "DROP_ARMED";
    case MusicalSectionPhase::DROP_IMPACT: return "DROP_IMPACT";
    case MusicalSectionPhase::DROP_ACTIVE: return "DROP_ACTIVE";
    case MusicalSectionPhase::DROP_RELEASE: return "DROP_RELEASE";
  }
  return "UNKNOWN";
}

const char *dropConfidenceTierName(DropConfidenceTier t) {
  switch (t) {
    case DropConfidenceTier::NONE: return "NONE";
    case DropConfidenceTier::POSSIBLE_DROP: return "POSSIBLE_DROP";
    case DropConfidenceTier::CONFIRMED_DROP: return "CONFIRMED_DROP";
    case DropConfidenceTier::MAJOR_DROP: return "MAJOR_DROP";
  }
  return "UNKNOWN";
}

// Per-signal [0,1] contributions this tick -- each one independently
// normalized (never a rigid boolean chain; "no single factor is mandatory").
struct DropSignalScores {
  float energyRise;
  float sectionContrast;
  float bassImpact;
  float bassDensity;
  float beatDensity;
  float transientDensity;
  float buildupResolution;
  float sustainedEnergy;
};

struct DropSignalRawInputs {
  float songEnergyNow;
  float buildupReference;
  float longReference;
  float referenceFloor;
  float bassImpactDeltaNow;
  float bassImpactDeltaThreshold;
  float beatDensityScoreNow;
  float beatDensityNormalization;
  float transientDensityScoreNow;
  float transientDensityNormalization;
  float bassDensityScoreNow;
  float bassDensityNormalization;
  bool recentlyBuildingOrArmed;  // phase (as of the START of this tick) was BUILDUP/DROP_ARMED/DROP_IMPACT
  float performanceEnergyNow;
  float peakThresholdForNormalization;
};

// "Do not compare only against fixed global thresholds" -- energyRise and
// sectionContrast are both RATIOS against rolling references (the recent
// section, and the song's own envelope so far), not the fixed
// MUSIC_MOTOR_*_THRESHOLD band constants. sectionContrast is deliberately
// self-normalizing: a section that simply STAYS loud pulls longReference up
// to match it over time, so it stops contributing -- "a continuously loud
// section should not repeatedly generate new drops merely for staying
// loud."
DropSignalScores computeDropSignalScores(const DropSignalRawInputs &in) {
  DropSignalScores s{};
  s.energyRise = constrain((in.songEnergyNow - in.buildupReference) / max(in.buildupReference, in.referenceFloor), 0.0f, 1.0f);
  s.sectionContrast = constrain((in.songEnergyNow - in.longReference) / max(in.longReference, in.referenceFloor), 0.0f, 1.0f);
  s.bassImpact = constrain(in.bassImpactDeltaNow / max(in.bassImpactDeltaThreshold, 0.001f), 0.0f, 1.0f);
  s.bassDensity = constrain(in.bassDensityScoreNow / max(in.bassDensityNormalization, 0.001f), 0.0f, 1.0f);
  s.beatDensity = constrain(in.beatDensityScoreNow / max(in.beatDensityNormalization, 0.001f), 0.0f, 1.0f);
  s.transientDensity = constrain(in.transientDensityScoreNow / max(in.transientDensityNormalization, 0.001f), 0.0f, 1.0f);
  // "A buildup followed by a strong arrival" is weighted more heavily than
  // the same energy rise arriving with no preceding buildup at all (e.g. a
  // section that was already loud and just got slightly louder).
  s.buildupResolution = in.recentlyBuildingOrArmed ? s.energyRise : s.energyRise * 0.4f;
  s.sustainedEnergy = constrain(in.performanceEnergyNow / max(in.peakThresholdForNormalization, 0.001f), 0.0f, 1.0f);
  return s;
}

// Weighted combination -- "an overall drop confidence, not a rigid boolean
// chain." Weights are explicit parameters (mirroring Config.h's
// MUSIC_MOTOR_EDM_WEIGHT_* constants) so host tests can exercise this
// without any Arduino dependency, same pattern as every other pure function
// in this file.
float computeDropConfidence(const DropSignalScores &s, float wEnergyRise, float wSectionContrast, float wBassImpact,
                             float wBassDensity, float wBeatDensity, float wTransientDensity, float wBuildupResolution,
                             float wSustainedEnergy) {
  float total = s.energyRise * wEnergyRise + s.sectionContrast * wSectionContrast + s.bassImpact * wBassImpact +
                s.bassDensity * wBassDensity + s.beatDensity * wBeatDensity + s.transientDensity * wTransientDensity +
                s.buildupResolution * wBuildupResolution + s.sustainedEnergy * wSustainedEnergy;
  return constrain(total, 0.0f, 1.0f);
}

DropConfidenceTier classifyDropConfidenceTier(float confidence, float possibleThreshold, float confirmedThreshold,
                                               float majorThreshold) {
  if (confidence >= majorThreshold) return DropConfidenceTier::MAJOR_DROP;
  if (confidence >= confirmedThreshold) return DropConfidenceTier::CONFIRMED_DROP;
  if (confidence >= possibleThreshold) return DropConfidenceTier::POSSIBLE_DROP;
  return DropConfidenceTier::NONE;
}

struct MusicalSectionPhaseInputs {
  MusicalSectionPhase currentPhase = MusicalSectionPhase::NEUTRAL;
  unsigned long nowMs = 0;
  unsigned long phaseEnteredMs = 0;
  unsigned long dropRefractoryUntilMs = 0;
  unsigned long belowSustainFloorSinceMs = 0;  // 0 = at/above the sustain floor right now (meaningful only in DROP_ACTIVE)
  float dropConfidenceNow = 0.0f;
  float buildupEntryThreshold = 0.0f;
  float buildupFadeThreshold = 0.0f;
  float possibleThreshold = 0.0f;
  float confirmedThreshold = 0.0f;
  float activeSustainFloor = 0.0f;
  uint32_t buildupMinMs = 0;
  uint32_t armedTimeoutMs = 0;
  uint32_t impactConfirmMs = 0;
  uint32_t releaseGraceMs = 0;
  uint32_t refractoryMs = 0;
};

struct MusicalSectionPhaseResult {
  MusicalSectionPhase newPhase;
  bool transitioned;
  const char *reason;
};

// The EDM/dubstep drop phase state machine: NEUTRAL -> BUILDUP -> DROP_ARMED
// -> DROP_IMPACT -> DROP_ACTIVE -> DROP_RELEASE -> NEUTRAL. Every promotion
// requires either a sustained dwell (BUILDUP's buildupMinMs, DROP_IMPACT's
// impactConfirmMs) or a confidence threshold crossing (DROP_ARMED's
// confirmedThreshold) -- never a single instantaneous sample, matching "do
// not define a drop from one isolated sample." DROP_ARMED expiring back to
// NEUTRAL on armedTimeoutMs is "DROP_ARMED should expire if the anticipated
// arrival never occurs." DROP_IMPACT collapsing back to BUILDUP (not
// DROP_ACTIVE) before impactConfirmMs elapses is "a single impact followed
// immediately by low energy should be an accent or hit, not necessarily a
// full drop." DROP_RELEASE always advances to NEUTRAL on the very next
// evaluation -- the actual "don't re-arm too soon" protection is
// dropRefractoryUntilMs (set by the caller the instant DROP_ACTIVE exits),
// checked here only in the NEUTRAL case.
MusicalSectionPhaseResult computeMusicalSectionPhaseTransition(const MusicalSectionPhaseInputs &in) {
  MusicalSectionPhaseResult r{in.currentPhase, false, "no_change"};
  unsigned long elapsedInPhase = in.nowMs - in.phaseEnteredMs;
  switch (in.currentPhase) {
    case MusicalSectionPhase::NEUTRAL:
      if ((long)(in.nowMs - in.dropRefractoryUntilMs) < 0) return r;  // refractory -- stay NEUTRAL
      if (in.dropConfidenceNow >= in.buildupEntryThreshold) {
        r.newPhase = MusicalSectionPhase::BUILDUP;
        r.transitioned = true;
        r.reason = "rising_evidence";
      }
      return r;
    case MusicalSectionPhase::BUILDUP:
      if (in.dropConfidenceNow < in.buildupFadeThreshold) {
        r.newPhase = MusicalSectionPhase::NEUTRAL;
        r.transitioned = true;
        r.reason = "buildup_faded";
        return r;
      }
      if (elapsedInPhase >= in.buildupMinMs && in.dropConfidenceNow >= in.possibleThreshold) {
        r.newPhase = MusicalSectionPhase::DROP_ARMED;
        r.transitioned = true;
        r.reason = "buildup_sufficient";
      }
      return r;
    case MusicalSectionPhase::DROP_ARMED:
      if (in.dropConfidenceNow >= in.confirmedThreshold) {
        r.newPhase = MusicalSectionPhase::DROP_IMPACT;
        r.transitioned = true;
        r.reason = "impact_detected";
        return r;
      }
      if (elapsedInPhase >= in.armedTimeoutMs) {
        r.newPhase = MusicalSectionPhase::NEUTRAL;
        r.transitioned = true;
        r.reason = "armed_timeout";
      }
      return r;
    case MusicalSectionPhase::DROP_IMPACT:
      if (in.dropConfidenceNow < in.possibleThreshold) {
        r.newPhase = MusicalSectionPhase::BUILDUP;
        r.transitioned = true;
        r.reason = "impact_collapsed_to_accent";
        return r;
      }
      if (elapsedInPhase >= in.impactConfirmMs) {
        r.newPhase = MusicalSectionPhase::DROP_ACTIVE;
        r.transitioned = true;
        r.reason = "impact_confirmed";
      }
      return r;
    case MusicalSectionPhase::DROP_ACTIVE:
      if (in.dropConfidenceNow < in.activeSustainFloor && in.belowSustainFloorSinceMs != 0 &&
          (in.nowMs - in.belowSustainFloorSinceMs) >= in.releaseGraceMs) {
        r.newPhase = MusicalSectionPhase::DROP_RELEASE;
        r.transitioned = true;
        r.reason = "sustained_evidence_collapsed";
      }
      return r;
    case MusicalSectionPhase::DROP_RELEASE:
      r.newPhase = MusicalSectionPhase::NEUTRAL;
      r.transitioned = true;
      r.reason = "release_complete";
      return r;
  }
  return r;
}

// Rate-limited per-tick signal/confidence/phase snapshot -- 'musicmotor
// debug' diagnostic, same DebugRateLimit pattern as every other
// log*Evaluation() function. Always significant (prints unconditionally,
// bypassing rate limiting) on a phase transition; otherwise deduped like
// any other REJECTED-style evaluation.
void logDropSignalEvaluation(unsigned long now, bool phaseChangedThisTick) {
  if (!debugLoggingEnabled) return;
  char reasonBuf[24];
  snprintf(reasonBuf, sizeof(reasonBuf), "phase_%s", musicalSectionPhaseName(musicalSectionPhase));
  if (!debugShouldPrint(dropSignalRateLimit, reasonBuf, now, phaseChangedThisTick)) return;
  Serial.printf(
      "[MUSIC MOTOR] drop signals phase=%s tier=%s confidence=%.2f energyRise=%.2f sectionContrast=%.2f "
      "bassImpact=%.2f bassDensity=%.2f beatDensity=%.2f transientDensity=%.2f buildupResolution=%.2f "
      "sustainedEnergy=%.2f\n",
      musicalSectionPhaseName(musicalSectionPhase), dropConfidenceTierName(dropConfidenceTier), (double)dropConfidence,
      (double)dropEnergyRiseScore, (double)dropSectionContrastScore, (double)dropBassImpactScore, (double)dropBassDensityScore,
      (double)dropBeatDensityScore, (double)dropTransientDensityScore, (double)dropBuildupResolutionScore,
      (double)dropSustainedEnergyScore);
}

// Mutating wrapper -- called exactly once per MUSIC_MOTOR_TICK_MS decision
// tick from updateMusicMotorController(), AFTER songEnergy/beatDetectedThisTick/
// strongHitDetectedThisTick/performanceEnergy are all current for this tick.
// Owns every Revision 9 EMA/density/reference update, the drop-confidence
// scoring, and the phase state machine. Entirely inert while
// relativeDropDetectionEnabled is false (the A/B toggle) -- no globals in
// this section are touched at all in that case, so musicalSectionPhase
// simply stays NEUTRAL forever and none of Revision 9's new sustained-drive
// entry/floor/extension paths can ever fire. `bassRaw` is
// AudioFeatures.lowFrequencyEnergy for this tick (see AudioAnalyzer.cpp --
// a single-pole low-pass RMS proxy, NOT true FFT bass; see Config.h).
void updateRelativeDropTracking(unsigned long now, float bassRaw) {
  if (!relativeDropDetectionEnabled) return;

  // --- bass EMA + impact delta ---
  bassFastEnergy = constrain(bassFastEnergy + (bassRaw - bassFastEnergy) * MUSIC_MOTOR_BASS_FAST_ALPHA, 0.0f, 1.0f);
  bassBaselineEnergy = constrain(bassBaselineEnergy + (bassFastEnergy - bassBaselineEnergy) * MUSIC_MOTOR_BASS_BASELINE_ALPHA, 0.0f, 1.0f);
  bassImpactDelta = max(0.0f, bassFastEnergy - bassBaselineEnergy);

  // --- rolling references (both driven by songEnergy, the same signal the
  // absolute band detector uses -- Revision 9 differs in what it compares
  // songEnergy AGAINST, not in re-measuring it a second way) ---
  buildupEnergyReference = constrain(buildupEnergyReference + (songEnergy - buildupEnergyReference) * MUSIC_MOTOR_BUILDUP_REFERENCE_ALPHA, 0.0f, 1.0f);
  longSongEnergyReference = constrain(longSongEnergyReference + (songEnergy - longSongEnergyReference) * MUSIC_MOTOR_LONG_SONG_REFERENCE_ALPHA, 0.0f, 1.0f);

  // --- leaky-integrator density scores. transientDensityScore intentionally
  // reads the RAW threshold crossing (ungated by the beat cooldown), so a
  // fast cluster of moderate hits shows up here even when the cooldown
  // suppresses most of them from becoming discrete beatDetectedThisTick
  // events -- "repeated moderate transients...no single huge transient." ---
  beatDensityScore = beatDensityScore * MUSIC_MOTOR_DENSITY_DECAY_PER_TICK + (beatDetectedThisTick ? 1.0f : 0.0f);
  bool rawTransientThisTick = transientDelta >= tunableBeatThreshold;
  transientDensityScore = transientDensityScore * MUSIC_MOTOR_DENSITY_DECAY_PER_TICK + (rawTransientThisTick ? 1.0f : 0.0f);
  bool bassImpactThisTick = bassImpactDelta >= MUSIC_MOTOR_BASS_IMPACT_DELTA_THRESHOLD;
  bassDensityScore = bassDensityScore * MUSIC_MOTOR_DENSITY_DECAY_PER_TICK + (bassImpactThisTick ? 1.0f : 0.0f);

  // --- score + combine (phase read here is the phase AS OF THE START of
  // this tick, i.e. before this tick's own transition below) ---
  bool recentlyBuildingOrArmed = musicalSectionPhase == MusicalSectionPhase::BUILDUP ||
                                  musicalSectionPhase == MusicalSectionPhase::DROP_ARMED ||
                                  musicalSectionPhase == MusicalSectionPhase::DROP_IMPACT;
  DropSignalRawInputs raw{};
  raw.songEnergyNow = songEnergy;
  raw.buildupReference = buildupEnergyReference;
  raw.longReference = longSongEnergyReference;
  raw.referenceFloor = MUSIC_MOTOR_RELATIVE_REFERENCE_FLOOR;
  raw.bassImpactDeltaNow = bassImpactDelta;
  raw.bassImpactDeltaThreshold = MUSIC_MOTOR_BASS_IMPACT_DELTA_THRESHOLD;
  raw.beatDensityScoreNow = beatDensityScore;
  raw.beatDensityNormalization = MUSIC_MOTOR_BEAT_DENSITY_NORMALIZATION;
  raw.transientDensityScoreNow = transientDensityScore;
  raw.transientDensityNormalization = MUSIC_MOTOR_TRANSIENT_DENSITY_NORMALIZATION;
  raw.bassDensityScoreNow = bassDensityScore;
  raw.bassDensityNormalization = MUSIC_MOTOR_BASS_DENSITY_NORMALIZATION;
  raw.recentlyBuildingOrArmed = recentlyBuildingOrArmed;
  raw.performanceEnergyNow = performanceEnergy;
  raw.peakThresholdForNormalization = tunablePeakThreshold;

  DropSignalScores scores = computeDropSignalScores(raw);
  dropEnergyRiseScore = scores.energyRise;
  dropSectionContrastScore = scores.sectionContrast;
  dropBassImpactScore = scores.bassImpact;
  dropBassDensityScore = scores.bassDensity;
  dropBeatDensityScore = scores.beatDensity;
  dropTransientDensityScore = scores.transientDensity;
  dropBuildupResolutionScore = scores.buildupResolution;
  dropSustainedEnergyScore = scores.sustainedEnergy;
  dropConfidence = computeDropConfidence(scores, MUSIC_MOTOR_EDM_WEIGHT_ENERGY_RISE, MUSIC_MOTOR_EDM_WEIGHT_SECTION_CONTRAST,
                                          MUSIC_MOTOR_EDM_WEIGHT_BASS_IMPACT, MUSIC_MOTOR_EDM_WEIGHT_BASS_DENSITY,
                                          MUSIC_MOTOR_EDM_WEIGHT_BEAT_DENSITY, MUSIC_MOTOR_EDM_WEIGHT_TRANSIENT_DENSITY,
                                          MUSIC_MOTOR_EDM_WEIGHT_BUILDUP_RESOLUTION, MUSIC_MOTOR_EDM_WEIGHT_SUSTAINED_ENERGY);
  dropConfidenceTier = classifyDropConfidenceTier(dropConfidence, MUSIC_MOTOR_EDM_POSSIBLE_DROP_THRESHOLD,
                                                   MUSIC_MOTOR_EDM_CONFIRMED_DROP_THRESHOLD, MUSIC_MOTOR_EDM_MAJOR_DROP_THRESHOLD);

  // --- track "below sustain floor" dwell BEFORE calling the phase
  // transition, exactly like sustainedHighSinceMs feeds
  // dropHoldSustainQualify in updateMusicMotorController() ---
  if (musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE) {
    if (dropConfidence < MUSIC_MOTOR_EDM_ACTIVE_SUSTAIN_FLOOR) {
      if (dropBelowSustainFloorSinceMs == 0) dropBelowSustainFloorSinceMs = now;
    } else {
      dropBelowSustainFloorSinceMs = 0;
    }
  } else {
    dropBelowSustainFloorSinceMs = 0;
  }

  MusicalSectionPhaseInputs phaseIn{};
  phaseIn.currentPhase = musicalSectionPhase;
  phaseIn.nowMs = now;
  phaseIn.phaseEnteredMs = musicalSectionPhaseEnteredMs;
  phaseIn.dropRefractoryUntilMs = dropRefractoryUntilMs;
  phaseIn.belowSustainFloorSinceMs = dropBelowSustainFloorSinceMs;
  phaseIn.dropConfidenceNow = dropConfidence;
  phaseIn.buildupEntryThreshold = MUSIC_MOTOR_EDM_BUILDUP_ENTRY_THRESHOLD;
  phaseIn.buildupFadeThreshold = MUSIC_MOTOR_EDM_BUILDUP_FADE_THRESHOLD;
  phaseIn.possibleThreshold = MUSIC_MOTOR_EDM_POSSIBLE_DROP_THRESHOLD;
  phaseIn.confirmedThreshold = MUSIC_MOTOR_EDM_CONFIRMED_DROP_THRESHOLD;
  phaseIn.activeSustainFloor = MUSIC_MOTOR_EDM_ACTIVE_SUSTAIN_FLOOR;
  phaseIn.buildupMinMs = MUSIC_MOTOR_EDM_BUILDUP_MIN_MS;
  phaseIn.armedTimeoutMs = MUSIC_MOTOR_EDM_ARMED_TIMEOUT_MS;
  phaseIn.impactConfirmMs = MUSIC_MOTOR_EDM_IMPACT_CONFIRM_MS;
  phaseIn.releaseGraceMs = MUSIC_MOTOR_EDM_RELEASE_GRACE_MS;
  phaseIn.refractoryMs = MUSIC_MOTOR_EDM_REFRACTORY_MS;

  MusicalSectionPhaseResult phaseResult = computeMusicalSectionPhaseTransition(phaseIn);
  logDropSignalEvaluation(now, phaseResult.transitioned);

  if (phaseResult.transitioned) {
    Serial.printf("[MUSIC MOTOR] musical section %s -> %s (%s) confidence=%.2f\n", musicalSectionPhaseName(musicalSectionPhase),
                  musicalSectionPhaseName(phaseResult.newPhase), phaseResult.reason, (double)dropConfidence);
    musicalSectionPhase = phaseResult.newPhase;
    musicalSectionPhaseEnteredMs = now;
    if (musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE) {
      dropActiveSinceMs = now;
      sustainedPhraseStartedThisDrop = false;
      dropEntryNextCheckMs = now;  // allow an entry-opportunity check on the very next eligible tick
      summaryRelativeDropActiveCount++;
    } else if (musicalSectionPhase == MusicalSectionPhase::DROP_RELEASE) {
      if (!sustainedPhraseStartedThisDrop) summaryDropsWithNoSustainedDrive++;
      // Revision 10 -- per-drop phrase/reversal bookkeeping finalizes and
      // resets here ("reset switch eligibility after DROP_RELEASE").
      if (dropPhraseUsedMoreThanOneTypeThisDrop) summaryDropsUsingMultiplePhrases++;
      if (dropConfidenceTier == DropConfidenceTier::MAJOR_DROP) {
        summaryMajorDropDirectionChangesSum += dropPhraseTotalDirectionChangesThisDrop;
        summaryMajorDropCountForAvg++;
      } else {
        summaryConfirmedDropDirectionChangesSum += dropPhraseTotalDirectionChangesThisDrop;
        summaryConfirmedDropCountForAvg++;
      }
      dropActiveSinceMs = 0;
      dropBelowSustainFloorSinceMs = 0;
      dropRefractoryUntilMs = now + MUSIC_MOTOR_EDM_REFRACTORY_MS;
      dropPhraseBoothShakesThisDrop = 0;
      dropPhraseSustainedReversalsThisDrop = 0;
      dropPhraseTotalDirectionChangesThisDrop = 0;
      dropPhraseUsedMoreThanOneTypeThisDrop = false;
    }
  }
}

// Revision 10 -- faster, more decisive drop entry. MAJOR_DROP gets a
// near-immediate high chance from its very first eligible tick (or an
// outright guarantee once effectiveBand is already at least MEDIUM);
// CONFIRMED_DROP keeps the escalation ladder but reaches "guaranteed"
// sooner than Revision 9's original timing. See Config.h's "faster, more
// decisive drop entry" comment. Defined here (ahead of the pure-function
// block further below) because trySustainedDriveEntryFromDrop() just
// below needs the complete type.
struct DropEntryChanceResult {
  uint8_t percent;
  const char *escalation;
};
DropEntryChanceResult pickDropEntryChancePercent(unsigned long sinceActiveMs, DropConfidenceTier tier, bool effectiveBandAtLeastMedium) {
  if (tier == DropConfidenceTier::MAJOR_DROP) {
    if (sinceActiveMs >= MUSIC_MOTOR_EDM_ENTRY_MAJOR_GUARANTEE_AFTER_MS || effectiveBandAtLeastMedium) {
      return {MUSIC_MOTOR_EDM_ENTRY_GUARANTEED_PERCENT, "guaranteed_major"};
    }
    return {MUSIC_MOTOR_EDM_ENTRY_MAJOR_IMMEDIATE_PERCENT, "major_immediate"};
  }
  if (sinceActiveMs >= MUSIC_MOTOR_EDM_ENTRY_CONFIRMED_GUARANTEE_AFTER_MS) {
    return {MUSIC_MOTOR_EDM_ENTRY_GUARANTEED_PERCENT, "guaranteed_confirmed"};
  }
  if (sinceActiveMs >= MUSIC_MOTOR_EDM_ENTRY_ESCALATE_AFTER_MS) {
    return {MUSIC_MOTOR_EDM_ENTRY_ESCALATED_PERCENT, "escalated"};
  }
  return {MUSIC_MOTOR_EDM_ENTRY_INITIAL_PERCENT, "initial"};
}

// Revision 9 sustained-drive entry escalation -- an ADDITIONAL opportunity
// alongside (never a replacement for) trySustainedDriveEntry()'s own
// absolute-band weighted roll. Only reachable while a CONFIRMED_DROP or
// MAJOR_DROP is DROP_ACTIVE and no phrase has started yet during this
// specific drop section; the entry chance escalates the longer the drop
// stays active with zero opportunity taken -- "a prolonged major drop
// cannot pass with zero sustained-drive opportunity."
bool trySustainedDriveEntryFromDrop(unsigned long now) {
  if (!relativeDropDetectionEnabled) return false;
  if (musicalSectionPhase != MusicalSectionPhase::DROP_ACTIVE) return false;
  if ((int)dropConfidenceTier < (int)DropConfidenceTier::CONFIRMED_DROP) return false;
  if (sustainedPhraseStartedThisDrop) return false;
  if ((long)(now - dropEntryNextCheckMs) < 0) return false;
  dropEntryNextCheckMs = now + MUSIC_MOTOR_EDM_ENTRY_REVIEW_MS;

  unsigned long sinceActiveMs = (dropActiveSinceMs != 0) ? (now - dropActiveSinceMs) : 0;
  // Revision 10 -- MAJOR_DROP uses its own much shorter ladder (near-
  // immediate, and guaranteed the instant effectiveBand is already at
  // least MEDIUM); CONFIRMED_DROP's ladder now guarantees sooner too. "Do
  // not allow a major active drop to remain indefinitely in ordinary
  // INTENSITY_SWAY because of random weight-roll failures."
  DropEntryChanceResult entryChance =
      pickDropEntryChancePercent(sinceActiveMs, dropConfidenceTier, (int)effectiveBand() >= (int)MusicIntensityBand::BAND_MEDIUM);
  uint8_t entryChancePercent = entryChance.percent;
  const char *escalation = entryChance.escalation;

  bool alreadyActive = (state == MusicMotorState::SUSTAINED_DRIVE);
  bool silent = (state == MusicMotorState::SILENT);
  bool cooldownReady = (long)(now - sustainedDriveCooldownUntilMs) >= 0;
  uint8_t roll = (uint8_t)random(100);
  bool rollPassed = roll < entryChancePercent;
  bool qualifies = !alreadyActive && !silent && cooldownReady && rollPassed;

  Serial.printf(
      "[MUSIC MOTOR] relative drop entry opportunity escalation=%s chance=%u%% roll=%u alreadyActive=%d silent=%d "
      "cooldownReady=%d confidence=%.2f tier=%s -> %s\n",
      escalation, (unsigned)entryChancePercent, (unsigned)roll, alreadyActive ? 1 : 0, silent ? 1 : 0, cooldownReady ? 1 : 0,
      (double)dropConfidence, dropConfidenceTierName(dropConfidenceTier), qualifies ? "qualified" : "rejected");

  if (!qualifies) {
    if (alreadyActive) summaryRelativeEntryRejectAlreadyActive++;
    else if (silent) summaryRelativeEntryRejectSilent++;
    else if (!cooldownReady) summaryRelativeEntryRejectCooldown++;
    else summaryRelativeEntryRejectRollFailed++;
    return false;
  }

  bool strongAccent = true;  // a confirmed/major active drop always counts as a strong musical accent
  bool consecutiveCapOverride = (dropConfidenceTier == DropConfidenceTier::MAJOR_DROP);
  uint8_t flipDraw = (uint8_t)random(100);
  bool safeToFlip = canReverseNow(now);
  SustainedDriveDirectionChoice choice =
      chooseSustainedDriveDirection(currentDirection, strongAccent, flipDraw, safeToFlip, lastSustainedDriveDirection,
                                     consecutiveSustainedDriveSameDirectionCount, consecutiveCapOverride);
  pendingSustainedDriveDirection = choice.direction;
  sustainedPhraseStartedThisDrop = true;
  return true;
}

// ----------------------------------------------------------------------------
// Revision 10 -- physical choreography and dynamic-range refinement. PURE
// functions only in this block (no Serial, no globals, no random()), same
// testing philosophy as every decision helper above.
// ----------------------------------------------------------------------------

const char *motionTierName(MotionTier t) {
  switch (t) {
    case MotionTier::REST: return "REST";
    case MotionTier::QUIET_BUILDUP: return "QUIET_BUILDUP";
    case MotionTier::MELLOW: return "MELLOW";
    case MotionTier::GROOVE: return "GROOVE";
    case MotionTier::HIGH_ENERGY: return "HIGH_ENERGY";
    case MotionTier::CONFIRMED_DROP_DRIVE: return "CONFIRMED_DROP_DRIVE";
    case MotionTier::MAJOR_DROP_DRIVE: return "MAJOR_DROP_DRIVE";
  }
  return "UNKNOWN";
}

// Classifies the choreography role for THIS tick from the MEASURED band,
// the (Revision 5) lent effective band, and Revision 9's relative-drop
// phase/tier -- purely a LABEL for diagnostics/duty-cycle/summary purposes;
// does not itself compute a speed (see computeSpeedAuthorityCap() below for
// that).
MotionTier classifyMotionTier(MusicIntensityBand measuredBand, MusicIntensityBand effBand, bool dropHoldActiveNow,
                               MusicalSectionPhase phase, DropConfidenceTier tier, bool relativeEnabled,
                               bool quietBuildupQualifies) {
  bool relativeMajor = relativeEnabled && phase == MusicalSectionPhase::DROP_ACTIVE && tier == DropConfidenceTier::MAJOR_DROP;
  bool relativeConfirmed =
      relativeEnabled && phase == MusicalSectionPhase::DROP_ACTIVE && (int)tier >= (int)DropConfidenceTier::CONFIRMED_DROP;
  if ((measuredBand == MusicIntensityBand::BAND_PEAK && dropHoldActiveNow) || relativeMajor) return MotionTier::MAJOR_DROP_DRIVE;
  if (effBand == MusicIntensityBand::BAND_PEAK || dropHoldActiveNow || relativeConfirmed) return MotionTier::CONFIRMED_DROP_DRIVE;
  if (measuredBand == MusicIntensityBand::BAND_HIGH || effBand == MusicIntensityBand::BAND_HIGH) return MotionTier::HIGH_ENERGY;
  if (measuredBand == MusicIntensityBand::BAND_MEDIUM) return MotionTier::GROOVE;
  if (measuredBand == MusicIntensityBand::BAND_LOW) return MotionTier::MELLOW;
  return quietBuildupQualifies ? MotionTier::QUIET_BUILDUP : MotionTier::REST;
}

// Bounded lending -- returns the MAXIMUM commanded percent allowed this
// tick (a ceiling, 100 = uncapped/full authority), keyed to the CURRENT
// MEASURED band (not the lent one). The caller applies
// min(desiredPercent, ceilingPercent) wherever a lent value (intensityTargetPercent,
// or a SUSTAINED_DRIVE speed floor) could otherwise exceed it. See
// Config.h's "speed-authority cap" comment for the full rationale.
struct SpeedAuthorityCapResult {
  uint8_t ceilingPercent;
  const char *source;
};
SpeedAuthorityCapResult computeSpeedAuthorityCap(MusicIntensityBand measuredBand, bool quietGraceExpired, uint8_t quietCapPercent,
                                                  uint8_t mellowCapPercent, uint8_t naturalMediumPercent,
                                                  uint8_t mediumBoundedRaisePercent) {
  switch (measuredBand) {
    case MusicIntensityBand::BAND_QUIET:
      if (!quietGraceExpired) return {100, "none_grace"};
      return {quietCapPercent, "quiet_cap"};
    case MusicIntensityBand::BAND_LOW:
      return {mellowCapPercent, "mellow_cap"};
    case MusicIntensityBand::BAND_MEDIUM: {
      uint16_t cap = (uint16_t)naturalMediumPercent + (uint16_t)mediumBoundedRaisePercent;
      if (cap > 100) cap = 100;
      return {(uint8_t)cap, "medium_bounded_raise_cap"};
    }
    default:
      return {100, "none_full_authority"};
  }
}

// Distinguishes a genuine quiet MUSICAL buildup from an empty, silent room
// -- see Config.h's "Quiet-buildup alive qualification" comment.
bool computeQuietBuildupQualification(bool relativeEnabled, MusicalSectionPhase phase, float audioEnergyNow, float roomNoiseFloor,
                                       unsigned long quietDurationMs, unsigned long maxQuietMs) {
  if (!relativeEnabled) return false;
  if (phase != MusicalSectionPhase::BUILDUP && phase != MusicalSectionPhase::DROP_ARMED) return false;
  if (audioEnergyNow <= roomNoiseFloor) return false;
  if (quietDurationMs > maxQuietMs) return false;
  return true;
}

// Movement duty-cycle window transition (QUIET_BUILDUP/MELLOW pulse/rest
// choreography) -- `pulseDurationMs`/`restDurationMs` are the caller's
// already-randomized picks (mirrors randomRangeU32()'s contract, same
// purity convention as chooseRampDownDurationMs() above).
struct MotionDutyTransitionResult {
  bool pulseOn;
  bool changed;
  unsigned long nextWindowEndMs;
};
MotionDutyTransitionResult computeMotionDutyTransition(bool currentPulseOn, unsigned long now, unsigned long windowEndMs,
                                                         uint32_t pulseDurationMs, uint32_t restDurationMs) {
  MotionDutyTransitionResult r{currentPulseOn, false, windowEndMs};
  if ((long)(now - windowEndMs) < 0) return r;
  r.pulseOn = !currentPulseOn;
  r.changed = true;
  r.nextWindowEndMs = now + (r.pulseOn ? pulseDurationMs : restDurationMs);
  return r;
}

// (pickDropEntryChancePercent()/DropEntryChanceResult moved above, right
// before trySustainedDriveEntryFromDrop() -- their only caller defined
// earlier in the file needed the complete type, not just a declaration.)

const char *dropPhraseTypeName(DropPhraseType t) {
  switch (t) {
    case DropPhraseType::FULL_SUSTAIN: return "FULL_SUSTAIN";
    case DropPhraseType::SUSTAINED_REVERSAL: return "SUSTAINED_REVERSAL";
    case DropPhraseType::DROP_BOOTY_SHAKE: return "DROP_BOOTY_SHAKE";
    case DropPhraseType::DROP_PUNCH_AND_HOLD: return "DROP_PUNCH_AND_HOLD";
    case DropPhraseType::DOUBLE_PUNCH: return "DOUBLE_PUNCH";
    case DropPhraseType::SUSTAIN_WITH_ACCENTS: return "SUSTAIN_WITH_ACCENTS";
  }
  return "UNKNOWN";
}

// Evidence-weighted, eligibility-gated drop-phrase selection -- "randomness
// may add variation only after eligibility is established." Eligibility
// (per-drop limits, minimum active duration) excludes a phrase from the
// candidate set entirely; among the SURVIVING candidates, evidence sets the
// base weight and a roll (explicit parameter, mirrors random(1000)) makes
// the final pick. See Config.h's "Drop choreography phrase vocabulary"
// comment for the limits referenced here.
struct DropPhraseEvidence {
  float sustainedEnergyScore = 0.0f;
  float beatDensityScore = 0.0f;
  float bassDensityScore = 0.0f;
  float transientDensityScore = 0.0f;
  bool isReselection = false;  // false = initial phrase pick at phrase entry
  bool freshImpactCue = false;
  unsigned long dropActiveElapsedMs = 0;
  uint8_t boothShakesUsedThisDrop = 0;
  uint8_t sustainedReversalsUsedThisDrop = 0;
  uint8_t maxBoothShakesPerDrop = 0;
  uint8_t maxSustainedReversalsPerDrop = 0;
  DropPhraseType lastPhraseUsed = DropPhraseType::FULL_SUSTAIN;
  bool hasLastPhrase = false;
  uint32_t minActiveMsForReversal = 0;
  uint32_t minActiveMsForShake = 0;
  float antiRepeatMultiplier = 1.0f;
};
struct DropPhraseChoice {
  DropPhraseType type;
  const char *reason;
};
DropPhraseChoice selectDropPhraseType(const DropPhraseEvidence &e, uint16_t weightRoll0to999) {
  struct Candidate {
    DropPhraseType type;
    uint16_t weight;
    const char *reason;
  };
  Candidate candidates[6];
  int n = 0;

  bool reversalEligible = e.sustainedReversalsUsedThisDrop < e.maxSustainedReversalsPerDrop &&
                           e.dropActiveElapsedMs >= e.minActiveMsForReversal;
  bool shakeEligible =
      e.boothShakesUsedThisDrop < e.maxBoothShakesPerDrop && e.dropActiveElapsedMs >= e.minActiveMsForShake;
  bool introPhraseEligible = !e.isReselection || e.freshImpactCue;

  uint16_t fullSustainWeight = 30;
  if (e.sustainedEnergyScore > 0.6f && e.beatDensityScore < 0.4f) fullSustainWeight += 25;
  candidates[n++] = {DropPhraseType::FULL_SUSTAIN, fullSustainWeight, "smooth_sustained_energy"};

  uint16_t accentsWeight = 20;
  if (e.beatDensityScore >= 0.3f && e.beatDensityScore < 0.7f) accentsWeight += 15;
  candidates[n++] = {DropPhraseType::SUSTAIN_WITH_ACCENTS, accentsWeight, "moderate_beat_density"};

  if (shakeEligible) {
    uint16_t shakeWeight = 10;
    if (e.beatDensityScore >= 0.6f && e.bassDensityScore >= 0.6f) shakeWeight += 40;
    candidates[n++] = {DropPhraseType::DROP_BOOTY_SHAKE, shakeWeight, "dense_bass_cluster"};
  }

  if (introPhraseEligible) {
    uint16_t punchWeight = e.isReselection ? 8 : 15;
    if (e.freshImpactCue) punchWeight += 15;
    candidates[n++] = {DropPhraseType::DROP_PUNCH_AND_HOLD, punchWeight, e.isReselection ? "fresh_impact_cue" : "dramatic_opening_impact"};
  }

  if (introPhraseEligible && e.transientDensityScore >= 0.5f) {
    uint16_t doubleWeight = (uint16_t)((e.isReselection ? 6 : 10) + (e.transientDensityScore * 20.0f));
    candidates[n++] = {DropPhraseType::DOUBLE_PUNCH, doubleWeight, "repeated_clustered_impacts"};
  }

  if (reversalEligible) {
    uint16_t reversalWeight = e.isReselection ? 12 : 6;
    if (e.isReselection && e.freshImpactCue) reversalWeight += 30;
    candidates[n++] = {DropPhraseType::SUSTAINED_REVERSAL, reversalWeight, "impact_during_long_phrase"};
  }

  if (e.hasLastPhrase && n > 1) {
    for (int i = 0; i < n; i++) {
      if (candidates[i].type == e.lastPhraseUsed) {
        candidates[i].weight = (uint16_t)(candidates[i].weight * e.antiRepeatMultiplier);
        if (candidates[i].weight == 0) candidates[i].weight = 1;
      }
    }
  }

  uint32_t total = 0;
  for (int i = 0; i < n; i++) total += candidates[i].weight;
  uint32_t pick = (total == 0) ? 0 : ((uint32_t)weightRoll0to999 * total) / 1000;
  uint32_t cursor = 0;
  for (int i = 0; i < n; i++) {
    cursor += candidates[i].weight;
    if (pick < cursor) return {candidates[i].type, candidates[i].reason};
  }
  return {DropPhraseType::FULL_SUSTAIN, "fallback"};
}

// Builds the non-blocking step sequence for a chosen phrase type -- pure
// (explicit punch durations, mirrors randomRangeU32()'s contract). DROP_BOOTY_SHAKE
// alternates starting opposite of currentDir, with the terminal sustain
// continuing the alternation one more step (matches the documented
// "F-R-F-SUSTAIN_R" example shape). Returns the step count written into
// `out` (capped at MAX_DROP_PHRASE_STEPS).
uint8_t buildDropPhraseSteps(DropPhraseType type, MusicMotorDirection currentDir, uint8_t dropSpeedPercent, uint32_t punchDurationMs1,
                              uint32_t punchDurationMs2, uint32_t punchDurationMs3, DropPhraseStep out[MAX_DROP_PHRASE_STEPS]) {
  MusicMotorDirection opp = oppositeOf(currentDir);
  switch (type) {
    case DropPhraseType::FULL_SUSTAIN:
    case DropPhraseType::SUSTAIN_WITH_ACCENTS:
      out[0] = {false, currentDir, dropSpeedPercent, 0};
      return 1;
    case DropPhraseType::SUSTAINED_REVERSAL:
      out[0] = {false, opp, dropSpeedPercent, 0};
      return 1;
    case DropPhraseType::DROP_PUNCH_AND_HOLD:
      out[0] = {true, opp, MUSIC_MOTOR_DROP_PHRASE_PUNCH_PERCENT, punchDurationMs1};
      out[1] = {false, currentDir, dropSpeedPercent, 0};
      return 2;
    case DropPhraseType::DOUBLE_PUNCH:
      out[0] = {true, opp, MUSIC_MOTOR_DROP_PHRASE_PUNCH_PERCENT, punchDurationMs1};
      out[1] = {true, currentDir, MUSIC_MOTOR_DROP_PHRASE_PUNCH_PERCENT, punchDurationMs2};
      out[2] = {false, currentDir, dropSpeedPercent, 0};
      return 3;
    case DropPhraseType::DROP_BOOTY_SHAKE: {
      MusicMotorDirection d = opp;
      uint32_t durations[3] = {punchDurationMs1, punchDurationMs2, punchDurationMs3};
      for (int i = 0; i < 3; i++) {
        out[i] = {true, d, MUSIC_MOTOR_DROP_PHRASE_PUNCH_PERCENT, durations[i]};
        d = oppositeOf(d);
      }
      out[3] = {false, d, dropSpeedPercent, 0};
      return 4;
    }
  }
  out[0] = {false, currentDir, dropSpeedPercent, 0};
  return 1;
}

// Performs the actual coast+flip request (mutating) -- only ever called
// once checkReversalGate() has already passed.
// Which validated sustained speed floor a drop phrase should target right
// now -- mirrors the SUSTAINED_DRIVE floor-tier selection in
// updateMusicMotorController() exactly (kept as a single shared function so
// the two can never drift apart).
uint8_t currentDropSpeedFloorPercent() {
  bool absolutePeakDropHold = (intensityBand == MusicIntensityBand::BAND_PEAK) && dropHoldActive;
  bool relativeMajorDropActive = relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                                  dropConfidenceTier == DropConfidenceTier::MAJOR_DROP;
  if (absolutePeakDropHold || relativeMajorDropActive) return MUSIC_MOTOR_SUSTAINED_PEAK_FLOOR_PERCENT;
  return MUSIC_MOTOR_SUSTAINED_PERFORMANCE_FLOOR_PERCENT;
}

// Selects a drop choreography phrase (evidence-weighted, per-drop-limit
// gated) and builds its non-blocking step sequence -- called once at
// SUSTAINED_DRIVE entry (isReselection=false) and again whenever
// maybeTriggerDropPhraseReselection() below detects a qualifying mid-drop
// cue (isReselection=true). See selectDropPhraseType()/buildDropPhraseSteps()
// above for the pure logic this wraps.
// `baseDirection` is the direction the phrase's steps are built relative
// to. At ENTRY (isReselection=false) this must be the newly-chosen
// sustained-drive direction, NOT the (possibly stale, about-to-flip)
// currentDirection global -- by the time the sequencer actually starts
// executing, updateMusicMotorController()'s shared coastingForReversal gate
// has already completed any entry-time coast+flip, so currentDirection is
// guaranteed to equal baseDirection by then. At a mid-drop RESELECTION,
// currentDirection is already valid/current, so the caller passes that.
void selectAndBeginDropPhrase(unsigned long now, bool isReselection, bool freshImpactCue, MusicMotorDirection baseDirection) {
  uint8_t dropSpeedPercent = currentDropSpeedFloorPercent();

  // Revision 10 -- respect the general reversal-safety gate (minimum
  // direction-hold time / reversal cooldown / post-spin hold -- the same
  // gate every other reversal path in this file goes through) before ever
  // choosing a phrase that could change polarity. If unsafe right now,
  // fall back to the safe no-reversal default instead of risking a
  // violation -- "emergency stop must interrupt it immediately" and
  // "obey the minimum safe reversal interval" apply here too.
  if (!canReverseNow(now)) {
    summarySafetyBlockedReversalCount++;
    dropPhraseStepCount =
        buildDropPhraseSteps(DropPhraseType::FULL_SUSTAIN, baseDirection, dropSpeedPercent, 0, 0, 0, dropPhraseSteps);
    dropPhraseStepIndex = 0;
    dropPhraseTransitionPhase = DropPhraseTransitionPhase::DRIVING;
    currentDropPhraseType = DropPhraseType::FULL_SUSTAIN;
    lastDropPhraseSelectionReason = "reversal_unsafe_fallback";
    return;
  }

  DropPhraseEvidence ev{};
  ev.sustainedEnergyScore = dropSustainedEnergyScore;
  ev.beatDensityScore = dropBeatDensityScore;
  ev.bassDensityScore = dropBassDensityScore;
  ev.transientDensityScore = dropTransientDensityScore;
  ev.isReselection = isReselection;
  ev.freshImpactCue = freshImpactCue;
  ev.dropActiveElapsedMs = (dropActiveSinceMs != 0) ? (now - dropActiveSinceMs) : 0;
  ev.boothShakesUsedThisDrop = dropPhraseBoothShakesThisDrop;
  ev.sustainedReversalsUsedThisDrop = dropPhraseSustainedReversalsThisDrop;
  ev.maxBoothShakesPerDrop = MUSIC_MOTOR_DROP_PHRASE_MAX_BOOTY_SHAKES_PER_DROP;
  uint8_t maxReversals = tunableDropPhraseSwitchLimit;
  if (ev.dropActiveElapsedMs >= MUSIC_MOTOR_DROP_PHRASE_LONG_DROP_EXTRA_REVERSAL_MS) maxReversals++;
  ev.maxSustainedReversalsPerDrop = maxReversals;
  ev.lastPhraseUsed = lastDropPhraseUsed;
  ev.hasLastPhrase = hasLastDropPhrase;
  ev.minActiveMsForReversal = MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_REVERSAL;
  ev.minActiveMsForShake = MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_SHAKE;
  ev.antiRepeatMultiplier = MUSIC_MOTOR_DROP_PHRASE_ANTIREPEAT_WEIGHT_MULTIPLIER;

  uint16_t roll = (uint16_t)randomRangeU32(0, 999);
  DropPhraseChoice choice = selectDropPhraseType(ev, roll);

  uint32_t d1 = randomRangeU32(MUSIC_MOTOR_DROP_PHRASE_PUNCH_MIN_MS, MUSIC_MOTOR_DROP_PHRASE_PUNCH_MAX_MS);
  uint32_t d2 = randomRangeU32(MUSIC_MOTOR_DROP_PHRASE_PUNCH_MIN_MS, MUSIC_MOTOR_DROP_PHRASE_PUNCH_MAX_MS);
  uint32_t d3 = randomRangeU32(MUSIC_MOTOR_DROP_PHRASE_PUNCH_MIN_MS, MUSIC_MOTOR_DROP_PHRASE_PUNCH_MAX_MS);
  dropPhraseStepCount = buildDropPhraseSteps(choice.type, baseDirection, dropSpeedPercent, d1, d2, d3, dropPhraseSteps);
  dropPhraseStepIndex = 0;
  dropPhraseTransitionPhase = DropPhraseTransitionPhase::DRIVING;
  currentDropPhraseType = choice.type;
  lastDropPhraseSelectionReason = choice.reason;

  uint8_t directionChangesThisPhrase = 0;
  for (uint8_t i = 0; i < dropPhraseStepCount; i++) {
    MusicMotorDirection prevDir = (i == 0) ? baseDirection : dropPhraseSteps[i - 1].direction;
    if (dropPhraseSteps[i].direction != prevDir) directionChangesThisPhrase++;
  }
  dropPhraseTotalDirectionChangesThisDrop += directionChangesThisPhrase;
  if (dropPhraseTotalDirectionChangesThisDrop > summaryMaxDirectionChangesInOneDrop)
    summaryMaxDirectionChangesInOneDrop = dropPhraseTotalDirectionChangesThisDrop;

  switch (choice.type) {
    case DropPhraseType::FULL_SUSTAIN: summaryFullSustainCount++; break;
    case DropPhraseType::SUSTAINED_REVERSAL:
      summarySustainedReversalCount++;
      dropPhraseSustainedReversalsThisDrop++;
      break;
    case DropPhraseType::DROP_BOOTY_SHAKE:
      summaryDropBootyShakeCount++;
      dropPhraseBoothShakesThisDrop++;
      break;
    case DropPhraseType::DROP_PUNCH_AND_HOLD: summaryDropPunchAndHoldCount++; break;
    case DropPhraseType::DOUBLE_PUNCH: summaryDoublePunchCount++; break;
    case DropPhraseType::SUSTAIN_WITH_ACCENTS: summarySustainWithAccentsCount++; break;
  }
  if (hasLastDropPhrase) {
    if (lastDropPhraseUsed == choice.type) summaryRepeatedIdenticalPhraseSelections++;
    else dropPhraseUsedMoreThanOneTypeThisDrop = true;
  }
  lastDropPhraseUsed = choice.type;
  hasLastDropPhrase = true;
  if (choice.type != DropPhraseType::FULL_SUSTAIN && choice.type != DropPhraseType::SUSTAIN_WITH_ACCENTS) {
    dropPhraseSequenceReadyMs = now + tunableDropPhraseSequenceCooldownMs;
  }

  Serial.printf(
      "[MUSIC MOTOR] drop phrase selected=%s tier=%s reason=%s beatDensity=%.2f bassDensity=%.2f sustainedEnergy=%.2f "
      "steps=%u\n",
      dropPhraseTypeName(choice.type), dropConfidenceTierName(dropConfidenceTier), choice.reason, (double)dropBeatDensityScore,
      (double)dropBassDensityScore, (double)dropSustainedEnergyScore, (unsigned)dropPhraseStepCount);
}

// Detects a qualifying mid-drop reselection cue -- only reachable once the
// intro sequencer is idle (steady-state sustain), a CONFIRMED/MAJOR drop
// remains DROP_ACTIVE, minimum commitment has elapsed, and the post-
// sequence cooldown has cleared. "No meaningful musical cue -> continue
// current phrase instead of changing randomly": this returns false far
// more often than true. `rollPercent0to99` gates how often a qualifying
// cue is actually ACTED on ("occasionally, not constantly") -- mirrors
// random(100), tunable via 'musicmotor switchchance'.
bool maybeTriggerDropPhraseReselection(unsigned long now, bool qualifyingStrongHitCue, uint8_t rollPercent0to99,
                                        uint8_t switchChancePercent) {
  if (dropPhraseStepIndex < dropPhraseStepCount) return false;  // intro sequence still running
  if (!relativeDropDetectionEnabled) return false;
  if (musicalSectionPhase != MusicalSectionPhase::DROP_ACTIVE) return false;
  if ((int)dropConfidenceTier < (int)DropConfidenceTier::CONFIRMED_DROP) return false;
  if ((now - sustainedDriveDirectionCommitStartMs) < sustainedDriveMinCommitmentMs) return false;
  if ((long)(now - dropPhraseSequenceReadyMs) < 0) return false;

  unsigned long dropActiveElapsedMs = (dropActiveSinceMs != 0) ? (now - dropActiveSinceMs) : 0;
  bool freshImpactCue = strongHitDetectedThisTick || bassImpactDelta >= MUSIC_MOTOR_BASS_IMPACT_DELTA_THRESHOLD;
  bool longNoSwitchCue = dropActiveElapsedMs >= MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_REVERSAL * 2 &&
                          dropPhraseTotalDirectionChangesThisDrop == 0;

  bool cueFired = qualifyingStrongHitCue || (freshImpactCue && dropActiveElapsedMs >= MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_SHAKE) ||
                   longNoSwitchCue;
  if (!cueFired) return false;
  if (rollPercent0to99 >= switchChancePercent) return false;

  selectAndBeginDropPhrase(now, /*isReselection=*/true, freshImpactCue || qualifyingStrongHitCue, currentDirection);
  return true;
}

// Advances the current phrase's non-blocking step sequencer by one tick.
// Returns true while the intro sequence (punches/reversal preamble) is
// still running -- the caller (updateSustainedDrive()) should skip its
// normal review/continuation logic entirely while this is true, and only
// resume it once the sequencer reaches its terminal open-ended sustain
// step. Every direction change goes through a brief RAMP_DOWN (shared
// rampFromPercent/rampToPercent/applyRampTick() ramp, same infrastructure
// BASS_ACCENT/DECELERATING/EXTENDED_SPIN already use) then the existing
// coast-before-flip hardware-safety primitive -- never an instantaneous
// full-forward-to-full-reverse command.
bool advanceDropPhraseStepSequencer(unsigned long now) {
  if (dropPhraseStepIndex >= dropPhraseStepCount) return false;
  DropPhraseStep &step = dropPhraseSteps[dropPhraseStepIndex];
  // Revision 10.1 fix -- MUST compare against the LIVE currentDirection,
  // never the previous array step's direction. currentDirection is kept in
  // sync with the step actually being executed (set the instant a
  // COAST->DRIVING transition completes, below); once it matches
  // step.direction, directionChanges correctly becomes false and driving
  // proceeds. Comparing against dropPhraseSteps[index-1].direction instead
  // (the original bug) is a FIXED array value that never changes, so once
  // index>0 needed a direction change, this check stayed true forever --
  // an infinite RAMP_DOWN->COAST->RAMP_DOWN loop that never actually
  // drove the motor. See the Revision 10.1 report for the full trace.
  bool directionChanges = step.direction != currentDirection;

  switch (dropPhraseTransitionPhase) {
    case DropPhraseTransitionPhase::DRIVING:
      if (directionChanges) {
        // First time reaching this step and it requires a polarity change
        // -- ramp current speed down before coasting. Never an
        // instantaneous full-forward-to-full-reverse command.
        rampFromPercent = currentSpeedPercent;
        rampToPercent = 0;
        rampStartMs = now;
        rampDurationMs = MUSIC_MOTOR_DROP_PHRASE_DECEL_MS;
        dropPhraseTransitionPhase = DropPhraseTransitionPhase::DECEL;
        return true;
      }
      // Same direction as the previous step (or the phrase's own entry
      // direction) -- drive straight toward this step's target.
      intensityTargetPercent = step.targetPercent;
      updateAppliedSpeedTowardTarget(now);
      if (!step.isPunch) {
        dropPhraseStepIndex = dropPhraseStepCount;  // terminal sustain step -- sequencer done, steady state resumes
        return false;
      }
      if (dropPhraseStepDeadlineMs == 0) dropPhraseStepDeadlineMs = now + step.durationMs;
      if ((long)(now - dropPhraseStepDeadlineMs) >= 0) {
        dropPhraseStepDeadlineMs = 0;
        dropPhraseStepIndex++;
      }
      return true;
    case DropPhraseTransitionPhase::DECEL:
      applyRampTick(now);
      if ((long)(now - (rampStartMs + rampDurationMs)) < 0) return true;
      currentSpeedPercent = 0;
      motorPWMCoast();
      coastEndMs = now + MUSIC_MOTOR_REVERSE_COAST_MS;
      dropPhraseTransitionPhase = DropPhraseTransitionPhase::COAST;
      return true;
    case DropPhraseTransitionPhase::COAST:
      if ((long)(now - coastEndMs) < 0) return true;
      currentDirection = step.direction;
      directionStartMs = now;
      Serial.printf("[MUSIC MOTOR] drop phrase step direction=%s target=M%u punch=%d\n", dirName(currentDirection),
                    (unsigned)step.targetPercent, step.isPunch ? 1 : 0);
      dropPhraseTransitionPhase = DropPhraseTransitionPhase::DRIVING;
      dropPhraseStepDeadlineMs = 0;
      return true;
  }
  return true;
}

// ----------------------------------------------------------------------------
// Revision 10.1 -- centralized invariant/recovery, defense-in-depth on top
// of the actual root-cause fix above. Invariant: while SUSTAINED_DRIVE
// wants nonzero movement (intensityTargetPercent > 0), the motor must be
// EITHER actually applying it (currentSpeedPercent > 0) OR inside an
// explicitly tracked DECEL/COAST transition. A brief single-tick gap while
// a legitimate transition is still resolving is expected and NOT a
// violation; a "wants to move, is stopped, isn't transitioning" state that
// PERSISTS beyond MUSIC_MOTOR_SUSTAINED_DRIVE_INVARIANT_GRACE_MS is
// provably invalid. Recovery: abort the corrupt phrase, attempt one clean
// restart via the safe FULL_SUSTAIN fallback (reuses the same
// coast-before-flip step builder as any other phrase step -- never bypasses
// reversal safety); if that itself is unsafe right now (canReverseNow()
// false), exit SUSTAINED_DRIVE cleanly instead of forcing a reversal.
// Never touches emergency-stop state -- hardStop()/cancelMusicMotorController()
// already bypass this entire module before this function could run.
// ----------------------------------------------------------------------------
bool checkSustainedDriveInvariant(unsigned long now) {
  if (state != MusicMotorState::SUSTAINED_DRIVE) {
    dropPhraseInvariantViolationSinceMs = 0;
    return false;
  }
  bool legitimateTransition = (dropPhraseStepIndex < dropPhraseStepCount) &&
                               (dropPhraseTransitionPhase == DropPhraseTransitionPhase::DECEL ||
                                dropPhraseTransitionPhase == DropPhraseTransitionPhase::COAST);
  bool desiredMovement = intensityTargetPercent > 0;
  bool violating = desiredMovement && currentSpeedPercent == 0 && !legitimateTransition;
  if (!violating) {
    dropPhraseInvariantViolationSinceMs = 0;
    return false;
  }
  if (dropPhraseInvariantViolationSinceMs == 0) {
    dropPhraseInvariantViolationSinceMs = now;
    return false;  // grace period just started -- do not act on a single tick
  }
  summaryUnexpectedStoppedMs += MUSIC_MOTOR_TICK_MS;
  if ((now - dropPhraseInvariantViolationSinceMs) < MUSIC_MOTOR_SUSTAINED_DRIVE_INVARIANT_GRACE_MS) return false;

  summarySustainedDriveStoppedInvariantCount++;
  bool safeToRestart = canReverseNow(now);
  Serial.printf(
      "[MUSIC MOTOR] INVALID STATE: sustained drive stopped outside transition desiredDirection=%s target=M%u "
      "phrase=%s step=%u/%u transitionPhase=%s stoppedForMs=%lu recovery=%s\n",
      dirName(currentDirection), (unsigned)intensityTargetPercent, dropPhraseTypeName(currentDropPhraseType),
      (unsigned)dropPhraseStepIndex, (unsigned)dropPhraseStepCount,
      dropPhraseTransitionPhase == DropPhraseTransitionPhase::DRIVING
          ? "DRIVING"
          : (dropPhraseTransitionPhase == DropPhraseTransitionPhase::DECEL ? "RAMP_DOWN" : "COAST"),
      (unsigned long)(now - dropPhraseInvariantViolationSinceMs), safeToRestart ? "RESTART_FULL_SUSTAIN" : "EXIT_SUSTAINED_DRIVE");
  dropPhraseInvariantViolationSinceMs = 0;

  if (safeToRestart) {
    summaryDropPhraseRecoveryCount++;
    uint8_t dropSpeedPercent = currentDropSpeedFloorPercent();
    dropPhraseStepCount =
        buildDropPhraseSteps(DropPhraseType::FULL_SUSTAIN, currentDirection, dropSpeedPercent, 0, 0, 0, dropPhraseSteps);
    dropPhraseStepIndex = 0;
    dropPhraseTransitionPhase = DropPhraseTransitionPhase::DRIVING;
    currentDropPhraseType = DropPhraseType::FULL_SUSTAIN;
    lastDropPhraseSelectionReason = "invariant_recovery";
    currentSpeedPercent = 0;  // forces the DRIVING branch's snap-from-dead-stop path on the very next tick
  } else {
    summaryDropPhraseAbortCount++;
    exitSustainedDrive(now, "invariant_recovery_unsafe");
  }
  return true;
}

// Classifies this tick's MotionTier, applies the speed-authority cap to
// the just-computed intensityTargetPercent, and accumulates the
// tier-time summary counters. Called once per tick from
// updateMusicMotorController(), after intensityTargetPercent's initial
// (Revision 5-9) computation and BEFORE the SUSTAINED_DRIVE floor block --
// the floor block additionally clamps its own floor to
// lastAuthorityCapPercent (see its own comment) so a decaying drop can
// never hold an unjustified maximum through genuinely quiet/mellow audio.
void applyMotionDynamics(unsigned long now, MusicIntensityBand effBand) {
  if (intensityBand == MusicIntensityBand::BAND_QUIET) {
    if (measuredQuietSinceMs == 0) measuredQuietSinceMs = now;
  } else {
    measuredQuietSinceMs = 0;
  }
  bool quietGraceExpired = measuredQuietSinceMs != 0 && (now - measuredQuietSinceMs) >= MUSIC_MOTOR_QUIET_CAP_GRACE_MS;

  bool quietBuildupQualifies =
      quietBuildupMotionEnabled &&
      computeQuietBuildupQualification(relativeDropDetectionEnabled, musicalSectionPhase, max(rawEnergy, fastEnergy),
                                        MUSIC_MOTOR_ROOM_NOISE_FLOOR, measuredQuietSinceMs != 0 ? (now - measuredQuietSinceMs) : 0,
                                        MUSIC_MOTOR_QUIET_BUILDUP_MAX_QUIET_MS);

  currentMotionTier = classifyMotionTier(intensityBand, effBand, dropHoldActive, musicalSectionPhase, dropConfidenceTier,
                                          relativeDropDetectionEnabled, quietBuildupQualifies);

  uint8_t naturalMediumPercent = computeRawIntensityTargetPercent(MusicIntensityBand::BAND_MEDIUM, songEnergy);
  SpeedAuthorityCapResult cap =
      computeSpeedAuthorityCap(intensityBand, quietGraceExpired, MUSIC_MOTOR_MOTION_QUIET_BUILDUP_PERCENT,
                                MUSIC_MOTOR_MOTION_MELLOW_MAX_PERCENT, naturalMediumPercent,
                                MUSIC_MOTOR_MEDIUM_LENDING_BOUNDED_RAISE_PERCENT);
  lastAuthorityCapPercent = cap.ceilingPercent;
  lastAuthorityCapSource = cap.source;
  if (intensityTargetPercent > cap.ceilingPercent) intensityTargetPercent = cap.ceilingPercent;

  switch (currentMotionTier) {
    case MotionTier::MELLOW: summaryMellowMotionMs += MUSIC_MOTOR_TICK_MS; break;
    case MotionTier::GROOVE: summaryGrooveMotionMs += MUSIC_MOTOR_TICK_MS; break;
    case MotionTier::CONFIRMED_DROP_DRIVE: summaryConfirmedDropDriveMs += MUSIC_MOTOR_TICK_MS; break;
    case MotionTier::MAJOR_DROP_DRIVE: summaryMajorDropDriveMs += MUSIC_MOTOR_TICK_MS; break;
    default: break;
  }
}

// Movement duty-cycle gate (QUIET_BUILDUP/MELLOW pulse/rest choreography)
// -- see Config.h's "Movement duty cycle" comment. Only meaningful while
// state==INTENSITY_SWAY (the "continuous movement" state); called from
// updateIntensitySway() immediately before its own tail call to
// updateAppliedSpeedTowardTarget(). During a "rest" window this zeroes
// intensityTargetPercent for THIS tick only -- the existing ramp mechanism
// then coasts down/back up naturally, giving "gradual ramps" and "gentle
// partial sways" for free, with no new ramp logic required. Revision 10.1
// fix -- QUIET_BUILDUP's PULSE_ON window must explicitly supply the M80
// target: the "natural" intensityTargetPercent computed from the absolute
// QUIET band is itself ~0, so leaving it alone (the original bug) produced
// a pulse window with nothing to actually pulse toward -- silently
// motionless "movement." MELLOW's natural target (already M80-83 from the
// LOW band) needs no override. DROP_ARMED gets a shorter rest than plain
// BUILDUP -- "noticeably expectant motion," denser than early buildup.
void applyMotionDutyGateIfNeeded(unsigned long now) {
  if (currentMotionTier != MotionTier::QUIET_BUILDUP && currentMotionTier != MotionTier::MELLOW) {
    motionDutyPulseOn = true;
    return;
  }
  uint32_t pulseMs = (currentMotionTier == MotionTier::QUIET_BUILDUP)
                          ? randomRangeU32(MUSIC_MOTOR_QUIET_BUILDUP_PULSE_MIN_MS, MUSIC_MOTOR_QUIET_BUILDUP_PULSE_MAX_MS)
                          : randomRangeU32(MUSIC_MOTOR_MELLOW_PULSE_MIN_MS, MUSIC_MOTOR_MELLOW_PULSE_MAX_MS);
  uint32_t restMs;
  if (currentMotionTier == MotionTier::QUIET_BUILDUP && musicalSectionPhase == MusicalSectionPhase::DROP_ARMED) {
    restMs = randomRangeU32(MUSIC_MOTOR_DROP_ARMED_REST_MIN_MS, MUSIC_MOTOR_DROP_ARMED_REST_MAX_MS);
  } else if (currentMotionTier == MotionTier::QUIET_BUILDUP) {
    restMs = randomRangeU32(MUSIC_MOTOR_QUIET_BUILDUP_REST_MIN_MS, MUSIC_MOTOR_QUIET_BUILDUP_REST_MAX_MS);
  } else {
    restMs = randomRangeU32(MUSIC_MOTOR_MELLOW_REST_MIN_MS, MUSIC_MOTOR_MELLOW_REST_MAX_MS);
  }
  MotionDutyTransitionResult r = computeMotionDutyTransition(motionDutyPulseOn, now, motionDutyWindowEndMs, pulseMs, restMs);
  if (r.changed) {
    motionDutyPulseOn = r.pulseOn;
    motionDutyWindowEndMs = r.nextWindowEndMs;
    if (motionDutyPulseOn && currentMotionTier == MotionTier::QUIET_BUILDUP) {
      lastQuietBuildupMotionMs = now;
      summaryQuietBuildupMotionCount++;
    }
    if (debugLoggingEnabled) {
      Serial.printf("[MUSIC MOTOR] motion duty %s tier=%s windowMs=%lu\n", motionDutyPulseOn ? "PULSE_ON" : "REST",
                    motionTierName(currentMotionTier), (unsigned long)(motionDutyWindowEndMs - now));
    }
  }
  if (!motionDutyPulseOn) {
    intensityTargetPercent = 0;
  } else if (currentMotionTier == MotionTier::QUIET_BUILDUP) {
    intensityTargetPercent = MUSIC_MOTOR_MOTION_QUIET_BUILDUP_PERCENT;
  }
}

bool tryRequestReversal(unsigned long now, ReversalRejectReason &reason) {
  if (!checkReversalGate(now, reason)) return false;
  pendingDirection = (currentDirection == MusicMotorDirection::FORWARD) ? MusicMotorDirection::REVERSE
                                                                          : MusicMotorDirection::FORWARD;
  coastingForReversal = true;
  coastEndMs = now + MUSIC_MOTOR_REVERSE_COAST_MS;
  motorPWMCoast();  // both GPIO8/GPIO9 LOW -- never command full-forward immediately followed by full-reverse
  currentSpeedPercent = 0;
  lastReversalMs = now;
  reason = ReversalRejectReason::NONE;
  return true;
}

// Time-progress ramp (not rate-based) -- recomputed fresh from rampStartMs/
// rampDurationMs every tick, so it's exact regardless of tick jitter.
void applyRampTick(unsigned long now) {
  float progress = (rampDurationMs > 0) ? (float)(now - rampStartMs) / (float)rampDurationMs : 1.0f;
  if (progress > 1.0f) progress = 1.0f;
  if (progress < 0.0f) progress = 0.0f;
  int16_t span = (int16_t)rampToPercent - (int16_t)rampFromPercent;
  uint8_t p = (uint8_t)((int16_t)rampFromPercent + (int16_t)(span * progress));
  if (p != currentSpeedPercent) {
    currentSpeedPercent = p;
    applyPwm(currentDirection, currentSpeedPercent);
  }
}

// Defensive, centralized per-band clamp -- applied immediately before
// intensityTargetPercent is stored (in computeRawIntensityTargetPercent()
// below) and again wherever else a value claiming to represent "the target
// for the currently-reported band" is produced. BAND_QUIET is the one band
// allowed to target zero/anything below a LOW-style floor -- every other
// band must stay within its own documented [min,max] percent range. This
// does not replace fixing the actual interpolation/state-transition logic
// (see computeRawIntensityTargetPercent()'s already-clamped `t` and
// updateDecelerating()'s live re-targeting below) -- it's a second,
// independent safety net so a future bug in either of those can't silently
// reintroduce an out-of-band value.
uint8_t clampTargetForBand(MusicIntensityBand band, uint8_t targetPercent) {
  uint8_t lo, hi;
  switch (band) {
    case MusicIntensityBand::BAND_QUIET:
      return targetPercent;  // QUIET is the only band permitted to target zero / ramp toward it
    case MusicIntensityBand::BAND_LOW:
      lo = tunableLowMinPercent;
      hi = tunableLowMaxPercent;
      break;
    case MusicIntensityBand::BAND_MEDIUM:
      lo = MUSIC_MOTOR_MEDIUM_MIN_PERCENT;
      hi = MUSIC_MOTOR_MEDIUM_MAX_PERCENT;
      break;
    case MusicIntensityBand::BAND_HIGH:
      lo = MUSIC_MOTOR_HIGH_MIN_PERCENT;
      hi = MUSIC_MOTOR_HIGH_MAX_PERCENT;
      break;
    case MusicIntensityBand::BAND_PEAK:
      lo = MUSIC_MOTOR_PEAK_MIN_PERCENT;
      hi = MUSIC_MOTOR_PEAK_MAX_PERCENT;
      break;
    default:
      return targetPercent;
  }
  if (targetPercent < lo) return lo;
  if (targetPercent > hi) return hi;
  return targetPercent;
}

// Energy -> percent mapping for a single band, continuously interpolated
// between that band's [MIN_PERCENT, MAX_PERCENT] across its own energy
// span -- e.g. the bottom of MEDIUM maps near its MIN_PERCENT, the top
// near its MAX_PERCENT -- rather than jumping straight to one fixed value
// per band. The interpolation fraction `t` is clamped to [0,1] BEFORE use,
// so this can never produce a value outside [outLo,outHi] even when energy
// sits below the band's nominal lower threshold -- which matters for
// BAND_LOW specifically: downward hysteresis (see computeIntensityBand())
// can keep BAND_LOW latched down to (tunableLowThreshold -
// MUSIC_MOTOR_INTENSITY_HYSTERESIS), i.e. *below* bandLo, without this
// function ever computing a sub-tunableLowMinPercent result -- `t` simply
// floors at 0, so the output floors at outLo (>=tunableLowMinPercent), not
// at some interpolated-below-zero value. clampTargetForBand() is applied
// on top anyway, as a second, independent safety net.
uint8_t computeRawIntensityTargetPercent(MusicIntensityBand band, float energy) {
  float bandLo, bandHi;
  uint8_t outLo, outHi;
  switch (band) {
    case MusicIntensityBand::BAND_QUIET: return 0;
    case MusicIntensityBand::BAND_LOW:
      bandLo = tunableLowThreshold;
      bandHi = tunableMediumThreshold;
      outLo = tunableLowMinPercent;
      outHi = tunableLowMaxPercent;
      break;
    case MusicIntensityBand::BAND_MEDIUM:
      bandLo = tunableMediumThreshold;
      bandHi = tunableHighThreshold;
      outLo = MUSIC_MOTOR_MEDIUM_MIN_PERCENT;
      outHi = MUSIC_MOTOR_MEDIUM_MAX_PERCENT;
      break;
    case MusicIntensityBand::BAND_HIGH:
      bandLo = tunableHighThreshold;
      bandHi = tunablePeakThreshold;
      outLo = MUSIC_MOTOR_HIGH_MIN_PERCENT;
      outHi = MUSIC_MOTOR_HIGH_MAX_PERCENT;
      break;
    case MusicIntensityBand::BAND_PEAK:
      bandLo = tunablePeakThreshold;
      bandHi = 1.0f;
      outLo = MUSIC_MOTOR_PEAK_MIN_PERCENT;
      outHi = MUSIC_MOTOR_PEAK_MAX_PERCENT;
      break;
    default: return 0;
  }
  float t = (bandHi > bandLo) ? constrain((energy - bandLo) / (bandHi - bandLo), 0.0f, 1.0f) : 1.0f;
  uint8_t raw = (uint8_t)(outLo + (outHi - outLo) * t + 0.5f);
  return clampTargetForBand(band, raw);
}

struct SpinProfile {
  uint8_t percent;
  uint32_t durationMs;
};

// Picks which of the three physically-calibrated spin profiles to run,
// based on context AT THE MOMENT the spin is entered -- see Config.h's
// "Extended spin" comment for the calibration table each profile is
// derived from. 'musicmotor spintime <ms>' overrides this entirely (fixed
// M100 for the given duration) for manual calibration.
SpinProfile pickSpinProfile() {
  if (spinTimeManualOverride) return {MUSIC_MOTOR_SPIN_FAST_PERCENT, spinTimeManualOverrideMs};
  if (dropHoldActive) {
    return {MUSIC_MOTOR_SPIN_EXTENDED_PERCENT, randomRangeU32(MUSIC_MOTOR_SPIN_EXTENDED_MIN_MS, MUSIC_MOTOR_SPIN_EXTENDED_MAX_MS)};
  }
  if (intensityBand == MusicIntensityBand::BAND_PEAK) {
    return {MUSIC_MOTOR_SPIN_FAST_PERCENT, MUSIC_MOTOR_SPIN_FAST_MS};
  }
  return {MUSIC_MOTOR_SPIN_NORMAL_PERCENT, MUSIC_MOTOR_SPIN_NORMAL_MS};
}

// Intensity-band classification WITH hysteresis -- must fall this far
// below a band's own entry threshold to drop OUT of it, not merely below
// the threshold itself, so it doesn't flicker rapidly right at a boundary.
// Free to move upward through multiple bands at once (a sudden loud
// section), but only ever drops one hysteresis-gated step at a time on
// the way down within a single call (matches how songEnergy itself can
// only fall gradually, given its slow release).
MusicIntensityBand computeIntensityBand(float energy, MusicIntensityBand prev) {
  switch (prev) {
    case MusicIntensityBand::BAND_QUIET:
      if (energy >= tunablePeakThreshold) return MusicIntensityBand::BAND_PEAK;
      if (energy >= tunableHighThreshold) return MusicIntensityBand::BAND_HIGH;
      if (energy >= tunableMediumThreshold) return MusicIntensityBand::BAND_MEDIUM;
      if (energy >= tunableLowThreshold) return MusicIntensityBand::BAND_LOW;
      return MusicIntensityBand::BAND_QUIET;
    case MusicIntensityBand::BAND_LOW:
      if (energy >= tunablePeakThreshold) return MusicIntensityBand::BAND_PEAK;
      if (energy >= tunableHighThreshold) return MusicIntensityBand::BAND_HIGH;
      if (energy >= tunableMediumThreshold) return MusicIntensityBand::BAND_MEDIUM;
      if (energy < tunableLowThreshold - MUSIC_MOTOR_INTENSITY_HYSTERESIS) return MusicIntensityBand::BAND_QUIET;
      return MusicIntensityBand::BAND_LOW;
    case MusicIntensityBand::BAND_MEDIUM:
      if (energy >= tunablePeakThreshold) return MusicIntensityBand::BAND_PEAK;
      if (energy >= tunableHighThreshold) return MusicIntensityBand::BAND_HIGH;
      if (energy < tunableMediumThreshold - MUSIC_MOTOR_INTENSITY_HYSTERESIS) {
        return (energy >= tunableLowThreshold) ? MusicIntensityBand::BAND_LOW : MusicIntensityBand::BAND_QUIET;
      }
      return MusicIntensityBand::BAND_MEDIUM;
    case MusicIntensityBand::BAND_HIGH:
      if (energy >= tunablePeakThreshold) return MusicIntensityBand::BAND_PEAK;
      if (energy < tunableHighThreshold - MUSIC_MOTOR_INTENSITY_HYSTERESIS) {
        if (energy >= tunableMediumThreshold) return MusicIntensityBand::BAND_MEDIUM;
        return (energy >= tunableLowThreshold) ? MusicIntensityBand::BAND_LOW : MusicIntensityBand::BAND_QUIET;
      }
      return MusicIntensityBand::BAND_HIGH;
    case MusicIntensityBand::BAND_PEAK:
      if (energy < tunablePeakThreshold - MUSIC_MOTOR_INTENSITY_HYSTERESIS) {
        if (energy >= tunableHighThreshold) return MusicIntensityBand::BAND_HIGH;
        if (energy >= tunableMediumThreshold) return MusicIntensityBand::BAND_MEDIUM;
        return (energy >= tunableLowThreshold) ? MusicIntensityBand::BAND_LOW : MusicIntensityBand::BAND_QUIET;
      }
      return MusicIntensityBand::BAND_PEAK;
  }
  return MusicIntensityBand::BAND_QUIET;
}

// ----------------------------------------------------------------------------
// Revision 4 -- band decision diagnostics. computeRawCandidateBand() is a
// PURE, hysteresis-FREE re-implementation of the plain threshold ladder
// (no dependency on the previous band) -- used only to compare against the
// real, hysteresis-aware computeIntensityBand() result so a diagnostic can
// report "hysteresis actively blocked a move to a higher band" without
// adding any new behavior; it does not replace computeIntensityBand() and
// is never used for the real classification.
// ----------------------------------------------------------------------------
MusicIntensityBand computeRawCandidateBand(float energy, float lowT, float mediumT, float highT, float peakT) {
  if (energy >= peakT) return MusicIntensityBand::BAND_PEAK;
  if (energy >= highT) return MusicIntensityBand::BAND_HIGH;
  if (energy >= mediumT) return MusicIntensityBand::BAND_MEDIUM;
  if (energy >= lowT) return MusicIntensityBand::BAND_LOW;
  return MusicIntensityBand::BAND_QUIET;
}

// Each band's own entry (rising) threshold -- 0.0 for BAND_QUIET, which has
// no entry threshold of its own (it's simply "below LOW").
float bandEntryThreshold(MusicIntensityBand b, float lowT, float mediumT, float highT, float peakT) {
  switch (b) {
    case MusicIntensityBand::BAND_LOW: return lowT;
    case MusicIntensityBand::BAND_MEDIUM: return mediumT;
    case MusicIntensityBand::BAND_HIGH: return highT;
    case MusicIntensityBand::BAND_PEAK: return peakT;
    default: return 0.0f;
  }
}

void logBandEvaluation(unsigned long now, MusicIntensityBand previousBand, MusicIntensityBand resultingBand) {
  if (!debugLoggingEnabled) return;
  MusicIntensityBand candidateBand =
      computeRawCandidateBand(songEnergy, tunableLowThreshold, tunableMediumThreshold, tunableHighThreshold, tunablePeakThreshold);
  bool transitioned = previousBand != resultingBand;
  bool hysteresisBlocked = candidateBand != resultingBand;
  if (!transitioned && !hysteresisBlocked) return;  // nothing noteworthy this tick
  char key[24];
  snprintf(key, sizeof(key), "%d->%d", (int)candidateBand, (int)resultingBand);
  if (!debugShouldPrint(bandRateLimit, key, now, transitioned)) return;
  float entryThreshold = bandEntryThreshold(candidateBand, tunableLowThreshold, tunableMediumThreshold, tunableHighThreshold,
                                             tunablePeakThreshold);
  float exitThreshold = bandEntryThreshold(resultingBand, tunableLowThreshold, tunableMediumThreshold, tunableHighThreshold,
                                            tunablePeakThreshold) -
                         MUSIC_MOTOR_INTENSITY_HYSTERESIS;
  Serial.printf(
      "[MUSIC MOTOR] band evaluation previous=%s candidate=%s resulting=%s song=%.2f fast=%.2f baseline=%.2f "
      "transient=%.2f entryThreshold=%.2f exitThreshold=%.2f hysteresisBlocked=%d\n",
      intensityBandName(previousBand), intensityBandName(candidateBand), intensityBandName(resultingBand), (double)songEnergy,
      (double)fastEnergy, (double)baselineEnergy, (double)transientDelta, (double)entryThreshold, (double)exitThreshold,
      hysteresisBlocked ? 1 : 0);
}

// ----------------------------------------------------------------------------
// Revision 4 -- strong-hit decision diagnostics. PURE reason function (no
// Serial, no mutation) mirroring the real strong-hit gate exactly: raw
// qualification (clap OR transientDelta>=threshold), then the independent
// edge+cooldown gate. Only "below_transient_threshold"/"cooldown_active"/
// "qualified" are used -- "below_energy_requirement" and "beat_false" from
// the original suggested vocabulary do not correspond to any real gating
// condition in this calculation (there is no separate energy requirement
// beyond transientDelta itself, and beat-vs-strongHit classification is a
// different, downstream concern) and are intentionally not used here.
// ----------------------------------------------------------------------------
const char *computeStrongHitReason(bool clap, float transientDeltaValue, float threshold, bool cooldownArmedBefore,
                                    unsigned long sinceLastStrongHitMs, uint32_t cooldownMs) {
  bool raw = clap || (transientDeltaValue >= threshold);
  if (!raw) return "below_transient_threshold";
  if (!(cooldownArmedBefore && sinceLastStrongHitMs >= cooldownMs)) return "cooldown_active";
  return "qualified";
}

void logStrongHitEvaluation(unsigned long now, bool clap, bool cooldownArmedBefore, unsigned long sinceLastStrongHitMs) {
  if (!debugLoggingEnabled) return;
  const char *reason =
      computeStrongHitReason(clap, transientDelta, tunableStrongHitThreshold, cooldownArmedBefore, sinceLastStrongHitMs,
                              MUSIC_MOTOR_STRONG_HIT_COOLDOWN_MS);
  bool significant = strongHitDetectedThisTick;  // a qualified strong hit always prints; rejections are rate-limited
  if (!debugShouldPrint(strongHitRateLimit, reason, now, significant)) return;
  float fastDelta = fastEnergy - previousFastEnergyForWobble;
  Serial.printf(
      "[MUSIC MOTOR] strongHit evaluation beat=%d raw=%.2f fast=%.2f previousFast=%.2f fastDelta=%.2f song=%.2f "
      "baseline=%.2f transient=%.2f strongHitThreshold=%.2f strongHit=%d reason=%s\n",
      beatDetectedThisTick ? 1 : 0, (double)rawEnergy, (double)fastEnergy, (double)previousFastEnergyForWobble,
      (double)fastDelta, (double)songEnergy, (double)baselineEnergy, (double)transientDelta,
      (double)tunableStrongHitThreshold, strongHitDetectedThisTick ? 1 : 0, reason);
}

// ----------------------------------------------------------------------------
// Revision 5 -- performanceEnergy decision diagnostic. Prints whenever
// performanceEnergy's own implied band (via the same hysteresis-free
// computeRawCandidateBand() the band-evaluation diagnostic uses) is actually
// LENDING something above the real measured intensityBand -- i.e. exactly
// the condition effectiveBand() itself would act on -- or whenever that
// implied band just changed. A steady "nothing to report" tick is
// rate-limited like every other diagnostic here; an actual change always
// prints immediately. Mutates previousPerformanceBand itself (this
// function's only caller, updateMusicMotorController(), calls it exactly
// once per tick) -- self-contained, unlike the log*Evaluation() functions
// above that read state mutated by their own caller.
// ----------------------------------------------------------------------------
void logPerformanceEvaluation(unsigned long now) {
  MusicIntensityBand perfBand =
      computeRawCandidateBand(performanceEnergy, tunableLowThreshold, tunableMediumThreshold, tunableHighThreshold, tunablePeakThreshold);
  bool changed = perfBand != previousPerformanceBand;
  previousPerformanceBand = perfBand;
  if (!debugLoggingEnabled) return;
  bool lending = (int)perfBand > (int)intensityBand;
  if (!lending && !changed) return;
  char key[8];
  snprintf(key, sizeof(key), "%d", (int)perfBand);
  if (!debugShouldPrint(performanceRateLimit, key, now, changed)) return;
  Serial.printf(
      "[MUSIC MOTOR] performance evaluation performanceEnergy=%.2f impliedBand=%s measuredBand=%s lending=%d beat=%d "
      "strongHit=%d\n",
      (double)performanceEnergy, intensityBandName(perfBand), intensityBandName(intensityBand), lending ? 1 : 0,
      beatDetectedThisTick ? 1 : 0, strongHitDetectedThisTick ? 1 : 0);
}

const char *sustainedDriveOutcomeName(SustainedDriveOutcome o) { return o == SustainedDriveOutcome::STARTED ? "STARTED" : "REJECTED"; }

// Revision 7 -- eligibility/weighted-roll diagnostic, gated behind
// debugLoggingEnabled like every other "evaluation" log in this file
// (logDropHoldEvaluation()/logStrongHitEvaluation()/logBandEvaluation());
// the actual entry/exit lines below print unconditionally, matching
// "Entering hip shake"/"extended spin started"'s existing precedent for
// significant lifecycle events.
void logSustainedDriveEvaluation(unsigned long now, MusicIntensityBand effBand, const SustainedDriveDecision &d) {
  if (!debugLoggingEnabled) return;
  bool significant = d.outcome == SustainedDriveOutcome::STARTED;
  if (!debugShouldPrint(sustainedDriveRateLimit, d.reason, now, significant)) return;
  unsigned long cooldownRemainingMs =
      ((long)(sustainedDriveCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedDriveCooldownUntilMs - now) : 0UL;
  Serial.printf(
      "[MUSIC MOTOR] sustained drive evaluation effectiveBand=%s measuredBand=%s bandEligible=%d cooldownReady=%d "
      "cooldownRemainingMs=%lu weightRoll=%d result=%s reason=%s\n",
      intensityBandName(effBand), intensityBandName(intensityBand), d.bandEligible ? 1 : 0, d.cooldownReady ? 1 : 0,
      cooldownRemainingMs, d.weightRoll ? 1 : 0, sustainedDriveOutcomeName(d.outcome), d.reason);
}

// Revision 8 -- review/continuation evaluation diagnostic. Fires whenever
// updateSustainedDrive() actually calls computeSustainedDriveContinuationDecision()
// (i.e. review due or a switch opportunity is being considered) -- NOT
// every tick, matching "do not re-roll an exit every loop iteration."
void logSustainedDriveContinuationEvaluation(unsigned long now, const SustainedDriveContinuationResult &r) {
  if (!debugLoggingEnabled) return;
  bool significant = r.decision != SustainedDriveContinuationDecision::CONTINUE_UNTIL_REVIEW;
  if (!debugShouldPrint(sustainedDriveContinuationRateLimit, r.reason, now, significant)) return;
  Serial.printf(
      "[MUSIC MOTOR] sustained drive review phraseElapsedMs=%lu tier=%s effectiveBand=%s measuredBand=%s "
      "performanceEnergy=%.2f dropHold=%d lowEnergyGrace=%d extensionCount=%u switchCount=%u decision=%s reason=%s\n",
      (unsigned long)(now - sustainedDriveEnteredMs), phraseTierName(sustainedDrivePhraseTier), intensityBandName(effectiveBand()),
      intensityBandName(intensityBand), (double)performanceEnergy, dropHoldActive ? 1 : 0, sustainedDriveLowEnergySinceMs != 0 ? 1 : 0,
      (unsigned)sustainedDriveExtensionCount, (unsigned)sustainedDriveSwitchCount, sustainedDriveContinuationDecisionName(r.decision),
      r.reason);
}

// Revision 8 -- direct sustained-direction switch qualification diagnostic.
void logSustainedSwitchEvaluation(unsigned long now, const SustainedSwitchQualification &q) {
  if (!debugLoggingEnabled) return;
  if (!debugShouldPrint(sustainedSwitchRateLimit, q.reason, now, q.qualifies)) return;
  unsigned long cooldownRemainingMs =
      ((long)(sustainedSwitchCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedSwitchCooldownUntilMs - now) : 0UL;
  Serial.printf(
      "[MUSIC MOTOR] sustained switch evaluation measuredBand=%s dropHold=%d directionHeldMs=%lu cooldownRemainingMs=%lu "
      "qualifies=%d exceptionalBypass=%d reason=%s\n",
      intensityBandName(intensityBand), dropHoldActive ? 1 : 0, (unsigned long)(now - sustainedDriveDirectionCommitStartMs),
      cooldownRemainingMs, q.qualifies ? 1 : 0, q.exceptionalBypass ? 1 : 0, q.reason);
}

// Revision 8 -- SHORT-phrase promotion roll diagnostic (unconditional --
// this is a rare, significant per-phrase event, not a per-tick evaluation).
void logSustainedDrivePromotionEvaluation(unsigned long now, bool promoted, uint8_t rollPercent) {
  (void)now;
  Serial.printf("[MUSIC MOTOR] sustained drive short-phrase promotion roll=%u threshold=%u promoted=%d\n", (unsigned)rollPercent,
                (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_PROMOTION_PERCENT, promoted ? 1 : 0);
}

// ----------------------------------------------------------------------------
// Revision 7 -- SUSTAINED_DRIVE weighted entry (mutating: does the actual
// random() rolls, see computeSustainedDriveDecision()/
// chooseSustainedDriveDirection() above for the pure logic they mirror).
// Unlike every other action in selectBeatAction() below, this DELIBERATELY
// uses raw random() -- an explicit exception to that function's own
// "deterministic, counter-based, never random()" rule, per the explicit
// request for "weighted choreography selection rather than a rigid
// repeating timer." Tried FIRST, before the calling band's own modular
// counter logic in selectBeatAction(), so a miss (the overwhelmingly common
// case, by design -- see Config.h's low weight percentages) never disturbs
// those counters' existing cadence. Returns true iff a sustained drive
// should start this tick; sets pendingSustainedDriveDirection for the
// caller to hand off to enterSustainedDrive().
// ----------------------------------------------------------------------------
bool trySustainedDriveEntry(unsigned long now) {
  MusicIntensityBand effBand = effectiveBand();
  bool silent = (state == MusicMotorState::SILENT);
  uint8_t weightDraw = (uint8_t)random(100);
  SustainedDriveDecision decision = computeSustainedDriveDecision(effBand, silent, state == MusicMotorState::SUSTAINED_DRIVE, now,
                                                                    sustainedDriveCooldownUntilMs, weightDraw);
  logSustainedDriveEvaluation(now, effBand, decision);
  if (decision.outcome != SustainedDriveOutcome::STARTED) return false;

  // "Strong musical accent" -- same definition already used elsewhere in
  // this file for the rotation-commitment gate (revision 6): real measured
  // BAND_PEAK, or an active drop hold.
  bool strongAccent = (intensityBand == MusicIntensityBand::BAND_PEAK) || dropHoldActive;
  bool consecutiveCapOverride = (intensityBand == MusicIntensityBand::BAND_PEAK) && dropHoldActive;
  uint8_t flipDraw = (uint8_t)random(100);
  bool safeToFlip = canReverseNow(now);
  SustainedDriveDirectionChoice choice =
      chooseSustainedDriveDirection(currentDirection, strongAccent, flipDraw, safeToFlip, lastSustainedDriveDirection,
                                     consecutiveSustainedDriveSameDirectionCount, consecutiveCapOverride);
  pendingSustainedDriveDirection = choice.direction;
  Serial.printf(
      "[MUSIC MOTOR] sustained drive direction choice current=%s chosen=%s strongAccent=%d accentFlipped=%d "
      "capForcedFlip=%d capForcedButUnsafe=%d\n",
      dirLetter(currentDirection), dirLetter(choice.direction), strongAccent ? 1 : 0, choice.accentFlipped ? 1 : 0,
      choice.capForcedFlip ? 1 : 0, choice.capForcedButUnsafe ? 1 : 0);
  return true;
}

// ----------------------------------------------------------------------------
// Deterministic, counter-based beat/strong-hit action selection -- NEVER
// uses raw random() (with one deliberate exception -- see
// trySustainedDriveEntry() immediately above, called first). Each intensity
// band uses a small modular counter (incremented once per QUALIFYING
// strong hit at that band) to bound how often a stronger action (reversal/
// spin) is chosen relative to the default (accent/hip-shake), so behavior
// stays repeatable enough to debug across an identical listening session
// while still avoiding "always the same action" monotony. See Config.h's
// "beat action selection" constants.
// ----------------------------------------------------------------------------
MusicMotorBeatAction selectBeatAction(unsigned long now, bool isStrong) {
  if (intensityBand == MusicIntensityBand::BAND_QUIET) {
    lastSelectionReason = "measured_quiet_no_action";
    return MusicMotorBeatAction::NONE;
  }

  if (!isStrong) {
    // Ordinary beats NEVER reverse and NEVER start a spin -- they only
    // accent the CURRENT direction, at every intensity band (the accent's
    // magnitude scales with band in applyBeatAccent(), not the action
    // choice itself). Directly satisfies "do not reverse on every beat" /
    // "ordinary beats usually reinforce the current direction."
    lastSelectionReason = "ordinary_beat_accent";
    return MusicMotorBeatAction::ACCENT_CURRENT_DIRECTION;
  }

  bool canReverse = canReverseNow(now);
  bool canSpin = canSpinNow(now);
  bool canHipShake = canStartHipShakeNow(now);
  pendingReverseHipShakeHeavy = false;

  // Revision 6 -- "favor continuation over interruption" (see Config.h's
  // MUSIC_MOTOR_MIN_ROTATION_HOLD_MS comment). strongAccent/phraseBoundary
  // are the two exemptions that still allow an IMMEDIATE reversal (subject
  // only to canReverse's hardware-safety floor); everything else must wait
  // out tunableMinRotationHoldMs in the current direction first. Computed
  // ONCE here and reused by every band's reversal slot below, so all three
  // apply the exact same rule.
  bool strongAccent = (intensityBand == MusicIntensityBand::BAND_PEAK) || dropHoldActive;
  bool canReverseCommitted =
      canReverse && reversalCommitmentSatisfied(now, directionStartMs, strongAccent, bandTransitionedThisTick, tunableMinRotationHoldMs);

  // Selection runs on the CHOREOGRAPHY band -- identical to the real
  // measured intensityBand except a drop hold "lends" HIGH eligibility to
  // a MEDIUM reading (see effectiveBand()). Diagnostics/status
  // always report the real intensityBand separately -- this substitution
  // is local to this function only.
  switch (effectiveBand()) {
    case MusicIntensityBand::BAND_QUIET:
      lastSelectionReason = "effective_quiet_no_action";
      return MusicMotorBeatAction::NONE;

    case MusicIntensityBand::BAND_LOW:
      // "occasional safe reversal" -- only every 3rd qualifying strong hit
      // at LOW even ATTEMPTS a reversal, and even then only once the
      // rotation-commitment gate agrees (see above) -- the rest accent
      // instead. Extended spin/reverse-hip-shake stay out of LOW entirely
      // (below their respective intensity gates), per the original spec.
      lowStrongHitCounter++;
      if (canReverseCommitted && (lowStrongHitCounter % 3 == 0)) {
        lastSelectionReason = "low_reversal_slot";
        return MusicMotorBeatAction::REVERSE_DIRECTION;
      }
      lastSelectionReason = "low_default_accent";
      return MusicMotorBeatAction::ACCENT_CURRENT_DIRECTION;

    case MusicIntensityBand::BAND_MEDIUM:
      // Revision 7 -- tried FIRST, before the modular counter below, so a
      // miss (by far the common case at MEDIUM -- see Config.h's low
      // MEDIUM weight) never disturbs mediumStrongHitCounter's parity.
      if (trySustainedDriveEntry(now)) {
        lastSelectionReason = "sustained_drive_entry";
        return MusicMotorBeatAction::START_SUSTAINED_DRIVE;
      }
      // "reverse or hip shake" -- alternate between the two on successive
      // qualifying strong hits (odd -> reversal attempt, even -> hip
      // shake), so neither dominates. Spin/reverse-hip-shake stay disabled
      // at a genuinely-measured MEDIUM (MUSIC_MOTOR_SPIN_MIN_INTENSITY_LEVEL
      // gate + this function only reaches BAND_HIGH's case below when
      // dropHoldActive promotes it).
      mediumStrongHitCounter++;
      if ((mediumStrongHitCounter % 2) == 1 && canReverseCommitted) {
        lastSelectionReason = "medium_reversal_slot";
        return MusicMotorBeatAction::REVERSE_DIRECTION;
      }
      lastSelectionReason = "medium_default_hipshake";
      return MusicMotorBeatAction::START_HIP_SHAKE;

    case MusicIntensityBand::BAND_HIGH: {
      // Revision 7 -- tried FIRST, same rationale as BAND_MEDIUM above; a
      // larger weight than MEDIUM ("more likely during sustained HIGH").
      if (trySustainedDriveEntry(now)) {
        lastSelectionReason = "sustained_drive_entry";
        return MusicMotorBeatAction::START_SUSTAINED_DRIVE;
      }
      // 4-way modular rotation: spin (its own cooldown), reversal, plain
      // hip-shake burst, and -- new in revision 3 -- an occasional regular
      // REVERSE_HIP_SHAKE ("may occur occasionally on a beat"). Each slot
      // falls back to a plain hip-shake burst if its own gate (spin
      // cooldown/intensity floor, reversal gate, or shared hip-shake-start
      // cooldown) currently refuses, so a strong hit always produces SOME
      // energetic action rather than silently doing nothing.
      highStrongHitCounter++;
      uint8_t slot = highStrongHitCounter % 4;
      if (slot == 0 && canSpin) {
        lastSelectionReason = "high_slot0_spin";
        return MusicMotorBeatAction::START_EXTENDED_SPIN;
      }
      if (slot == 1 && canReverseCommitted) {
        lastSelectionReason = "high_slot1_reverse";
        return MusicMotorBeatAction::REVERSE_DIRECTION;
      }
      if (slot == 3 && canHipShake) {
        pendingReverseHipShakeHeavy = false;
        lastSelectionReason = "high_slot3_reverse_hipshake_regular";
        return MusicMotorBeatAction::START_REVERSE_HIP_SHAKE;
      }
      lastSelectionReason = (slot == 2) ? "high_slot2_hipshake" : "high_slot_fallback_hipshake";
      return MusicMotorBeatAction::START_HIP_SHAKE;
    }

    case MusicIntensityBand::BAND_PEAK: {
      // "do not reverse on every strong hit" -- PEAK never returns
      // REVERSE_DIRECTION. Revision 3: "regular reverse hip shake becomes
      // more likely" and "PEAK + strongHit: heavy reverse hip shake
      // becomes eligible" -- a 3-way rotation among spin / heavy reverse
      // hip shake / regular reverse hip shake, each falling back to a
      // plain hip-shake burst if its own gate currently refuses, so
      // consecutive strong hits still reinforce the same direction with
      // SOME energetic action rather than flipping back and forth (the
      // original "mostly switched directions" physical-test finding this
      // whole no-PEAK-reversal rule addresses).
      //
      // Revision 7 -- tried FIRST, same rationale as BAND_MEDIUM/BAND_HIGH
      // above; the largest weight of the three bands ("more likely during
      // sustained...PEAK sections").
      if (trySustainedDriveEntry(now)) {
        lastSelectionReason = "sustained_drive_entry";
        return MusicMotorBeatAction::START_SUSTAINED_DRIVE;
      }
      peakStrongHitCounter++;
      uint8_t slot = peakStrongHitCounter % 3;
      if (slot == 0 && canSpin) {
        lastSelectionReason = "peak_slot0_spin";
        return MusicMotorBeatAction::START_EXTENDED_SPIN;
      }
      if (slot == 1 && canHipShake) {
        pendingReverseHipShakeHeavy = true;
        lastSelectionReason = "peak_slot1_reverse_hipshake_heavy";
        return MusicMotorBeatAction::START_REVERSE_HIP_SHAKE;
      }
      if (slot == 2 && canHipShake) {
        pendingReverseHipShakeHeavy = false;
        lastSelectionReason = "peak_slot2_reverse_hipshake_regular";
        return MusicMotorBeatAction::START_REVERSE_HIP_SHAKE;
      }
      lastSelectionReason = "peak_slot_fallback_hipshake";
      return MusicMotorBeatAction::START_HIP_SHAKE;
    }
  }
  lastSelectionReason = "unreachable";
  return MusicMotorBeatAction::NONE;
}

// Revision 4 -- choreography-selection diagnostic. Called right after
// EVERY action decision (selectBeatAction()'s beat/strong-hit path, and the
// separate wobble-cue path in updateIntensitySway(), which sets
// lastSelectionReason itself since it doesn't go through selectBeatAction()).
// The plain default (an ordinary beat's ACCENT_CURRENT_DIRECTION) is
// rate-limited like any other diagnostic; every other action -- which is
// exactly the "action selection" event Config.h's rate-limiting comment
// calls significant -- always prints immediately.
void logChoreographySelection(unsigned long now, MusicMotorBeatAction action) {
  if (!debugLoggingEnabled) return;
  bool significant = action != MusicMotorBeatAction::ACCENT_CURRENT_DIRECTION;
  if (!debugShouldPrint(choreographyRateLimit, lastSelectionReason, now, significant)) return;
  Serial.printf(
      "[MUSIC MOTOR] choreography measuredBand=%s effectiveBand=%s dropHold=%d wobbleCue=%d selectedAction=%s "
      "reason=%s spinCooldownReady=%d reverseCooldownReady=%d timeSinceLastSpin=%lu timeSinceLastReverse=%lu\n",
      intensityBandName(intensityBand), intensityBandName(effectiveBand()), dropHoldActive ? 1 : 0,
      wobbleCueActive ? 1 : 0, beatActionName(action), lastSelectionReason, canSpinNow(now) ? 1 : 0,
      canReverseNow(now) ? 1 : 0, (unsigned long)(now - lastSpinMs), (unsigned long)(now - lastReversalMs));
}

// Scales up slightly with intensity band -- a "stronger accent" at HIGH
// than at LOW, per the example behavior table.
void applyBeatAccent(unsigned long now) {
  int bandLevel = (int)intensityBand - (int)MusicIntensityBand::BAND_LOW;  // 0=LOW,1=MEDIUM,2=HIGH,3=PEAK
  if (bandLevel < 0) bandLevel = 0;
  currentAccentPercent = (uint8_t)(MUSIC_MOTOR_BEAT_ACCENT_PERCENT + 2 * bandLevel);
  accentActive = true;
  accentEndMs = now + MUSIC_MOTOR_BEAT_ACCENT_MS;
}

uint32_t currentTargetDurationMs(unsigned long now) {
  switch (state) {
    case MusicMotorState::INTENSITY_SWAY:
      return (intensityBand == MusicIntensityBand::BAND_LOW && swayDeadlineMs > now) ? (uint32_t)(swayDeadlineMs - now) : 0;
    case MusicMotorState::BASS_ACCENT: return rampDurationMs;
    case MusicMotorState::HIP_SHAKE: return (hipShakeDeadlineMs > now) ? (uint32_t)(hipShakeDeadlineMs - now) : 0;
    case MusicMotorState::REVERSE_HIP_SHAKE: {
      unsigned long phaseEnd = reverseHipShakePhaseStartMs + reverseHipShakePhaseDurationMs;
      uint8_t phasesLeft = (reverseHipShakeTotalPhases > reverseHipShakePhaseIndex)
                                ? (uint8_t)(reverseHipShakeTotalPhases - reverseHipShakePhaseIndex - 1)
                                : 0;
      uint32_t currentPhaseRemaining = (phaseEnd > now) ? (uint32_t)(phaseEnd - now) : 0;
      return currentPhaseRemaining + (uint32_t)phasesLeft * reverseHipShakePhaseDurationMs;
    }
    case MusicMotorState::EXTENDED_SPIN: return (spinDeadlineMs > now) ? (uint32_t)(spinDeadlineMs - now) : 0;
    case MusicMotorState::SUSTAINED_DRIVE: return (sustainedDriveNextReviewMs > now) ? (uint32_t)(sustainedDriveNextReviewMs - now) : 0;
    case MusicMotorState::DECELERATING: return rampDurationMs;
    case MusicMotorState::MUSICAL_RAMP_DOWN:
      return (rampStartMs + rampDurationMs > now) ? (uint32_t)(rampStartMs + rampDurationMs - now) : 0;
    default: return 0;
  }
}

uint32_t spinRemainingMs(unsigned long now) {
  if (state != MusicMotorState::EXTENDED_SPIN || spinDeadlineMs <= now) return 0;
  return (uint32_t)(spinDeadlineMs - now);
}

// The direction actually being driven RIGHT NOW -- identical to
// currentDirection except during REVERSE_HIP_SHAKE, where currentDirection
// deliberately stays pinned to the "original" direction throughout (see
// enterReverseHipShake()'s comment) while the phases briefly drive the
// opposite way. Used by diagnostics/status so what's printed matches what
// the motor is actually doing.
MusicMotorDirection activeDrivingDirection() {
  if (state == MusicMotorState::REVERSE_HIP_SHAKE && !reverseHipShakeCoasting) {
    return reverseHipShakePhaseDirection(reverseHipShakePhaseIndex, reverseHipShakeOriginalDirection);
  }
  return currentDirection;
}

const char *dirLetterOrStop() {
  if (state == MusicMotorState::OFF || state == MusicMotorState::SILENT || currentSpeedPercent == 0) return "STOP";
  return dirLetter(activeDrivingDirection());
}

// Field naming deliberately distinguishes three different things that all
// look like "the speed" but are not interchangeable:
//   intensityTarget -- the live, always-band-valid target for the CURRENTLY
//                       reported intensityBand (see the background block in
//                       updateMusicMotorController())
//   actual/commanded -- currentSpeedPercent, the value actually written to
//                       PWM this tick; may lag behind intensityTarget while
//                       ramping/slewing, or briefly exceed it during a beat
//                       accent -- "actual" and "commanded" are the same
//                       number (no position sensor/feedback path exists in
//                       this project; "commanded" is only ever what was
//                       last sent, never independently measured), reported
//                       under both names so a log line is self-explanatory
//                       without cross-referencing this comment.
void maybePrintDiagnostic(unsigned long now) {
  if (state == MusicMotorState::OFF || state == MusicMotorState::SILENT) return;
  if (now - lastDiagPrintMs < MUSIC_MOTOR_DIAG_PRINT_INTERVAL_MS) return;
  lastDiagPrintMs = now;
  unsigned long dropHoldRemainingMs = (dropHoldActive && dropHoldUntilMs > now) ? (unsigned long)(dropHoldUntilMs - now) : 0UL;
  char hipShakePhaseBuf[8];
  if (state == MusicMotorState::REVERSE_HIP_SHAKE) {
    snprintf(hipShakePhaseBuf, sizeof(hipShakePhaseBuf), "%u/%u", (unsigned)(reverseHipShakePhaseIndex + 1),
             (unsigned)reverseHipShakeTotalPhases);
  } else {
    snprintf(hipShakePhaseBuf, sizeof(hipShakePhaseBuf), "0/0");
  }
  unsigned long quietMs = (belowSilenceThresholdSinceMs != 0) ? (unsigned long)(now - belowSilenceThresholdSinceMs) : 0UL;
  bool sustainedDriveActiveNow = (state == MusicMotorState::SUSTAINED_DRIVE);
  unsigned long sustainedDriveNextReviewInMs =
      (sustainedDriveActiveNow && sustainedDriveNextReviewMs > now) ? (unsigned long)(sustainedDriveNextReviewMs - now) : 0UL;
  unsigned long sustainedDriveMinCommitmentRemainingMs =
      (sustainedDriveActiveNow && (now - sustainedDriveDirectionCommitStartMs) < sustainedDriveMinCommitmentMs)
          ? (unsigned long)(sustainedDriveMinCommitmentMs - (now - sustainedDriveDirectionCommitStartMs))
          : 0UL;
  unsigned long sustainedDriveCooldownRemainingMs =
      ((long)(sustainedDriveCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedDriveCooldownUntilMs - now) : 0UL;
  bool rampDownActiveNow = (state == MusicMotorState::MUSICAL_RAMP_DOWN);
  unsigned long rampDownRemainingMs =
      (rampDownActiveNow && (rampStartMs + rampDurationMs) > now) ? (unsigned long)(rampStartMs + rampDurationMs - now) : 0UL;
  Serial.printf(
      "[MUSIC MOTOR] rawNormalized=%.2f fastEnergy=%.2f songEnergy=%.2f baselineEnergy=%.2f transientDelta=%.2f "
      "performanceEnergy=%.2f intensityBand=%s effectiveBand=%s intensityTarget=M%u actual=M%u commanded=M%u beat=%d "
      "strongHit=%d selectedAction=%s movementState=%s direction=%s spinRemainingMs=%lu timeSinceLastSpin=%lu "
      "timeSinceLastReverse=%lu dropHold=%d dropHoldRemainingMs=%lu wobbleCue=%d hipShakePhase=%s quietMs=%lu "
      "sustainedDrive=%d sustainedDriveDirection=%s tier=%s phraseElapsedMs=%lu minCommitmentRemainingMs=%lu "
      "nextReviewInMs=%lu extensionCount=%u lowEnergyGrace=%d switchCount=%u switchCooldownRemainingMs=%lu "
      "sustainedDriveCooldownRemainingMs=%lu rampDown=%d rampDownRemainingMs=%lu\n",
      (double)rawEnergy, (double)fastEnergy, (double)songEnergy, (double)baselineEnergy, (double)transientDelta,
      (double)performanceEnergy, intensityBandName(intensityBand), intensityBandName(effectiveBand()),
      (unsigned)intensityTargetPercent, (unsigned)currentSpeedPercent, (unsigned)currentSpeedPercent,
      beatDetectedThisTick ? 1 : 0, strongHitDetectedThisTick ? 1 : 0, beatActionName(lastSelectedAction),
      reportedStateName(), dirLetterOrStop(), (unsigned long)spinRemainingMs(now), (unsigned long)(now - lastSpinMs),
      (unsigned long)(now - lastReversalMs), dropHoldActive ? 1 : 0, dropHoldRemainingMs, wobbleCueActive ? 1 : 0,
      hipShakePhaseBuf, quietMs, sustainedDriveActiveNow ? 1 : 0, dirLetter(sustainedDriveDirection),
      phraseTierName(sustainedDrivePhraseTier), sustainedDriveActiveNow ? (unsigned long)(now - sustainedDriveEnteredMs) : 0UL,
      sustainedDriveMinCommitmentRemainingMs, sustainedDriveNextReviewInMs, (unsigned)sustainedDriveExtensionCount,
      sustainedDriveLowEnergySinceMs != 0 ? 1 : 0, (unsigned)sustainedDriveSwitchCount,
      ((long)(sustainedSwitchCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedSwitchCooldownUntilMs - now) : 0UL,
      sustainedDriveCooldownRemainingMs, rampDownActiveNow ? 1 : 0, rampDownRemainingMs);

  // Revision 10.1 -- a second periodic line explaining WHY the motor is
  // intentionally stopped whenever commandedPercent==0, so a captured log
  // can always distinguish a legitimate coast/duty-rest/target-zero from
  // an invalid stall without needing the INVALID STATE line to have fired
  // yet (that line only appears once the invariant grace period elapses).
  const char *phraseTransitionName = "NONE";
  if (sustainedDriveActiveNow && dropPhraseStepIndex < dropPhraseStepCount) {
    phraseTransitionName = dropPhraseTransitionPhase == DropPhraseTransitionPhase::DRIVING     ? "DRIVING"
                            : dropPhraseTransitionPhase == DropPhraseTransitionPhase::DECEL     ? "DECELERATING"
                                                                                                  : "COASTING";
  }
  const char *dutyStateName = "NONE";
  if (state == MusicMotorState::INTENSITY_SWAY &&
      (currentMotionTier == MotionTier::QUIET_BUILDUP || currentMotionTier == MotionTier::MELLOW)) {
    dutyStateName = motionDutyPulseOn ? "PULSE_ON" : "REST";
  }
  const char *intentionalStopReason = "none";
  if (currentSpeedPercent == 0) {
    if (lastStopWasEmergency) intentionalStopReason = "emergency_stop";
    else if (strcmp(phraseTransitionName, "DECELERATING") == 0) intentionalStopReason = "decelerating";
    else if (strcmp(phraseTransitionName, "COASTING") == 0) intentionalStopReason = "coasting";
    // General-purpose MusicMotorState::DECELERATING (an ordinary sustained-
    // drive-exit/phrase-exit ramp toward the live intensity target) is
    // distinct from the drop-phrase sequencer's own internal DECEL phase
    // above (which only exists while state==SUSTAINED_DRIVE) -- this covers
    // the former.
    else if (state == MusicMotorState::DECELERATING) intentionalStopReason = "deceleration_handoff";
    else if (strcmp(dutyStateName, "REST") == 0) intentionalStopReason = "duty_rest";
    else if (coastingForReversal) intentionalStopReason = "reversal_coast";
    // Fixes a one-tick false "UNKNOWN_POSSIBLE_INVARIANT_VIOLATION": both
    // the top-level coastingForReversal handler (above, in
    // updateMusicMotorController()) and advanceDropPhraseStepSequencer()'s
    // own COAST branch set directionStartMs=now at the EXACT tick a
    // direction handoff resolves -- clearing coastingForReversal /
    // advancing dropPhraseTransitionPhase to DRIVING BEFORE this diagnostic
    // runs on that same tick, while the actual speed snap-to-target (via
    // updateAppliedSpeedTowardTarget()) doesn't happen until the per-state
    // dispatch resumes NEXT tick. currentSpeedPercent is therefore still
    // legitimately 0 for exactly one tick after the handoff is already
    // "done" by every other measure -- directionStartMs==now is the
    // precise, narrow signal for that single tick, matching neither a
    // continuing coast nor a genuine unexplained stall.
    else if (directionStartMs == now) intentionalStopReason = "direction_change_handoff";
    else if (intensityTargetPercent == 0) intentionalStopReason = "target_zero";
    else intentionalStopReason = "UNKNOWN_POSSIBLE_INVARIANT_VIOLATION";
  }
  unsigned long restartInMs = 0;
  if (strcmp(dutyStateName, "REST") == 0 && motionDutyWindowEndMs > now) restartInMs = (unsigned long)(motionDutyWindowEndMs - now);
  else if (strcmp(phraseTransitionName, "COASTING") == 0 && coastEndMs > now) restartInMs = (unsigned long)(coastEndMs - now);
  Serial.printf(
      "[MUSIC MOTOR] phraseTransition=%s dutyState=%s intentionalStopReason=%s restartInMs=%lu desiredDirection=%s "
      "desiredTarget=M%u physicalDirection=%s commandedPercent=%u\n",
      phraseTransitionName, dutyStateName, intentionalStopReason, restartInMs, dirName(currentDirection),
      (unsigned)intensityTargetPercent, dirLetterOrStop(), (unsigned)currentSpeedPercent);
}

// ============================================================================
// State machine
// ============================================================================

void stopCleanly(unsigned long now) {
  motorPWMCoast();
  currentSpeedPercent = 0;
  intensityTargetPercent = 0;
  accentActive = false;
  performanceEnergy = 0.0f;  // revision 5 -- a genuine full stop also clears musical momentum, not just the raw energies
  unsigned long quietMs = (belowSilenceThresholdSinceMs != 0) ? (now - belowSilenceThresholdSinceMs) : 0UL;
  belowSilenceThresholdSinceMs = 0;
  cancelDropHold("silence_cancel");
  // Revision 10.1 -- confirmed genuine silence is authoritative: a
  // relative-drop section cannot remain logically DROP_ACTIVE (or any
  // other non-NEUTRAL phase) after audio has genuinely collapsed for the
  // full MUSIC_MOTOR_SILENCE_TIMEOUT_MS. Without this, the density-score
  // leaky integrators (multi-second decay by design, for legitimate
  // short-gap tolerance) could keep dropConfidence above the release floor
  // for longer than confirmed silence itself, letting a stale drop-phrase
  // selection/step-sequencer state linger well past when the section
  // genuinely ended.
  if (musicalSectionPhase != MusicalSectionPhase::NEUTRAL) {
    Serial.printf("[MUSIC MOTOR] confirmed silence -- forcing musical section %s -> NEUTRAL\n",
                  musicalSectionPhaseName(musicalSectionPhase));
  }
  musicalSectionPhase = MusicalSectionPhase::NEUTRAL;
  dropConfidenceTier = DropConfidenceTier::NONE;
  dropConfidence = 0.0f;
  dropActiveSinceMs = 0;
  dropBelowSustainFloorSinceMs = 0;
  sustainedPhraseStartedThisDrop = false;
  dropRefractoryUntilMs = now + MUSIC_MOTOR_EDM_REFRACTORY_MS;
  bassFastEnergy = 0.0f;
  bassBaselineEnergy = 0.0f;
  bassImpactDelta = 0.0f;
  buildupEnergyReference = 0.0f;
  beatDensityScore = 0.0f;
  transientDensityScore = 0.0f;
  bassDensityScore = 0.0f;
  dropPhraseStepCount = 0;
  dropPhraseStepIndex = 0;
  currentDropPhraseType = DropPhraseType::FULL_SUSTAIN;
  dropPhraseBoothShakesThisDrop = 0;
  dropPhraseSustainedReversalsThisDrop = 0;
  dropPhraseTotalDirectionChangesThisDrop = 0;
  dropPhraseUsedMoreThanOneTypeThisDrop = false;
  Serial.printf("[MUSIC MOTOR] Silence detected -- motor stopped (quiet for %lums)\n", (unsigned long)quietMs);
  state = MusicMotorState::SILENT;
}

// ----------------------------------------------------------------------------
// Revision 8 -- MUSICAL_RAMP_DOWN: the NORMAL (gradual) response to
// confirmed genuine silence, entered only from beginMusicalSilenceStop()
// below. Reuses the same shared rampFromPercent/rampToPercent/rampStartMs/
// rampDurationMs fields and applyRampTick() every other ramp in this file
// already uses (BASS_ACCENT/EXTENDED_SPIN/DECELERATING) -- "only one ever
// in flight at a time given the sequential state machine" still holds.
// Direction is left completely untouched throughout (never reversed).
// ----------------------------------------------------------------------------
void enterMusicalRampDown(unsigned long now, uint32_t durationMs) {
  state = MusicMotorState::MUSICAL_RAMP_DOWN;
  rampFromPercent = currentSpeedPercent;
  rampToPercent = 0;
  rampStartMs = now;
  rampDurationMs = durationMs;
  accentActive = false;
  cancelDropHold("silence_cancel");  // a wind-down no longer needs choreography permission signals
  Serial.printf("[MUSIC MOTOR] musical ramp-down started fromSpeed=M%u durationMs=%lu\n", (unsigned)rampFromPercent,
                (unsigned long)durationMs);
}

// "If meaningful music returns early enough, allow the controller to
// smoothly recover...preserve direction...do not stop completely and
// restart unnecessarily." Recovery check uses intensityBand directly (the
// same slow, hysteresis-bearing signal computeIntensityBand() already
// debounces) rather than a second timer -- "avoid rapid toggling" falls out
// of that existing debounce for free, no new hysteresis layer needed.
// Recovering hands back to INTENSITY_SWAY WITHOUT snapping (currentSpeedPercent
// is left wherever the ramp-down had already brought it) so
// updateAppliedSpeedTowardTarget() picks it up next tick and ramps smoothly
// toward the freshly (re)computed intensityTargetPercent -- "ramp back
// toward the newly calculated musical target." Once the ramp actually
// completes (reaches stopCleanly() below), recovery instead goes through
// updateSilent()'s normal, unchanged, pre-revision-8 re-entry path -- that
// is "the sensible point after which the stop is considered complete."
void updateMusicalRampDown(unsigned long now) {
  if (intensityBand != MusicIntensityBand::BAND_QUIET) {
    Serial.println(F("[MUSIC MOTOR] musical ramp-down cancelled -- music returned, resuming"));
    belowSilenceThresholdSinceMs = 0;
    state = MusicMotorState::INTENSITY_SWAY;
    return;
  }
  applyRampTick(now);
  if (now - rampStartMs >= rampDurationMs) {
    Serial.println(F("[MUSIC MOTOR] musical ramp-down completed"));
    stopCleanly(now);  // reaches the same guaranteed electrical stop every other stop path uses
  }
}

// ----------------------------------------------------------------------------
// Revision 8 -- the shared silence-confirmed entry point, called ONLY from
// checkAndHandleSilenceTimeout() once the EXISTING hysteresis/timeout has
// already established genuine silence (no new/competing silence timer).
// Chooses a stop STYLE: GRADUAL_RAMP_DOWN (the normal case) or an
// occasional DRAMATIC_ABRUPT_STOP when musical evidence -- a drop hold
// recently active, i.e. "a major drop was followed by immediate silence"
// -- makes a deliberate dramatic cutoff more plausible than an ordinary
// fade. Never affects real safety stops: hardStop()/cancelMusicMotorController()
// call stopCleanly()-equivalent logic directly and never pass through this
// function at all.
// ----------------------------------------------------------------------------
void beginMusicalSilenceStop(unsigned long now) {
  bool sharpCutoffContext = (now - lastDropHoldActiveMs) < MUSIC_MOTOR_SHARP_CUTOFF_DROPHOLD_RECENCY_MS;
  uint8_t abruptRoll = (uint8_t)random(100);
  SilenceStopStyle style = chooseSilenceStopStyle(sharpCutoffContext, abruptRoll);
  lastStopStyle = style;
  lastStopStyleReason = sharpCutoffContext ? "sharp_cutoff_context" : "normal_context";

  // Unconditional -- a significant, once-per-silence-event lifecycle
  // line, same precedent as "Entering hip shake"/"sustained drive started."
  Serial.printf("[MUSIC MOTOR] stop style selected style=%s reason=%s sharpCutoffContext=%d currentSpeed=M%u\n",
                stopStyleName(style), lastStopStyleReason, sharpCutoffContext ? 1 : 0, (unsigned)currentSpeedPercent);

  if (style == SilenceStopStyle::DRAMATIC_ABRUPT_STOP) {
    stopCleanly(now);
    return;
  }

  bool wasHighEnergy = currentSpeedPercent >= MUSIC_MOTOR_HIGH_MIN_PERCENT;
  uint32_t lowRangeMs = randomRangeU32(MUSIC_MOTOR_RAMP_DOWN_LOW_MIN_MS, MUSIC_MOTOR_RAMP_DOWN_LOW_MAX_MS);
  uint32_t highRangeMs = randomRangeU32(MUSIC_MOTOR_RAMP_DOWN_HIGH_MIN_MS, MUSIC_MOTOR_RAMP_DOWN_HIGH_MAX_MS);
  uint32_t rampMs = chooseRampDownDurationMs(wasHighEnergy, lowRangeMs, highRangeMs);
  enterMusicalRampDown(now, rampMs);
}

// Entered only from SILENT (a genuine cold start). Snaps directly to the
// live intensity target rather than ramping through weak intermediate PWM
// values -- matches the physically-validated original behavior (instant
// slow-sway start from a dead stop). The gradual slew (see
// intensityTargetPercent's per-tick tracking in updateMusicMotorController())
// only governs ONGOING target changes once already moving; resuming from a
// reversal coast snaps the same way (see updateAppliedSpeedTowardTarget()).
void enterIntensitySway(unsigned long now) {
  state = MusicMotorState::INTENSITY_SWAY;
  intensityTargetPercent = computeRawIntensityTargetPercent(intensityBand, songEnergy);
  currentSpeedPercent = intensityTargetPercent;
  applyPwm(currentDirection, currentSpeedPercent);
  if (intensityBand == MusicIntensityBand::BAND_LOW) {
    swayDeadlineMs = now + randomRangeU32(MUSIC_MOTOR_NORMAL_SWAY_MIN_MS, MUSIC_MOTOR_NORMAL_SWAY_MAX_MS);
  }
  Serial.printf("[MUSIC MOTOR] Intensity sway: %s (%s) M%u\n", dirName(currentDirection),
                intensityBandName(intensityBand), (unsigned)currentSpeedPercent);
}

void updateSilent(unsigned long now) {
  if (intensityBand == MusicIntensityBand::BAND_QUIET) {
    // Revision 10.1 fix -- a verified quiet musical buildup (real audio
    // above the room-noise floor + Revision 9's own BUILDUP/DROP_ARMED
    // phase -- see computeQuietBuildupQualification()) must be able to
    // wake choreography before the absolute band ever rises out of QUIET.
    // currentMotionTier is kept live even while SILENT specifically so
    // this check is never reading a stale value (see
    // updateMusicMotorController()'s applyMotionDynamics() call). A single
    // transient can't trigger this -- currentMotionTier only becomes
    // QUIET_BUILDUP once the phase machine itself has already required a
    // sustained buildup dwell (MUSIC_MOTOR_EDM_BUILDUP_MIN_MS) to promote
    // past NEUTRAL.
    if (currentMotionTier == MotionTier::QUIET_BUILDUP) {
      Serial.println(F("[MUSIC MOTOR] quiet buildup detected -- waking from silent"));
      enterIntensitySway(now);
    }
    return;
  }
  enterIntensitySway(now);
}

// Rate-limits currentSpeedPercent (the actually-applied/"commanded" PWM
// value) toward the live, always-band-valid intensityTargetPercent, with
// any active beat-accent temporarily added on top. This is where
// MUSIC_MOTOR_SPEED_RISE_PERCENT_PER_SECOND/_FALL_PERCENT_PER_SECOND
// actually apply -- moved here (rather than smoothing
// intensityTargetPercent itself, as an earlier revision did) specifically
// so intensityTargetPercent always reflects a value that's genuinely valid
// for the CURRENTLY reported band, even mid-transition; currentSpeedPercent
// is the one variable allowed to visibly lag behind it while catching up
// (see updateMusicMotorController()'s diagnostic line, which reports both
// separately for exactly this reason). Snaps instantly when resuming from
// a dead stop (currentSpeedPercent==0, e.g. right after a reversal coast)
// instead of ramping through weak intermediate values -- matches the
// physically-validated original safe-reversal behavior (coast briefly,
// then resume at full target speed immediately).
void updateAppliedSpeedTowardTarget(unsigned long now) {
  uint8_t target = intensityTargetPercent;
  if (accentActive) {
    if ((long)(now - accentEndMs) >= 0) {
      accentActive = false;
    } else {
      target = (uint8_t)min((int)intensityTargetPercent + (int)currentAccentPercent, 100);
    }
  }
  if (target == currentSpeedPercent) return;
  if (currentSpeedPercent == 0 && target != 0) {
    currentSpeedPercent = target;  // snap from a dead stop -- see comment above
    applyPwm(currentDirection, currentSpeedPercent);
    return;
  }
  float rate =
      (target >= currentSpeedPercent) ? MUSIC_MOTOR_SPEED_RISE_PERCENT_PER_SECOND : MUSIC_MOTOR_SPEED_FALL_PERCENT_PER_SECOND;
  float maxStep = rate * (MUSIC_MOTOR_TICK_MS / 1000.0f);
  int diff = (int)target - (int)currentSpeedPercent;
  if (diff > 0) {
    currentSpeedPercent = (uint8_t)min((int)target, (int)currentSpeedPercent + (int)ceilf(maxStep));
  } else {
    currentSpeedPercent = (uint8_t)max((int)target, (int)currentSpeedPercent - (int)ceilf(maxStep));
  }
  applyPwm(currentDirection, currentSpeedPercent);
}

// Silence timeout -- must stay in the QUIET band this long before actually
// stopping (hysteresis against a single quiet instant). Revision 5: raised
// to MUSIC_MOTOR_SILENCE_TIMEOUT_MS=7000ms (see its own Config.h comment)
// -- deliberately keyed to the REAL measured intensityBand, not to
// effectiveBand()/performanceEnergy, which is what keeps the motor
// visibly, gradually decaying (via intensityTargetPercent in
// updateMusicMotorController()) throughout this entire countdown instead
// of sitting motionless at 0 for most of it.
//
// Revision 7: factored out of updateIntensitySway() so updateSustainedDrive()
// can share it -- "genuine silence shutdown...must remain able to interrupt
// immediately" applies to a long SUSTAINED_DRIVE phrase just as much as it
// does to ordinary sway (a phrase can run comparable to or longer than the
// timeout itself, unlike the much-shorter HIP_SHAKE/EXTENDED_SPIN bursts,
// where this was never a practical concern). Returns true iff it just
// preempted the current state -- the caller must return immediately in
// that case.
//
// Revision 8: the timeout firing no longer means an unconditional instant
// stop -- see beginMusicalSilenceStop() for the (normally gradual, only
// occasionally abrupt) resulting action. The timeout/hysteresis ITSELF is
// completely unchanged and remains the sole authority on "is this genuinely
// silence" -- this preserves "interrupts immediately" in the sense that
// checkAndHandleSilenceTimeout() still preempts whatever state called it
// the instant the SAME 7s timeout fires as before; only what happens next
// changed.
bool checkAndHandleSilenceTimeout(unsigned long now) {
  if (intensityBand == MusicIntensityBand::BAND_QUIET) {
    if (belowSilenceThresholdSinceMs == 0) {
      belowSilenceThresholdSinceMs = now;
      Serial.println(F("[MUSIC MOTOR] quiet timer started"));
    }
    if (now - belowSilenceThresholdSinceMs >= MUSIC_MOTOR_SILENCE_TIMEOUT_MS) {
      beginMusicalSilenceStop(now);
      return true;
    }
  } else {
    if (belowSilenceThresholdSinceMs != 0) Serial.println(F("[MUSIC MOTOR] quiet timer reset"));
    belowSilenceThresholdSinceMs = 0;
  }
  return false;
}

void updateIntensitySway(unsigned long now) {
  if (checkAndHandleSilenceTimeout(now)) return;

  if (strongHitDetectedThisTick || beatDetectedThisTick) {
    bool isStrong = strongHitDetectedThisTick;
    MusicMotorBeatAction action = selectBeatAction(now, isStrong);
    lastSelectedAction = action;
    Serial.printf("[MUSIC MOTOR] beat action=%s\n", beatActionName(action));
    logChoreographySelection(now, action);
    switch (action) {
      case MusicMotorBeatAction::ACCENT_CURRENT_DIRECTION:
        applyBeatAccent(now);
        break;
      case MusicMotorBeatAction::REVERSE_DIRECTION: {
        ReversalRejectReason reason;
        if (!tryRequestReversal(now, reason)) {
          Serial.printf("[MUSIC MOTOR] Reversal rejected: %s\n", reasonName(reason));
        }
        break;
      }
      case MusicMotorBeatAction::START_HIP_SHAKE:
        enterBassAccent(now);
        return;  // state changed -- let the new state's own update run next tick
      case MusicMotorBeatAction::START_REVERSE_HIP_SHAKE:
        enterReverseHipShake(now, pendingReverseHipShakeHeavy);
        return;
      case MusicMotorBeatAction::START_EXTENDED_SPIN:
        enterExtendedSpin(now);
        return;
      case MusicMotorBeatAction::START_SUSTAINED_DRIVE:
        enterSustainedDrive(now, pendingSustainedDriveDirection);
        return;
      case MusicMotorBeatAction::NONE:
        break;
    }
  } else if (wobbleCueActive) {
    // Wobble cue is an ELIGIBILITY signal only (see Config.h's "wobby
    // tone-shift response" comment) -- deterministic (not random)
    // alternation between an occasional regular reverse hip shake and a
    // plain accent, gated by the same shared hip-shake-start cooldown as
    // every other hip-shake trigger so sample-noise-level wobble doesn't
    // fire actions continuously.
    wobbleActionCounter++;
    MusicMotorBeatAction wobbleAction = MusicMotorBeatAction::ACCENT_CURRENT_DIRECTION;
    lastSelectionReason = "drop_hold_wobble_accent";
    if ((wobbleActionCounter % 2) == 0 && canStartHipShakeNow(now)) {
      pendingReverseHipShakeHeavy = false;
      wobbleAction = MusicMotorBeatAction::START_REVERSE_HIP_SHAKE;
      lastSelectionReason = "drop_hold_wobble_reverse_hipshake";
    }
    lastSelectedAction = wobbleAction;
    Serial.printf("[MUSIC MOTOR] beat action=%s (wobble)\n", beatActionName(wobbleAction));
    logChoreographySelection(now, wobbleAction);
    if (wobbleAction == MusicMotorBeatAction::START_REVERSE_HIP_SHAKE) {
      enterReverseHipShake(now, pendingReverseHipShakeHeavy);
      return;
    }
    applyBeatAccent(now);
  } else if (sustainedHighSinceMs != 0 && (now - sustainedHighSinceMs) >= MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_DWELL_MS &&
             (long)(now - sustainedDrivePersistentEntryNextCheckMs) >= 0) {
    // Revision 8 -- persistent-energy entry opportunity: lets a sustained
    // HIGH/PEAK section start a phrase without a fresh qualifying strong
    // hit. Reuses sustainedHighSinceMs (revision 5's drop-hold dwell
    // timer) rather than a new tracker. Only reached on ticks with NO
    // beat/strongHit/wobble event this tick (the else-if chain above), and
    // hard rate-limited to MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_REVIEW_MS
    // regardless of tick cadence -- "a percentage is meaningless if rolled
    // hundreds of times per second." trySustainedDriveEntry() itself never
    // touches lowStrongHitCounter/mediumStrongHitCounter/etc, so a failed
    // roll here (the common case) never disturbs those counters either.
    sustainedDrivePersistentEntryNextCheckMs = now + MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_REVIEW_MS;
    if (trySustainedDriveEntry(now)) {
      lastSelectionReason = "sustained_drive_persistent_entry";
      enterSustainedDrive(now, pendingSustainedDriveDirection);
      return;
    }
  } else if (relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
             (int)dropConfidenceTier >= (int)DropConfidenceTier::CONFIRMED_DROP && !sustainedPhraseStartedThisDrop) {
    // Revision 9 -- the escalating relative-drop entry opportunity. Only
    // reached on ticks with no beat/strongHit/wobble/persistent-energy
    // event this tick (same else-if chain as every other opportunity
    // above); trySustainedDriveEntryFromDrop() itself rate-limits.
    if (trySustainedDriveEntryFromDrop(now)) {
      lastSelectionReason = "sustained_drive_relative_drop_entry";
      enterSustainedDrive(now, pendingSustainedDriveDirection);
      return;
    }
  }

  // Revision 10 -- movement duty-cycle gate (QUIET_BUILDUP/MELLOW
  // pulse/rest choreography). Only affects THIS tick's continuous-drive
  // target below; a real beat/strongHit/wobble/entry event above already
  // returned before reaching here, so genuine musical events always stay
  // responsive regardless of the current duty window.
  applyMotionDutyGateIfNeeded(now);
  updateAppliedSpeedTowardTarget(now);

  // Periodic, non-beat-driven direction change -- LOW band, or (revision
  // 10) QUIET_BUILDUP motion, which also benefits from "occasional
  // opposite-direction sway" -- see Config.h's
  // MUSIC_MOTOR_NORMAL_SWAY_MIN_MS/_MAX_MS comment. Revision 5: gated on
  // effectiveBand() rather than the raw measured intensityBand -- "reduce
  // excessive short reversals...during active music": if
  // dropHold/performanceEnergy currently implies more than LOW is actually
  // going on, this section should already be committing to longer
  // one-direction movement like MEDIUM/HIGH/PEAK do (beat/hit-driven only),
  // not still twitching on the idle LOW-band cadence just because the raw
  // measured reading briefly dipped. Silent (no reject-reason log) on
  // rejection since this isn't a logged beat/hit event; it just tries again
  // next interval.
  if ((effectiveBand() == MusicIntensityBand::BAND_LOW || currentMotionTier == MotionTier::QUIET_BUILDUP) &&
      (long)(now - swayDeadlineMs) >= 0) {
    ReversalRejectReason reason;
    tryRequestReversal(now, reason);
    swayDeadlineMs = now + randomRangeU32(MUSIC_MOTOR_NORMAL_SWAY_MIN_MS, MUSIC_MOTOR_NORMAL_SWAY_MAX_MS);
  }
}

// Transitional: acceleration toward the hip-shake target. Any reversal
// decision for the triggering strong hit was already made by the caller
// (updateIntensitySway()) before entering this state.
void enterBassAccent(unsigned long now) {
  state = MusicMotorState::BASS_ACCENT;
  accelRampStarted = false;
  markHipShakeStarted(now);  // "start" is the moment the action commits, not once the accel ramp finishes
}

// Only ever runs on ticks where coastingForReversal is already false (the
// top-level coast handler defers calling any per-state update function
// until a pending coast has resolved), so the accel ramp can start
// unconditionally the first time this runs after entry, whether or not a
// reversal happened first.
void updateBassAccent(unsigned long now) {
  if (!accelRampStarted) {
    rampFromPercent = currentSpeedPercent;
    rampToPercent = tunableFastPercent;
    rampStartMs = now;
    rampDurationMs = tunableAccelMs;
    accelRampStarted = true;
  }
  applyRampTick(now);
  if (now - rampStartMs >= rampDurationMs) {
    enterHipShake(now);
  }
}

void enterHipShake(unsigned long now) {
  state = MusicMotorState::HIP_SHAKE;
  hipShakeEnteredMs = now;
  hipShakeDeadlineMs = now + randomRangeU32(tunableHoldMinMs, tunableHoldMaxMs);
  currentSpeedPercent = tunableFastPercent;
  applyPwm(currentDirection, currentSpeedPercent);
  Serial.println(F("[MUSIC MOTOR] Entering hip shake"));
}

void updateHipShake(unsigned long now) {
  if (strongHitDetectedThisTick) {
    MusicMotorBeatAction action = selectBeatAction(now, /*isStrong=*/true);
    lastSelectedAction = action;
    Serial.printf("[MUSIC MOTOR] beat action=%s\n", beatActionName(action));
    logChoreographySelection(now, action);
    if (action == MusicMotorBeatAction::START_EXTENDED_SPIN) {
      enterExtendedSpin(now);
      return;
    }
    if (action == MusicMotorBeatAction::START_SUSTAINED_DRIVE) {
      enterSustainedDrive(now, pendingSustainedDriveDirection);
      return;
    }
    if (action == MusicMotorBeatAction::REVERSE_DIRECTION) {
      ReversalRejectReason reason;
      if (tryRequestReversal(now, reason)) {
        unsigned long maxDeadline = hipShakeEnteredMs + MUSIC_MOTOR_HIP_SHAKE_MAX_TOTAL_MS;
        unsigned long newDeadline = now + randomRangeU32(tunableHoldMinMs, tunableHoldMaxMs);
        hipShakeDeadlineMs = (newDeadline < maxDeadline) ? newDeadline : maxDeadline;
      } else {
        Serial.printf("[MUSIC MOTOR] Reversal rejected: %s\n", reasonName(reason));
      }
      return;
    }
    // ACCENT_CURRENT_DIRECTION, START_HIP_SHAKE, or START_REVERSE_HIP_SHAKE
    // all just mean "stay energetic here" while already in HIP_SHAKE --
    // extend the hold, bounded by the same absolute ceiling either way.
    // Deliberately does NOT transition into REVERSE_HIP_SHAKE: "do not
    // allow a new hip shake to interrupt an active hip shake."
    unsigned long maxDeadline = hipShakeEnteredMs + MUSIC_MOTOR_HIP_SHAKE_MAX_TOTAL_MS;
    unsigned long newDeadline = now + randomRangeU32(tunableHoldMinMs, tunableHoldMaxMs);
    hipShakeDeadlineMs = (newDeadline < maxDeadline) ? newDeadline : maxDeadline;
    return;
  }
  if ((long)(now - hipShakeDeadlineMs) >= 0) {
    enterDecelerating(now);
  }
}

// ----------------------------------------------------------------------------
// REVERSE_HIP_SHAKE -- a distinct multi-phase action, NOT a rename of
// HIP_SHAKE: opposite/original/opposite/original (REGULAR, 4 phases, M90/
// 200ms each) or a 6-phase HEAVY variant (M100/180ms each) for major bass
// drops. Locks in "original direction" once, at entry, and always returns
// to it. Fully non-blocking (millis-based phase deadlines); never calls
// selectBeatAction() from within its own update -- once started, it always
// runs to completion (matching EXTENDED_SPIN's existing "committed, not
// interruptible" precedent) and hands off into DECELERATING, exactly like
// HIP_SHAKE/EXTENDED_SPIN do, so it "returns cleanly to normal intensity
// sway" via the same shared mechanism rather than a duplicated one.
//
// Direction flips between phases go through the SAME brief coast
// (MUSIC_MOTOR_REVERSE_COAST_MS, both GPIO8/GPIO9 LOW) every other
// reversal in this module uses -- never a direct forward<->reverse
// command -- but bypasses the ordinary hold-time/cooldown/post-spin-hold
// gate (checkReversalGate()): those exist to prevent ORGANIC beat-driven
// thrashing, not to gate a single pre-committed, safety-coasted, fixed-
// length choreographed sequence. currentDirection itself is never
// reassigned mid-sequence (it already IS the original direction and stays
// that way); each phase's applyPwm() call is given its own explicit
// direction instead of relying on currentDirection.
// ----------------------------------------------------------------------------
void enterReverseHipShake(unsigned long now, bool heavy) {
  state = MusicMotorState::REVERSE_HIP_SHAKE;
  reverseHipShakeOriginalDirection = currentDirection;
  reverseHipShakeHeavy = heavy;
  reverseHipShakeTotalPhases = heavy ? MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_COUNT : MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_COUNT;
  reverseHipShakePhaseDurationMs = heavy ? MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_MS : MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_MS;
  reverseHipShakeTargetPercent = heavy ? MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PERCENT : MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PERCENT;
  reverseHipShakePhaseIndex = 0;
  reverseHipShakePhaseStartMs = now;
  reverseHipShakeCoasting = true;
  reverseHipShakeCoastEndMs = now + MUSIC_MOTOR_REVERSE_COAST_MS;
  motorPWMCoast();
  currentSpeedPercent = 0;
  markHipShakeStarted(now);
  Serial.printf("[MUSIC MOTOR] reverse hip shake started variant=%s originalDirection=%s\n", heavy ? "HEAVY" : "REGULAR",
                dirLetter(reverseHipShakeOriginalDirection));
}

// Direction for a given phase index: even phases (0,2,4...) are the
// opposite of the original direction, odd phases (1,3,5...) are the
// original direction -- "starting FORWARD: REVERSE->FORWARD->REVERSE->
// FORWARD", "starting REVERSE: FORWARD->REVERSE->FORWARD->REVERSE".
MusicMotorDirection reverseHipShakePhaseDirection(uint8_t phaseIndex, MusicMotorDirection original) {
  return (phaseIndex % 2 == 0) ? oppositeOf(original) : original;
}

void updateReverseHipShake(unsigned long now) {
  if (reverseHipShakeCoasting) {
    if ((long)(now - reverseHipShakeCoastEndMs) < 0) return;
    reverseHipShakeCoasting = false;
    MusicMotorDirection phaseDir = reverseHipShakePhaseDirection(reverseHipShakePhaseIndex, reverseHipShakeOriginalDirection);
    currentSpeedPercent = reverseHipShakeTargetPercent;
    applyPwm(phaseDir, currentSpeedPercent);
  }

  if ((long)(now - reverseHipShakePhaseStartMs) < (long)reverseHipShakePhaseDurationMs) return;

  reverseHipShakePhaseIndex++;
  if (reverseHipShakePhaseIndex >= reverseHipShakeTotalPhases) {
    // Sequence complete -- coast once more, land back on the original
    // direction (already what currentDirection is), then wind down through
    // the normal shared DECELERATING ramp rather than snapping instantly.
    motorPWMCoast();
    currentSpeedPercent = 0;
    Serial.printf("[MUSIC MOTOR] reverse hip shake completed returnDirection=%s\n",
                  dirLetter(reverseHipShakeOriginalDirection));
    enterDecelerating(now);
    return;
  }

  // Next phase: another brief coast, then drive that phase's direction.
  reverseHipShakePhaseStartMs = now;
  reverseHipShakeCoasting = true;
  reverseHipShakeCoastEndMs = now + MUSIC_MOTOR_REVERSE_COAST_MS;
  motorPWMCoast();
  currentSpeedPercent = 0;
}

// Committed one-direction OPEN-LOOP rotation -- see Config.h's "extended
// spin" comment for the no-position-sensor caveat. Always locks the
// CURRENT direction (never forces a reversal first) -- addresses the
// physical-test finding that the controller reversed far too often instead
// of occasionally committing to one direction long enough to read as a
// full rotation.
uint8_t spinProfileTargetPercent = 0;  // the active spin's own ceiling, set at entry -- beat reinforcement nudges up to this

void enterExtendedSpin(unsigned long now) {
  state = MusicMotorState::EXTENDED_SPIN;
  spinRampStarted = false;
  spinEnteredMs = now;
  SpinProfile profile = pickSpinProfile();
  spinProfileTargetPercent = profile.percent;
  uint32_t duration = profile.durationMs;
  if (duration > MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS) duration = MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS;
  spinDeadlineMs = now + duration;
  Serial.printf("[MUSIC MOTOR] extended spin started direction=%s target=M%u duration=%lums\n",
                dirLetter(currentDirection), (unsigned)profile.percent, (unsigned long)duration);
}

void updateExtendedSpin(unsigned long now) {
  if (!spinRampStarted) {
    rampFromPercent = currentSpeedPercent;
    rampToPercent = spinProfileTargetPercent;
    rampStartMs = now;
    rampDurationMs = tunableAccelMs;
    spinRampStarted = true;
  }
  applyRampTick(now);

  // Beats briefly reinforce (nudge the target up a little, capped at 100)
  // but never reverse or end the spin early -- "ignore ordinary reversal
  // requests during the committed-spin window; allow beats to briefly
  // increase speed in the current direction."
  if (beatDetectedThisTick || strongHitDetectedThisTick) {
    Serial.println(F("[MUSIC MOTOR] extended spin reinforced by beat"));
    if (rampToPercent < 100) {
      rampToPercent = (uint8_t)min((int)rampToPercent + 2, 100);
    }
  }

  bool timeUp = (long)(now - spinDeadlineMs) >= 0;
  bool absoluteMaxHit = (now - spinEnteredMs) >= MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS;
  if (timeUp || absoluteMaxHit) {
    Serial.println(F("[MUSIC MOTOR] extended spin completed"));
    lastSpinMs = now;
    lastSpinEndMs = now;
    enterDecelerating(now);
  }
}

// ----------------------------------------------------------------------------
// Revision 7/8 -- SUSTAINED_DRIVE entry. Direction was already chosen (and,
// if different from currentDirection, already verified SAFE via
// canReverseNow()) by trySustainedDriveEntry()/chooseSustainedDriveDirection()
// a moment ago, in pendingSustainedDriveDirection -- this function commits
// to it. The coast+flip below bypasses the ordinary per-event
// checkReversalGate() the same way enterReverseHipShake() already does for
// its own pre-committed, self-justified sequence -- that gate exists to
// prevent ORGANIC beat-driven thrashing, not to re-veto a decision this
// module's own selection logic just made moments ago from the exact same
// inputs.
//
// Revision 8: picks a phrase TIER (chooseSustainedDriveEntryTier()) and,
// from that, either a SHORT tier's own short commitment (which doubles as
// its only review point -- see updateSustainedDrive()) or the long-form
// path's MIN_COMMITMENT_MS floor plus a band-sized initial review point
// (sustainedDriveReviewRange()).
//
// Speed target: NOT computed/stored here as the authoritative value --
// updateMusicMotorController() floors the live intensityTargetPercent at
// BAND_HIGH's own range whenever state==SUSTAINED_DRIVE (see its own
// comment), and updateSustainedDrive() below drives toward THAT via the
// existing updateAppliedSpeedTowardTarget() (same rate-limited ramp
// INTENSITY_SWAY already uses -- "ramp smoothly...rather than jumping
// instantly"; snaps only when resuming from a dead stop after the coast
// below, matching this module's one existing, physically-validated
// safe-reversal precedent). sustainedDriveTargetPercent here is a preview
// for THIS entry's log line only.
// ----------------------------------------------------------------------------
void enterSustainedDrive(unsigned long now, MusicMotorDirection direction) {
  summarySustainedDriveEntryCount++;
  state = MusicMotorState::SUSTAINED_DRIVE;
  sustainedDriveDirection = direction;
  sustainedDriveEnteredMs = now;                // phrase start -- persists across in-phrase switches
  sustainedDriveDirectionCommitStartMs = now;    // this direction segment's own start
  sustainedDriveExtensionCount = 0;
  sustainedDriveSwitchCount = 0;
  sustainedDriveLowEnergySinceMs = 0;
  sustainedDriveLastExtensionMs = 0;
  sustainedDriveLastExtensionReason = "none";
  sustainedDriveExitReason = "none";
  previousSongEnergyForTrend = songEnergy;

  MusicIntensityBand effBand = effectiveBand();
  bool dropHoldNow = dropHoldActive;
  bool majorTransient =
      strongHitDetectedThisTick && (transientDelta >= tunableStrongHitThreshold * MUSIC_MOTOR_SUSTAINED_DRIVE_MAJOR_TRANSIENT_MULTIPLIER);
  uint8_t tierRoll = (uint8_t)random(100);
  sustainedDrivePhraseTier = chooseSustainedDriveEntryTier(effBand, dropHoldNow, majorTransient, tierRoll);

  if (sustainedDrivePhraseTier == SustainedDrivePhraseTier::SHORT) {
    sustainedDriveMinCommitmentMs = randomRangeU32(MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MAX_MS);
    sustainedDriveNextReviewMs = now + sustainedDriveMinCommitmentMs;  // a SHORT phrase's one review coincides with its own commitment ending
  } else {
    sustainedDriveMinCommitmentMs = MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS;
    SustainedDriveTimeRange rr = sustainedDriveReviewRange(effBand, dropHoldNow);
    sustainedDriveNextReviewMs = now + randomRangeU32(rr.minMs, rr.maxMs);
  }

  if (direction == lastSustainedDriveDirection) {
    consecutiveSustainedDriveSameDirectionCount++;
  } else {
    consecutiveSustainedDriveSameDirectionCount = 1;
  }
  lastSustainedDriveDirection = direction;

  // Revision 10 -- drop choreography phrase selection. A genuine drop
  // context (absolute PEAK+DropHold, or a relative CONFIRMED/MAJOR drop)
  // hands the ENTIRE initial direction transition to the phrase
  // sequencer (its own step0 may settle directly into `direction`, or
  // punch away from it first) -- skipping the ordinary coast-to-`direction`
  // block below entirely so a punch-opening phrase never double-flips.
  // An ordinary (non-drop) MEDIUM/HIGH entry is completely unaffected:
  // dropPhraseStepCount stays 0 and the coast-to-`direction` block runs
  // exactly as it always has.
  bool dropContext = (intensityBand == MusicIntensityBand::BAND_PEAK && dropHoldActive) ||
                      (relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                       (int)dropConfidenceTier >= (int)DropConfidenceTier::CONFIRMED_DROP);
  if (dropContext) {
    bool freshImpactCue = strongHitDetectedThisTick || bassImpactDelta >= MUSIC_MOTOR_BASS_IMPACT_DELTA_THRESHOLD || majorTransient;
    selectAndBeginDropPhrase(now, /*isReselection=*/false, freshImpactCue, direction);
  } else {
    dropPhraseStepCount = 0;
    dropPhraseStepIndex = 0;
    currentDropPhraseType = DropPhraseType::FULL_SUSTAIN;
    if (direction != currentDirection) {
      pendingDirection = direction;
      coastingForReversal = true;
      coastEndMs = now + MUSIC_MOTOR_REVERSE_COAST_MS;
      motorPWMCoast();
      currentSpeedPercent = 0;
      lastReversalMs = now;
    }
  }

  accentActive = false;  // any in-flight ordinary accent no longer applies -- SUSTAINED_DRIVE manages its own reinforcement bumps

  MusicIntensityBand previewBand = effBand;
  if ((int)previewBand < (int)MusicIntensityBand::BAND_HIGH) previewBand = MusicIntensityBand::BAND_HIGH;
  sustainedDriveTargetPercent =
      clampTargetForBand(previewBand, computeRawIntensityTargetPercent(previewBand, max(songEnergy, performanceEnergy)));

  Serial.printf(
      "[MUSIC MOTOR] sustained drive started direction=%s tier=%s minCommitmentMs=%lu nextReviewInMs=%lu entryReason=%s "
      "targetIntensity=M%u\n",
      dirLetter(direction), phraseTierName(sustainedDrivePhraseTier), (unsigned long)sustainedDriveMinCommitmentMs,
      (unsigned long)(sustainedDriveNextReviewMs - now), lastSelectionReason, (unsigned)sustainedDriveTargetPercent);
}

// Revision 8 -- gathers computeSustainedDriveContinuationDecision()'s
// inputs from the module's live globals at review/switch-check time.
// energyTrendRising compares songEnergy against its value at the START of
// this review interval (previousSongEnergyForTrend, sampled at entry and
// at the end of every prior review) -- a lightweight, best-effort "is this
// still going up or holding, not fading" signal, not a new subsystem.
SustainedDriveContinuationInputs buildSustainedDriveContinuationInputs(unsigned long now) {
  SustainedDriveContinuationInputs in;
  in.effectiveBandNow = effectiveBand();
  in.dropHoldActiveNow = dropHoldActive;
  in.energyTrendRising = songEnergy >= (previousSongEnergyForTrend - 0.005f);
  in.recentBeatActivity = beatDetectedThisTick;
  in.recentStrongHitActivity = strongHitDetectedThisTick;
  in.genuinelySilent = (state == MusicMotorState::SILENT);
  in.motorSafetyOk = true;  // real safety exits (hardStop()) bypass this function entirely -- see its own comment
  in.nowMs = now;
  in.directionCommitStartMs = sustainedDriveDirectionCommitStartMs;
  in.minCommitmentMs = sustainedDriveMinCommitmentMs;
  in.lowEnergySinceMs = sustainedDriveLowEnergySinceMs;
  in.musicalSectionActive = relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                             (int)dropConfidenceTier >= (int)DropConfidenceTier::CONFIRMED_DROP;
  return in;
}

// Revision 8 -- extends the phrase in the SAME direction: schedules a new
// review point sized by the CURRENT effective band (which may differ from
// the band at entry -- a phrase can move between MEDIUM/HIGH/PEAK ranges as
// the music itself does). This is the mechanism that lets sustained
// HIGH/PEAK energy carry a phrase past 30s, then past 60s -- no separate
// "long phrase" code path, just repeated extensions kept alive by real,
// continuing energy.
void performSustainedDriveExtension(unsigned long now) {
  MusicIntensityBand effBand = effectiveBand();
  SustainedDriveTimeRange er = sustainedDriveExtensionRange(effBand, dropHoldActive);
  uint32_t extMs = randomRangeU32(er.minMs, er.maxMs);
  sustainedDriveNextReviewMs = now + extMs;
  sustainedDriveExtensionCount++;
  summarySustainedDriveExtensionCount++;
  sustainedDriveLastExtensionMs = extMs;
  sustainedDriveLastExtensionReason = "energy_supports_extension";
  previousSongEnergyForTrend = songEnergy;

  unsigned long elapsed = now - sustainedDriveEnteredMs;
  if (elapsed >= MUSIC_MOTOR_SUSTAINED_DRIVE_RENEWABLE_ELAPSED_MS) {
    sustainedDrivePhraseTier = SustainedDrivePhraseTier::RENEWABLE;
  } else if (elapsed >= MUSIC_MOTOR_SUSTAINED_DRIVE_EXTENDED_ELAPSED_MS) {
    sustainedDrivePhraseTier = SustainedDrivePhraseTier::EXTENDED;
  }
  Serial.printf(
      "[MUSIC MOTOR] sustained drive extended direction=%s effectiveBand=%s extensionMs=%lu extensionCount=%u "
      "phraseElapsedMs=%lu tier=%s\n",
      dirLetter(sustainedDriveDirection), intensityBandName(effBand), (unsigned long)extMs, (unsigned)sustainedDriveExtensionCount,
      elapsed, phraseTierName(sustainedDrivePhraseTier));
}

// Revision 8 -- direct sustained FORWARD<->REVERSE switch, staying in
// SUSTAINED_DRIVE throughout ("the phrase remains continuous even though
// its direction changed"). Uses the exact same safe coast+flip primitives
// as tryRequestReversal()/enterSustainedDrive()'s own entry-time flip --
// never an instantaneous polarity change. Deliberately does NOT touch
// sustainedDriveEnteredMs (total phrase elapsed time keeps counting), the
// cross-phrase consecutive-direction cap fields (lastSustainedDriveDirection/
// consecutiveSustainedDriveSameDirectionCount -- those describe SEPARATE
// PHRASES, not in-phrase switches), or sustainedDriveCooldownUntilMs (the
// RE-ENTRY cooldown -- this was never a phrase exit, so it never applies).
void performSustainedDriveDirectionSwitch(unsigned long now, bool wasExceptional) {
  MusicMotorDirection newDirection = oppositeOf(sustainedDriveDirection);
  Serial.printf("[MUSIC MOTOR] sustained %s -> sustained %s requested exceptional=%d\n", dirName(sustainedDriveDirection),
                dirName(newDirection), wasExceptional ? 1 : 0);

  pendingDirection = newDirection;
  coastingForReversal = true;
  coastEndMs = now + MUSIC_MOTOR_REVERSE_COAST_MS;
  motorPWMCoast();  // controlled coast interval -- the shared top-level tick handler flips currentDirection once it elapses
  currentSpeedPercent = 0;
  lastReversalMs = now;
  accentActive = false;

  sustainedDriveDirection = newDirection;
  sustainedDriveDirectionCommitStartMs = now;                            // fresh minimum commitment for the new direction
  sustainedDriveMinCommitmentMs = MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS;  // in-phrase switches always use the standard floor from here
  sustainedDriveSwitchCount++;
  summarySustainedDriveSwitchCount++;
  sustainedSwitchCooldownUntilMs = now + randomRangeU32(MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MIN_MS, MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MAX_MS);

  MusicIntensityBand effBand = effectiveBand();
  SustainedDriveTimeRange rr = sustainedDriveReviewRange(effBand, dropHoldActive);
  sustainedDriveNextReviewMs = now + randomRangeU32(rr.minMs, rr.maxMs);
  previousSongEnergyForTrend = songEnergy;
  if (sustainedDrivePhraseTier == SustainedDrivePhraseTier::SHORT) sustainedDrivePhraseTier = SustainedDrivePhraseTier::STANDARD;

  Serial.printf(
      "[MUSIC MOTOR] sustained-direction switch completed newDirection=%s switchCountInPhrase=%u phraseElapsedMs=%lu "
      "nextReviewInMs=%lu\n",
      dirName(newDirection), (unsigned)sustainedDriveSwitchCount, (unsigned long)(now - sustainedDriveEnteredMs),
      (unsigned long)(sustainedDriveNextReviewMs - now));
}

// Revision 7/8 -- SUSTAINED_DRIVE's per-tick update. checkAndHandleSilenceTimeout()
// still runs FIRST and unconditionally, exactly as revision 7 -- confirmed
// silence "must remain able to interrupt immediately," now resolving via
// beginMusicalSilenceStop() (graceful ramp-down, occasionally an abrupt
// stop) rather than an instant stopCleanly() -- see that function's own
// comment; either way this phrase is preempted right away, never waiting
// for its own review timer.
void updateSustainedDrive(unsigned long now) {
  if (checkAndHandleSilenceTimeout(now)) return;

  // Revision 10 -- while a drop phrase's intro sequence (punches/reversal
  // preamble) is still running, it owns speed/direction entirely; skip
  // everything else this tick. Returns false immediately (a cheap no-op)
  // for FULL_SUSTAIN/SUSTAIN_WITH_ACCENTS (0 steps) and once any phrase
  // reaches its terminal open-ended sustain step.
  bool sequencerActive = advanceDropPhraseStepSequencer(now);

  // Revision 10.1 -- invariant check runs every tick state==SUSTAINED_DRIVE,
  // whether or not the sequencer is currently active, so it also catches a
  // stuck STEADY-STATE (post-sequencer) condition, not just a stuck intro.
  if (checkSustainedDriveInvariant(now)) return;  // recovery just fired -- let it take effect starting next tick

  if (sequencerActive) return;

  // --- low-energy grace tracking -- see Config.h's
  // MUSIC_MOTOR_SUSTAINED_DRIVE_LOW_GRACE_MS comment. Keyed to
  // effectiveBand() (already includes performanceEnergy/dropHold lending),
  // NOT the raw measuredBand -- "short measured-band dips do not end the
  // phrase when effectiveBand/performanceEnergy remain qualified." ---
  // Revision 9 -- a CONFIRMED_DROP/MAJOR_DROP relative section shields the
  // grace timer from ever starting, tolerating short rhythmic gaps within
  // an active drop even when the measured/effective band momentarily dips.
  bool relativeSectionActiveNow = relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                                   (int)dropConfidenceTier >= (int)DropConfidenceTier::CONFIRMED_DROP;
  if ((int)effectiveBand() < (int)MusicIntensityBand::BAND_MEDIUM && !relativeSectionActiveNow) {
    if (sustainedDriveLowEnergySinceMs == 0) {
      sustainedDriveLowEnergySinceMs = now;
      Serial.println(F("[MUSIC MOTOR] sustained drive low-energy grace started"));
    }
  } else if (sustainedDriveLowEnergySinceMs != 0) {
    Serial.println(F("[MUSIC MOTOR] sustained drive low-energy grace reset"));
    sustainedDriveLowEnergySinceMs = 0;
  }

  // --- reinforcement + direct-switch opportunity -- ordinary beats ONLY
  // reinforce; selectBeatAction() is NEVER consulted here, so nothing
  // (reversal, hip shake, spin, another sustained drive) can replace this
  // commitment. A qualifying STRONG HIT additionally gets a chance at a
  // direct sustained-direction switch (never an ordinary beat -- see
  // computeSustainedSwitchQualification()'s own comment). ---
  SustainedSwitchQualification switchQual{};
  bool switchRollPassed = false;
  if (beatDetectedThisTick) {
    applyBeatAccent(now);
    Serial.println(F("[MUSIC MOTOR] sustained drive beat reinforcement"));
  } else if (strongHitDetectedThisTick) {
    applyBeatAccent(now);
    Serial.println(F("[MUSIC MOTOR] sustained drive strong hit reinforcement"));
    bool directionMinElapsed = (now - sustainedDriveDirectionCommitStartMs) >= sustainedDriveMinCommitmentMs;
    if (directionMinElapsed) {
      bool safeToFlip = canReverseNow(now);
      switchQual = computeSustainedSwitchQualification(intensityBand, dropHoldActive, now, sustainedDriveDirectionCommitStartMs,
                                                         sustainedSwitchCooldownUntilMs, safeToFlip);
      logSustainedSwitchEvaluation(now, switchQual);
      if (switchQual.qualifies) {
        uint8_t switchRoll = (uint8_t)random(100);
        switchRollPassed = switchRoll < MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT;
      }
    }
  }

  // Revision 10 -- while a relative CONFIRMED/MAJOR drop remains
  // DROP_ACTIVE, mid-drop reselection (SUSTAINED_REVERSAL/DROP_BOOTY_SHAKE/
  // etc, via the phrase vocabulary) REPLACES the plain Revision 8
  // strong-hit switch below -- the qualifying strong-hit cue computed
  // above is reused as one of maybeTriggerDropPhraseReselection()'s
  // inputs. Absolute-only contexts (PEAK+DropHold with the relative
  // detector off, or not currently a qualified relative drop) keep
  // Revision 8's original plain-flip behavior unchanged.
  bool dropContextNow = relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                         (int)dropConfidenceTier >= (int)DropConfidenceTier::CONFIRMED_DROP;
  if (dropContextNow) {
    uint8_t reselectRoll = (uint8_t)random(100);
    if (maybeTriggerDropPhraseReselection(now, switchQual.qualifies && switchRollPassed, reselectRoll, tunableSwitchChancePercent)) {
      return;  // a new phrase sequence just started -- its own sequencer takes over from next tick
    }
  }

  sustainedDriveTargetPercent = intensityTargetPercent;
  updateAppliedSpeedTowardTarget(now);

  bool reviewDue = (long)(now - sustainedDriveNextReviewMs) >= 0;
  bool switchQualifiedThisTick = !dropContextNow && switchQual.qualifies && switchRollPassed;
  if (!reviewDue && !switchQualifiedThisTick) return;  // CONTINUE_UNTIL_REVIEW, implicitly -- nothing to evaluate yet

  SustainedDriveContinuationInputs in = buildSustainedDriveContinuationInputs(now);
  SustainedDriveContinuationResult result = computeSustainedDriveContinuationDecision(in, reviewDue, switchQualifiedThisTick);
  logSustainedDriveContinuationEvaluation(now, result);

  bool isShortTier = (sustainedDrivePhraseTier == SustainedDrivePhraseTier::SHORT);
  if (result.decision == SustainedDriveContinuationDecision::EXTEND_SAME_DIRECTION ||
      result.decision == SustainedDriveContinuationDecision::SWITCH_SUSTAINED_DIRECTION) {
    if (isShortTier) {
      // "A sufficiently qualified musical event may transition it directly
      // into a longer phrase...avoid turning every short phrase into a
      // long phrase" -- an ADDITIONAL promotion roll gates whether this
      // qualifying continuation is actually honored.
      uint8_t promoRoll = (uint8_t)random(100);
      bool promoted = promoRoll < MUSIC_MOTOR_SUSTAINED_DRIVE_PROMOTION_PERCENT;
      logSustainedDrivePromotionEvaluation(now, promoted, promoRoll);
      if (!promoted) {
        exitSustainedDrive(now, "short_phrase_completed");
        return;
      }
      sustainedDrivePhraseTier = SustainedDrivePhraseTier::STANDARD;
      sustainedDriveMinCommitmentMs = MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS;
    }
    if (result.decision == SustainedDriveContinuationDecision::EXTEND_SAME_DIRECTION) {
      performSustainedDriveExtension(now);
    } else {
      performSustainedDriveDirectionSwitch(now, switchQual.exceptionalBypass);
    }
    return;
  }
  if (result.decision == SustainedDriveContinuationDecision::EXIT_TO_NORMAL) {
    exitSustainedDrive(now, isShortTier ? "short_phrase_completed" : result.reason);
    return;
  }
  // EXIT_FOR_SILENCE/EXIT_FOR_SAFETY are defensive outcomes only -- the
  // real paths (checkAndHandleSilenceTimeout() above, hardStop()) already
  // preempt this function entirely before either could be reached here.
}

// "When a phrase exits normally, do not automatically reverse -- smoothly
// reduce toward the current normal choreography target...allow the next
// choreography decision to continue the same direction, add a hip move,
// coast briefly, or reverse when musically justified" -- exactly what
// enterDecelerating() already does for HIP_SHAKE/EXTENDED_SPIN/
// REVERSE_HIP_SHAKE: ramp toward the live intensityTargetPercent (which,
// once state is no longer SUSTAINED_DRIVE, naturally stops being
// HIGH-floored and re-targets to whatever the music actually calls for),
// then hand back to ordinary INTENSITY_SWAY -- direction is left exactly
// as-is, never forced.
void exitSustainedDrive(unsigned long now, const char *reason) {
  unsigned long heldMs = (unsigned long)(now - sustainedDriveEnteredMs);
  summaryPhraseDurationSumMs += heldMs;
  summaryPhraseCount++;
  if (heldMs > summaryMaxPhraseDurationMs) summaryMaxPhraseDurationMs = heldMs;
  sustainedDriveExitReason = reason;
  // Genuinely-confirmed bug: this was never reset on exit, only at the
  // NEXT enterSustainedDrive() (or a full resetRuntimeState()). Whenever a
  // phrase exited BECAUSE of low_energy_grace_expired, this stayed nonzero
  // indefinitely afterward -- 'musicmotor status' would keep reporting
  // "Low-energy grace: ACTIVE" (with an ever-growing elapsed time, already
  // past its own 3500ms threshold) long after the phrase had genuinely
  // ended, since nothing else in this module ever cleared it outside a
  // fresh sustained-drive entry.
  sustainedDriveLowEnergySinceMs = 0;
  sustainedDriveCooldownUntilMs =
      now + randomRangeU32(MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MAX_MS);
  Serial.printf(
      "[MUSIC MOTOR] sustained drive exit reason=%s direction=%s phraseElapsedMs=%lu extensionCount=%u switchCount=%u "
      "cooldownMs=%lu\n",
      reason, dirLetter(sustainedDriveDirection), heldMs, (unsigned)sustainedDriveExtensionCount, (unsigned)sustainedDriveSwitchCount,
      (unsigned long)(sustainedDriveCooldownUntilMs - now));
  enterDecelerating(now);
}

// Ramps smoothly toward whatever the CURRENT song-intensity target is --
// NOT always back down to the LOW-band slow-sway value. Only actually
// returns to M75-M80 when songEnergy has genuinely fallen into LOW (or
// below). See Config.h's MUSIC_MOTOR_DECEL_MS comment.
void enterDecelerating(unsigned long now) {
  state = MusicMotorState::DECELERATING;
  rampFromPercent = currentSpeedPercent;
  rampToPercent = intensityTargetPercent;
  rampStartMs = now;
  rampDurationMs = tunableDecelMs;
  Serial.println(F("[MUSIC MOTOR] Beginning deceleration"));
}

void updateDecelerating(unsigned long now) {
  // Re-target every tick to the LIVE intensityTargetPercent rather than the
  // value frozen at enterDecelerating() -- if the intensity band changes
  // mid-ramp (e.g. QUIET -> LOW while decelerating out of a spin), a
  // frozen rampToPercent would converge to a now-stale value from the
  // band that was active at entry, and the completion log below would then
  // pair the CURRENT (different) band's name with that stale number --
  // exactly the "returning to LOW target M22" invariant violation this
  // re-targeting fixes. rampFromPercent/rampStartMs stay fixed (decel is
  // still "ease from the speed I was at when I started decelerating"), so
  // this remains a single smooth ramp, just toward a moving, always-valid
  // endpoint instead of a fixed possibly-stale one.
  rampToPercent = intensityTargetPercent;
  applyRampTick(now);
  if (now - rampStartMs >= rampDurationMs) {
    state = MusicMotorState::INTENSITY_SWAY;
    if (intensityBand == MusicIntensityBand::BAND_LOW) {
      swayDeadlineMs = now + randomRangeU32(MUSIC_MOTOR_NORMAL_SWAY_MIN_MS, MUSIC_MOTOR_NORMAL_SWAY_MAX_MS);
    }
    Serial.printf("[MUSIC MOTOR] returning to %s intensityTarget=M%u actual=M%u\n", intensityBandName(intensityBand),
                  (unsigned)intensityTargetPercent, (unsigned)currentSpeedPercent);
  }
}

// Full reset shared by initMusicMotorController() and the enable/hard-stop
// paths.
void resetRuntimeState() {
  currentDirection = MusicMotorDirection::FORWARD;
  pendingDirection = MusicMotorDirection::FORWARD;
  coastingForReversal = false;
  coastEndMs = 0;
  currentSpeedPercent = 0;
  directionStartMs = 0;
  // Seeded in the past so the very first reversal/beat/strong-hit/spin
  // after enabling is never blocked by a stale cooldown from a previous
  // session (matches DanceEngine's identical precedent).
  lastReversalMs = millis() - MUSIC_MOTOR_REVERSAL_COOLDOWN_MS;
  lastBeatMs = millis() - MUSIC_MOTOR_BEAT_COOLDOWN_MS;
  lastStrongHitMs = millis() - MUSIC_MOTOR_STRONG_HIT_COOLDOWN_MS;
  lastSpinMs = millis() - tunableSpinCooldownMs;
  lastSpinEndMs = millis() - MUSIC_MOTOR_POST_SPIN_DIRECTION_HOLD_MS;
  swayDeadlineMs = 0;
  accentActive = false;
  accentEndMs = 0;
  currentAccentPercent = 0;
  rampFromPercent = 0;
  rampToPercent = 0;
  rampStartMs = 0;
  rampDurationMs = 0;
  accelRampStarted = false;
  spinRampStarted = false;
  hipShakeEnteredMs = 0;
  hipShakeDeadlineMs = 0;
  spinEnteredMs = 0;
  spinDeadlineMs = 0;
  sustainedDriveDirection = MusicMotorDirection::FORWARD;
  sustainedDriveEnteredMs = 0;
  sustainedDriveTargetPercent = 0;
  lastSustainedDriveDirection = MusicMotorDirection::FORWARD;
  consecutiveSustainedDriveSameDirectionCount = 0;
  // Seeded once here (not left at 0), same "never blocked by a stale-past
  // cooldown" rationale as lastReversalMs/lastBeatMs/etc. above -- but
  // unlike those (seeded already-elapsed, so the FIRST qualifying event is
  // never blocked), this is seeded already-elapsed-INTO-THE-FUTURE by a
  // full randomized cooldown, so a freshly-enabled session doesn't fire a
  // sustained drive "for free" on its very first qualifying strong hit.
  sustainedDriveCooldownUntilMs =
      millis() + randomRangeU32(MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MIN_MS, MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MAX_MS);
  pendingSustainedDriveDirection = MusicMotorDirection::FORWARD;
  // --- Revision 8: renewable-phrase bookkeeping ---
  sustainedDrivePhraseTier = SustainedDrivePhraseTier::STANDARD;
  sustainedDriveDirectionCommitStartMs = 0;
  sustainedDriveMinCommitmentMs = 0;
  sustainedDriveNextReviewMs = 0;
  sustainedDriveExtensionCount = 0;
  sustainedDriveLastExtensionMs = 0;
  sustainedDriveLastExtensionReason = "none";
  sustainedDriveLowEnergySinceMs = 0;
  sustainedDriveSwitchCount = 0;
  sustainedDriveExitReason = "none";
  previousSongEnergyForTrend = 0.0f;
  sustainedDrivePersistentEntryNextCheckMs = 0;
  // Seeded already-elapsed, same rationale as sustainedDriveCooldownUntilMs
  // just above -- but for SWITCH cooldown specifically (in-phrase, not
  // re-entry), zero is already "ready," which is correct: nothing should
  // block the FIRST switch of a fresh session artificially.
  sustainedSwitchCooldownUntilMs = 0;
  lastStopStyle = SilenceStopStyle::GRADUAL_RAMP_DOWN;
  lastStopStyleReason = "none";
  lastDropHoldActiveMs = 0;
  belowSilenceThresholdSinceMs = 0;
  sustainedHighSinceMs = 0;
  lastTickMs = 0;
  lastDiagPrintMs = 0;
  rawEnergy = 0.0f;
  fastEnergy = 0.0f;
  songEnergy = 0.0f;
  baselineEnergy = 0.0f;
  transientDelta = 0.0f;
  performanceEnergy = 0.0f;
  previousPerformanceBand = MusicIntensityBand::BAND_QUIET;
  intensityBand = MusicIntensityBand::BAND_QUIET;
  intensityTargetPercent = 0;
  beatDetectedThisTick = false;
  strongHitDetectedThisTick = false;
  bandTransitionedThisTick = false;
  lastSelectedAction = MusicMotorBeatAction::NONE;
  pendingReverseHipShakeHeavy = false;
  beatCooldownArmed = true;
  strongHitCooldownArmed = true;
  lowStrongHitCounter = 0;
  mediumStrongHitCounter = 0;
  highStrongHitCounter = 0;
  peakStrongHitCounter = 0;
  dropHoldActive = false;
  dropHoldStartMs = 0;
  dropHoldUntilMs = 0;
  previousFastEnergyForWobble = 0.0f;
  wobbleCueActive = false;
  lastWobbleMs = 0;
  wobbleActionCounter = 0;
  lastHipShakeStartMs = millis() - MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MAX_MS;
  hipShakeStartCooldownMs = 0;
  reverseHipShakeOriginalDirection = MusicMotorDirection::FORWARD;
  reverseHipShakeHeavy = false;
  reverseHipShakePhaseIndex = 0;
  reverseHipShakeTotalPhases = 0;
  reverseHipShakePhaseDurationMs = 0;
  reverseHipShakeTargetPercent = 0;
  reverseHipShakePhaseStartMs = 0;
  reverseHipShakeCoasting = false;
  reverseHipShakeCoastEndMs = 0;
  spinProfileTargetPercent = 0;

  // --- Revision 9: relative/song-adaptive musical-section tracking + session
  // summary counters -- reset on every 'musicmotor on', same lifecycle as
  // everything else in this function. relativeDropDetectionEnabled itself
  // is NOT reset here (it's a persistent A/B toggle, same treatment as
  // debugLoggingEnabled -- see initMusicMotorController()). ---
  bassFastEnergy = 0.0f;
  bassBaselineEnergy = 0.0f;
  bassImpactDelta = 0.0f;
  buildupEnergyReference = 0.0f;
  longSongEnergyReference = 0.0f;
  beatDensityScore = 0.0f;
  transientDensityScore = 0.0f;
  bassDensityScore = 0.0f;
  dropEnergyRiseScore = 0.0f;
  dropSectionContrastScore = 0.0f;
  dropBassImpactScore = 0.0f;
  dropBassDensityScore = 0.0f;
  dropBeatDensityScore = 0.0f;
  dropTransientDensityScore = 0.0f;
  dropBuildupResolutionScore = 0.0f;
  dropSustainedEnergyScore = 0.0f;
  dropConfidence = 0.0f;
  musicalSectionPhase = MusicalSectionPhase::NEUTRAL;
  dropConfidenceTier = DropConfidenceTier::NONE;
  musicalSectionPhaseEnteredMs = 0;
  dropActiveSinceMs = 0;
  dropBelowSustainFloorSinceMs = 0;
  dropRefractoryUntilMs = 0;
  sustainedPhraseStartedThisDrop = false;
  dropEntryNextCheckMs = 0;
  lastSustainedFloorTierName = "NORMAL";
  for (int i = 0; i < 5; i++) summaryBandMs[i] = 0;
  summaryStrongHitCount = 0;
  summaryDropHoldStartCount = 0;
  summaryRelativeDropActiveCount = 0;
  summaryDropsWithNoSustainedDrive = 0;
  summarySustainedDriveEntryCount = 0;
  summarySustainedDriveExtensionCount = 0;
  summarySustainedDriveSwitchCount = 0;
  summaryPhraseDurationSumMs = 0;
  summaryPhraseCount = 0;
  summaryMaxPhraseDurationMs = 0;
  summaryMaxTargetPercent = 0;
  summaryMaxAppliedPercent = 0;
  summaryRelativeEntryRejectAlreadyActive = 0;
  summaryRelativeEntryRejectSilent = 0;
  summaryRelativeEntryRejectCooldown = 0;
  summaryRelativeEntryRejectRollFailed = 0;

  // --- Revision 10: motion tier / duty cycle / drop-phrase / summary reset
  // -- same lifecycle as the rest of this function (reset on every
  // 'musicmotor on'). quietBuildupMotionEnabled and the tunableSwitch*
  // values are NOT reset here (persistent toggles/tuning, same treatment
  // as debugLoggingEnabled/relativeDropDetectionEnabled -- see
  // initMusicMotorController()). ---
  measuredQuietSinceMs = 0;
  lastAuthorityCapPercent = 255;
  lastAuthorityCapSource = "none";
  currentMotionTier = MotionTier::REST;
  motionDutyPulseOn = false;
  motionDutyWindowEndMs = 0;
  lastQuietBuildupMotionMs = 0;
  summaryMellowMotionMs = 0;
  summaryGrooveMotionMs = 0;
  summaryConfirmedDropDriveMs = 0;
  summaryMajorDropDriveMs = 0;
  summaryQuietBuildupMotionCount = 0;
  summaryQuietOverCapMs = 0;
  summaryLowOverCapMs = 0;
  for (uint8_t i = 0; i < MAX_DROP_PHRASE_STEPS; i++) dropPhraseSteps[i] = DropPhraseStep{};
  dropPhraseStepCount = 0;
  dropPhraseStepIndex = 0;
  currentDropPhraseType = DropPhraseType::FULL_SUSTAIN;
  lastDropPhraseSelectionReason = "none";
  dropPhraseTransitionPhase = DropPhraseTransitionPhase::DRIVING;
  dropPhraseStepDeadlineMs = 0;
  dropPhraseSequenceReadyMs = 0;
  lastDropPhraseUsed = DropPhraseType::FULL_SUSTAIN;
  hasLastDropPhrase = false;
  dropPhraseBoothShakesThisDrop = 0;
  dropPhraseSustainedReversalsThisDrop = 0;
  dropPhraseTotalDirectionChangesThisDrop = 0;
  summaryFullSustainCount = 0;
  summarySustainedReversalCount = 0;
  summaryDropBootyShakeCount = 0;
  summaryDropPunchAndHoldCount = 0;
  summaryDoublePunchCount = 0;
  summarySustainWithAccentsCount = 0;
  summaryConfirmedDropDirectionChangesSum = 0;
  summaryConfirmedDropCountForAvg = 0;
  summaryMajorDropDirectionChangesSum = 0;
  summaryMajorDropCountForAvg = 0;
  summaryMaxDirectionChangesInOneDrop = 0;
  summaryDropsUsingMultiplePhrases = 0;
  summaryRepeatedIdenticalPhraseSelections = 0;
  summarySafetyBlockedReversalCount = 0;
  dropPhraseDistinctPhrasesThisDrop = 0;
  dropPhraseUsedMoreThanOneTypeThisDrop = false;
  dropPhraseInvariantViolationSinceMs = 0;
  summarySustainedDriveStoppedInvariantCount = 0;
  summaryDropPhraseRecoveryCount = 0;
  summaryDropPhraseAbortCount = 0;
  summaryUnexpectedStoppedMs = 0;
}

// Coasts, detaches PWM, and forces OFF -- shared by 'musicmotor off' and
// the emergency/mstop cancel path. Does not touch LED state in any way.
// `dropHoldCancelReason` distinguishes the two callers using the same
// stable reason vocabulary as everywhere else (disabled_cancel vs
// emergency_stop_cancel).
void hardStop(const char *dropHoldCancelReason) {
  motorPWMCoast();
  deinitMotorPWM();
  pwmReady = false;
  state = MusicMotorState::OFF;
  cancelDropHold(dropHoldCancelReason);  // logged before resetRuntimeState() clears it silently
  resetRuntimeState();
}

}  // namespace

void initMusicMotorController() {
  state = MusicMotorState::OFF;
  pwmReady = false;
  lastStopWasEmergency = false;
  tunableLowMinPercent = MUSIC_MOTOR_SLOW_PERCENT_MIN;
  tunableLowMaxPercent = MUSIC_MOTOR_SLOW_PERCENT_MAX;
  tunableFastPercent = MUSIC_MOTOR_FAST_PERCENT;
  tunableBeatThreshold = MUSIC_MOTOR_BEAT_DELTA_THRESHOLD;
  tunableStrongHitThreshold = MUSIC_MOTOR_STRONG_HIT_DELTA_THRESHOLD;
  tunableAccelMs = MUSIC_MOTOR_ACCEL_MS;
  tunableHoldMinMs = MUSIC_MOTOR_FAST_HOLD_MIN_MS;
  tunableHoldMaxMs = MUSIC_MOTOR_FAST_HOLD_MAX_MS;
  tunableDecelMs = MUSIC_MOTOR_DECEL_MS;
  tunableLowThreshold = MUSIC_MOTOR_LOW_THRESHOLD;
  tunableMediumThreshold = MUSIC_MOTOR_MEDIUM_THRESHOLD;
  tunableHighThreshold = MUSIC_MOTOR_HIGH_THRESHOLD;
  tunablePeakThreshold = MUSIC_MOTOR_PEAK_THRESHOLD;
  tunableSpinCooldownMs = MUSIC_MOTOR_SPIN_COOLDOWN_MS;
  tunableMinRotationHoldMs = MUSIC_MOTOR_MIN_ROTATION_HOLD_MS;
  spinTimeManualOverride = false;
  spinTimeManualOverrideMs = 0;
  debugLoggingEnabled = false;  // true boot-time reset only -- see its own comment for why musicmotor on/off don't touch this
  resetRuntimeState();
}

void updateMusicMotorController(unsigned long now) {
  if (state == MusicMotorState::OFF) return;

  // Revision 5: the whole audio pipeline below (all three EMAs +
  // performanceEnergy) now runs exactly once per MUSIC_MOTOR_TICK_MS
  // decision tick, not once per loop() call -- see Config.h's
  // MUSIC_MOTOR_FAST_ATTACK comment for the cadence-coupling bug this fixes
  // (the underlying AudioFeatures snapshot itself only refreshes every
  // MIC_PRINT_INTERVAL_MS=120ms regardless, so sampling it once per ~15ms
  // tick loses nothing).
  if (now - lastTickMs < MUSIC_MOTOR_TICK_MS) return;
  lastTickMs = now;

  // --- audio pipeline: AudioFeatures.normalized directly (see Config.h's
  // "Sustained song-intensity tracking" comment for why this revision
  // deliberately does NOT add a local gain/rescale stage here), then the
  // three independently-smoothed EMAs. ---
  const AudioFeatures &f = getAudioFeatures();
  float raw = sanitizeFloat(f.normalized, 0.0f, 0.0f, 1.0f);
  rawEnergy = raw;
  fastEnergy = constrain(fastEnergy + (raw - fastEnergy) * ((raw > fastEnergy) ? MUSIC_MOTOR_FAST_ATTACK : MUSIC_MOTOR_FAST_RELEASE),
                          0.0f, 1.0f);
  songEnergy = constrain(songEnergy + (raw - songEnergy) * ((raw > songEnergy) ? MUSIC_MOTOR_SONG_ATTACK : MUSIC_MOTOR_SONG_RELEASE),
                          0.0f, 1.0f);
  // Very slow, unconditional (both directions) EMA of fastEnergy -- the
  // "recent normal level" a transient stands out against.
  baselineEnergy = constrain(baselineEnergy + (fastEnergy - baselineEnergy) * MUSIC_MOTOR_BASELINE_ADAPT_RATE, 0.0f, 1.0f);
  transientDelta = max(0.0f, fastEnergy - baselineEnergy);

  // --- intensity band (from songEnergy, with hysteresis) ---
  MusicIntensityBand newBand = computeIntensityBand(songEnergy, intensityBand);
  logBandEvaluation(now, intensityBand, newBand);  // before mutation -- see its own comment for when this prints
  bandTransitionedThisTick = (newBand != intensityBand);  // revision 6 -- phrase-boundary exemption, see selectBeatAction()
  if (bandTransitionedThisTick) {
    Serial.printf("[MUSIC MOTOR] intensity %s -> %s\n", intensityBandName(intensityBand), intensityBandName(newBand));
    intensityBand = newBand;
    if (state != MusicMotorState::SILENT && state != MusicMotorState::OFF) {
      uint8_t preview = computeRawIntensityTargetPercent(intensityBand, songEnergy);
      Serial.printf("[MUSIC MOTOR] song intensity target M%u\n", (unsigned)preview);
    }
  }

  // --- Revision 5: sustained (non-transient) drop-hold dwell timer --
  // tracks how long intensityBand has continuously been BAND_HIGH or
  // BAND_PEAK, independent of any beat/strongHit event -- see Config.h's
  // MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS comment. ---
  if (intensityBand == MusicIntensityBand::BAND_HIGH || intensityBand == MusicIntensityBand::BAND_PEAK) {
    if (sustainedHighSinceMs == 0) sustainedHighSinceMs = now;
  } else {
    sustainedHighSinceMs = 0;
  }

  // --- beat/strong-hit detection (edge + independent cooldowns) -- moved
  // ahead of intensityTargetPercent (revision 5): performanceEnergy's
  // event bumps below need this tick's beat/strongHit result, and
  // intensityTargetPercent now needs performanceEnergy. ---
  bool strongHitRaw = f.clap || (transientDelta >= tunableStrongHitThreshold);
  bool beatRaw = !strongHitRaw && (transientDelta >= tunableBeatThreshold);

  // Snapshot BEFORE mutation, for logStrongHitEvaluation() below -- it needs
  // to know what the cooldown gate looked like at the moment this tick's
  // decision was actually made, not after strongHitCooldownArmed/
  // lastStrongHitMs have already been updated by this same block.
  bool strongHitCooldownArmedBefore = strongHitCooldownArmed;
  unsigned long sinceLastStrongHitMsBefore = now - lastStrongHitMs;

  beatDetectedThisTick = false;
  strongHitDetectedThisTick = false;
  if (strongHitRaw) {
    if (strongHitCooldownArmed && (now - lastStrongHitMs) >= MUSIC_MOTOR_STRONG_HIT_COOLDOWN_MS) {
      strongHitCooldownArmed = false;
      lastStrongHitMs = now;
      strongHitDetectedThisTick = true;
    }
  } else {
    strongHitCooldownArmed = true;
  }
  if (beatRaw) {
    if (beatCooldownArmed && (now - lastBeatMs) >= MUSIC_MOTOR_BEAT_COOLDOWN_MS) {
      beatCooldownArmed = false;
      lastBeatMs = now;
      beatDetectedThisTick = true;
    }
  } else {
    beatCooldownArmed = true;
  }

  logStrongHitEvaluation(now, f.clap, strongHitCooldownArmedBefore, sinceLastStrongHitMsBefore);

  // --- performanceEnergy: slowly-decaying musical-momentum signal
  // (revision 5) -- see Config.h's own comment. Continuous driver is
  // songEnergy (so sustained HIGH/PEAK keeps it elevated for as long as the
  // section lasts); a qualifying strong hit/beat this tick additionally
  // bumps it, so a percussive phrase sustains momentum through the gaps
  // between individual hits, not just while songEnergy itself is high. ---
  {
    float rate = (songEnergy > performanceEnergy) ? MUSIC_MOTOR_PERFORMANCE_ATTACK_PER_TICK : MUSIC_MOTOR_PERFORMANCE_RELEASE_PER_TICK;
    performanceEnergy = constrain(performanceEnergy + (songEnergy - performanceEnergy) * rate, 0.0f, 1.0f);
    if (strongHitDetectedThisTick) {
      performanceEnergy = constrain(performanceEnergy + MUSIC_MOTOR_PERFORMANCE_STRONG_HIT_BUMP, 0.0f, 1.0f);
    } else if (beatDetectedThisTick) {
      performanceEnergy = constrain(performanceEnergy + MUSIC_MOTOR_PERFORMANCE_BEAT_BUMP, 0.0f, 1.0f);
    }
  }
  logPerformanceEvaluation(now);

  // --- sustained drop hold: start/refresh via EITHER of two independent
  // paths (revision 5) -- a qualifying strong hit while the MEASURED band
  // is BAND_PEAK (original, immediate), OR intensityBand having stayed
  // continuously BAND_HIGH/BAND_PEAK for at least
  // MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS with no transient required at
  // all (new -- "do not make strong hits mandatory for...drop hold").
  // Expires on its own timeout otherwise. A choreography-permission signal
  // only -- never touches intensityBand itself. ---
  bool dropHoldSustainQualify = sustainedHighSinceMs != 0 && (now - sustainedHighSinceMs) >= MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS;
  logDropHoldEvaluation(now, dropHoldSustainQualify);  // before mutation -- reads the CURRENT (pre-decision) dropHoldActive/*Ms
  bool dropHoldHitQualify = strongHitDetectedThisTick && intensityBand == MusicIntensityBand::BAND_PEAK;
  if ((dropHoldHitQualify || dropHoldSustainQualify) && state != MusicMotorState::SILENT) {
    startOrRefreshDropHold(now);
  }
  updateDropHold(now);
  if (dropHoldActive) lastDropHoldActiveMs = now;  // revision 8 -- "was a drop recently active," for beginMusicalSilenceStop()'s sharp-cutoff tier
  static bool dropHoldActivePrevForSummary = false;
  if (dropHoldActive && !dropHoldActivePrevForSummary) summaryDropHoldStartCount++;
  dropHoldActivePrevForSummary = dropHoldActive;
  if (strongHitDetectedThisTick) summaryStrongHitCount++;
  summaryBandMs[(int)intensityBand] += MUSIC_MOTOR_TICK_MS;

  // --- Revision 9: relative/song-adaptive musical-section (EDM/dubstep
  // "drop") tracking -- see updateRelativeDropTracking()'s own comment.
  // Placed after strongHit/beat/performanceEnergy/dropHold are all current
  // for this tick, before the live intensity-target computation below (the
  // SUSTAINED_DRIVE speed-floor tier selection reads musicalSectionPhase/
  // dropConfidenceTier). ---
  updateRelativeDropTracking(now, sanitizeFloat(f.lowFrequencyEnergy, 0.0f, 0.0f, 1.0f));

  // --- live per-band intensity target (every tick, regardless of state, so
  // DECELERATING/HIP_SHAKE/EXTENDED_SPIN can always read what it currently
  // is) -- driven by effectiveBand() (revision 5: the real measured
  // intensityBand, possibly LENT a higher band by dropHold and/or
  // performanceEnergy -- see effectiveBand()'s own comment), evaluated
  // AFTER the drop-hold mutation above so both signals reflect this exact
  // tick. Paired with max(songEnergy, performanceEnergy) so the
  // interpolation-within-band uses whichever signal actually justifies the
  // lent band, not a stale/lower songEnergy. Never itself rate-limited or
  // "catching up" -- the gradual, physically-smooth motor ramp lives
  // separately, in currentSpeedPercent (see updateAppliedSpeedTowardTarget()
  // below), which is free to lag behind intensityTargetPercent mid-
  // transition -- that lag is exactly what "actual"/"commanded" being
  // different from "intensityTarget" in diagnostics represents. Frozen at 0
  // while SILENT so the next start-from-silence still snaps cleanly instead
  // of having already drifted upward during the silence itself. QUIET
  // remains the only band clampTargetForBand() permits to target/ramp
  // toward zero -- this is exactly how the wind-down's gradual coast-down
  // works: once both intensityBand AND performanceEnergy's implied band
  // have genuinely fallen to QUIET, effectiveBand() itself becomes QUIET
  // and the target eases to 0, well before -- but consistent with --
  // MUSIC_MOTOR_SILENCE_TIMEOUT_MS's hard stop in updateIntensitySway().
  MusicIntensityBand effBand = effectiveBand();
  float targetEnergy = max(songEnergy, performanceEnergy);
  intensityTargetPercent = (state == MusicMotorState::SILENT)
                                ? 0
                                : clampTargetForBand(effBand, computeRawIntensityTargetPercent(effBand, targetEnergy));

  // Revision 10 -- speed-authority cap (bounded lending): classifies this
  // tick's MotionTier and caps how far the lending above may have raised
  // intensityTargetPercent, keyed to the CURRENT MEASURED band. Revision
  // 10.1 fix -- this now runs EVEN WHILE SILENT (previously skipped),
  // because currentMotionTier must stay live so updateSilent() can detect
  // a qualifying QUIET_BUILDUP and wake -- skipping it left
  // currentMotionTier stale/frozen the entire time the controller was
  // SILENT, which was the root cause of quiet buildups never waking
  // choreography. Harmless for the cap itself: intensityTargetPercent is
  // already forced to 0 above while SILENT, and this only ever LOWERS a
  // value, never raises one, so it stays 0.
  applyMotionDynamics(now, effBand);

  // Revision 7 -- SUSTAINED_DRIVE floor: "maintain approximately HIGH-tier
  // motion...do not reverse merely because intensityBand briefly dips."
  // Applied AFTER the general computation above so it only ever RAISES the
  // target (never lowers it below what the music actually calls for), and
  // ONLY while this exact state is active -- the instant state changes
  // (e.g. exitSustainedDrive()'s enterDecelerating()), this floor stops
  // applying on the very next tick and intensityTargetPercent naturally
  // re-targets to whatever the real effectiveBand()/performanceEnergy
  // level calls for, which is what gives DECELERATING its "smoothly reduce
  // toward the current normal choreography target" behavior on exit.
  if (state == MusicMotorState::SUSTAINED_DRIVE) {
    // Revision 9 -- tiered floor selection. Reuses the already-validated
    // MUSIC_MOTOR_SUSTAINED_NORMAL/PERFORMANCE/PEAK_FLOOR_PERCENT constants
    // (themselves aliases of the existing HIGH_MIN/PEAK_MIN/PEAK_MAX
    // breakpoints -- see Config.h) rather than any band-interpolated value,
    // so "which floor is active" is a simple, centrally-configured, fixed
    // number for diagnostics. NORMAL is exactly Revision 7/8's original
    // (unconditional) HIGH-tier floor -- unchanged baseline behavior
    // whenever neither a relative drop nor an absolute PEAK/DropHold
    // context applies.
    bool absolutePeakDropHold = (intensityBand == MusicIntensityBand::BAND_PEAK) && dropHoldActive;
    bool relativeMajorDropActive = relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                                    dropConfidenceTier == DropConfidenceTier::MAJOR_DROP;
    bool relativeConfirmedDropActive = relativeDropDetectionEnabled && musicalSectionPhase == MusicalSectionPhase::DROP_ACTIVE &&
                                        (int)dropConfidenceTier >= (int)DropConfidenceTier::CONFIRMED_DROP;
    uint8_t floorTarget;
    if (absolutePeakDropHold || relativeMajorDropActive) {
      floorTarget = MUSIC_MOTOR_SUSTAINED_PEAK_FLOOR_PERCENT;
      lastSustainedFloorTierName = "PEAK";
    } else if (effBand == MusicIntensityBand::BAND_PEAK || dropHoldActive || relativeConfirmedDropActive) {
      floorTarget = MUSIC_MOTOR_SUSTAINED_PERFORMANCE_FLOOR_PERCENT;
      lastSustainedFloorTierName = "PERFORMANCE";
    } else {
      floorTarget = MUSIC_MOTOR_SUSTAINED_NORMAL_FLOOR_PERCENT;
      lastSustainedFloorTierName = "NORMAL";
    }
    // Revision 10 -- the floor itself is also bounded by the same
    // speed-authority cap: "a drop may remain logically active through a
    // short gap, but motor speed should still react meaningfully to the
    // current fast/song energy." A gap severe enough to drop the MEASURED
    // band to LOW/QUIET now produces a real, visible dip even while
    // SUSTAINED_DRIVE/DROP_ACTIVE stays alive for continuity.
    if (floorTarget > lastAuthorityCapPercent) floorTarget = lastAuthorityCapPercent;
    if (intensityTargetPercent < floorTarget) intensityTargetPercent = floorTarget;
  }

  // Revision 10 -- self-verifying regression check: after every path above
  // (including the SUSTAINED_DRIVE floor) has had a chance to run, the
  // final intensityTargetPercent should never exceed lastAuthorityCapPercent.
  // These two counters are expected to stay at (or very near) zero -- a
  // nonzero value means some path is bypassing the cap.
  if (intensityTargetPercent > lastAuthorityCapPercent) {
    if (intensityBand == MusicIntensityBand::BAND_QUIET) summaryQuietOverCapMs += MUSIC_MOTOR_TICK_MS;
    else if (intensityBand == MusicIntensityBand::BAND_LOW) summaryLowOverCapMs += MUSIC_MOTOR_TICK_MS;
  }

  if (intensityTargetPercent > summaryMaxTargetPercent) summaryMaxTargetPercent = intensityTargetPercent;
  if (currentSpeedPercent > summaryMaxAppliedPercent) summaryMaxAppliedPercent = currentSpeedPercent;

  // --- wobble cue: lightweight fastEnergy-delta detector, only evaluated
  // while a drop hold grants HIGH/PEAK-adjacent choreography permission and
  // the measured band is MEDIUM/HIGH/PEAK -- see Config.h's "wobby
  // tone-shift response" comment. Edge + debounce, same shape as
  // beat/strongHit above. ---
  {
    float fastEnergyDelta = fabsf(fastEnergy - previousFastEnergyForWobble);
    previousFastEnergyForWobble = fastEnergy;
    bool bandEligible = intensityBand == MusicIntensityBand::BAND_MEDIUM || intensityBand == MusicIntensityBand::BAND_HIGH ||
                         intensityBand == MusicIntensityBand::BAND_PEAK;
    bool wobbleRaw = dropHoldActive && bandEligible && fastEnergyDelta >= MUSIC_MOTOR_WOBBLE_DELTA_THRESHOLD;
    wobbleCueActive = false;
    if (wobbleRaw && (now - lastWobbleMs) >= MUSIC_MOTOR_WOBBLE_REFRACTORY_MS) {
      lastWobbleMs = now;
      wobbleCueActive = true;
    }
  }

  if (coastingForReversal) {
    if ((long)(now - coastEndMs) >= 0) {
      currentDirection = pendingDirection;
      directionStartMs = now;
      coastingForReversal = false;
      Serial.printf("[MUSIC MOTOR] Direction reversed: now %s\n", dirName(currentDirection));
    }
    maybePrintDiagnostic(now);
    return;  // per-state processing resumes next tick, once any coast is fully resolved
  }

  switch (state) {
    case MusicMotorState::OFF: break;  // unreachable (handled above)
    case MusicMotorState::SILENT: updateSilent(now); break;
    case MusicMotorState::INTENSITY_SWAY: updateIntensitySway(now); break;
    case MusicMotorState::BASS_ACCENT: updateBassAccent(now); break;
    case MusicMotorState::HIP_SHAKE: updateHipShake(now); break;
    case MusicMotorState::REVERSE_HIP_SHAKE: updateReverseHipShake(now); break;
    case MusicMotorState::EXTENDED_SPIN: updateExtendedSpin(now); break;
    case MusicMotorState::SUSTAINED_DRIVE: updateSustainedDrive(now); break;
    case MusicMotorState::DECELERATING: updateDecelerating(now); break;
    case MusicMotorState::MUSICAL_RAMP_DOWN: updateMusicalRampDown(now); break;
  }

  maybePrintDiagnostic(now);
}

bool isMusicMotorControllerActive() { return state != MusicMotorState::OFF; }

void musicMotorEnable() {
  if (state != MusicMotorState::OFF) {
    Serial.println(F("[MUSIC MOTOR] Already enabled"));
    return;
  }
  // DanceEngine is stopped PROACTIVELY, before the refusal check below --
  // isAnyMotorDiagnosticActive() already reports true while DanceEngine is
  // active, so checking it first would always refuse this enable instead
  // of cleanly taking over. Starting music-reactive mode must stop an
  // incompatible choreographed dance, not be blocked by it.
  if (isDanceEngineActive()) {
    Serial.println(F("[MUSIC MOTOR] Stopping DanceEngine -- MusicMotorController is taking motor ownership"));
    cancelDanceEngine();
  }
  if (isAnyMotorDiagnosticActive() || isExpressiveMotionMoving()) {
    Serial.println(F("[MUSIC MOTOR] Refused -- another motor diagnostic or expressive motion currently owns the motor"));
    return;
  }
  setMotorBehavior(MotorBehaviorMode::OFF);  // preempt IDLE_SWAY, matching every other motor-owning module
  if (getExpressiveMotionMode() == ExpressiveMotionMode::AUDIO_REACTIVE) {
    Serial.println(F("[MUSIC MOTOR] Disabling AUDIO_REACTIVE expressive-motion mode"));
    setExpressiveMotionMode(ExpressiveMotionMode::OFF);
  }
  ensurePWMReady();
  resetRuntimeState();
  lastStopWasEmergency = false;
  state = MusicMotorState::SILENT;
  Serial.println(F("[MUSIC MOTOR] Enabled"));
}

void musicMotorDisable() {
  if (state == MusicMotorState::OFF) {
    Serial.println(F("[MUSIC MOTOR] Already disabled"));
    return;
  }
  hardStop("disabled_cancel");
  Serial.println(F("[MUSIC MOTOR] Disabled"));
}

void cancelMusicMotorController() {
  if (state == MusicMotorState::OFF) return;  // idempotent no-op -- caller (k/mstop) prints its own message
  hardStop("emergency_stop_cancel");
  lastStopWasEmergency = true;
}

void musicMotorSetSlowPercent(uint8_t percent) {
  percent = (uint8_t)constrain((int)percent, 0, 100);
  tunableLowMinPercent = percent;
  tunableLowMaxPercent = (uint8_t)min((int)percent + 3, 100);
  Serial.printf("[MUSIC MOTOR] LOW-band target set to %u-%u%%\n", (unsigned)tunableLowMinPercent,
                (unsigned)tunableLowMaxPercent);
}

void musicMotorSetFastPercent(uint8_t percent) {
  tunableFastPercent = (uint8_t)constrain((int)percent, 0, 100);
  Serial.printf("[MUSIC MOTOR] Hip-shake target set to %u%%\n", (unsigned)tunableFastPercent);
}

void musicMotorSetStrongHitThreshold(float delta) {
  tunableStrongHitThreshold = delta;
  Serial.printf("[MUSIC MOTOR] Strong-hit delta threshold set to %.3f\n", (double)delta);
}

void musicMotorSetBeatThreshold(float delta) {
  tunableBeatThreshold = delta;
  Serial.printf("[MUSIC MOTOR] Beat delta threshold set to %.3f\n", (double)delta);
}

void musicMotorSetAccelMs(uint32_t ms) {
  tunableAccelMs = ms;
  Serial.printf("[MUSIC MOTOR] Accel duration set to %lums\n", (unsigned long)ms);
}

void musicMotorSetHoldMs(uint32_t ms) {
  tunableHoldMinMs = ms;
  tunableHoldMaxMs = ms;
  Serial.printf("[MUSIC MOTOR] Hip-shake hold duration set to %lums (fixed)\n", (unsigned long)ms);
}

void musicMotorSetDecelMs(uint32_t ms) {
  tunableDecelMs = ms;
  Serial.printf("[MUSIC MOTOR] Decel duration set to %lums\n", (unsigned long)ms);
}

void musicMotorSetLowThreshold(float value) {
  tunableLowThreshold = value;
  Serial.printf("[MUSIC MOTOR] LOW intensity threshold set to %.3f\n", (double)value);
}

void musicMotorSetMediumThreshold(float value) {
  tunableMediumThreshold = value;
  Serial.printf("[MUSIC MOTOR] MEDIUM intensity threshold set to %.3f\n", (double)value);
}

void musicMotorSetHighThreshold(float value) {
  tunableHighThreshold = value;
  Serial.printf("[MUSIC MOTOR] HIGH intensity threshold set to %.3f\n", (double)value);
}

// Validated (unlike the other three threshold setters, out of scope for
// this narrowly-focused fix -- see the "musicmotor bandpeak" investigation)
// because PEAK is the band most directly implicated in the
// "musicmotor peakthreshold"/"musicmotor bandpeak" parser-collision report:
// required ordering is lowThreshold < mediumThreshold < highThreshold <
// peakThreshold <= 1.0, matching computeRawIntensityTargetPercent()'s
// per-band [bandLo,bandHi) assumption. Shared verbatim by both the
// canonical 'musicmotor peakthreshold' command and the 'musicmotor
// bandpeak' alias (see dispatchMusicMotorCommand() in Controls.cpp) --
// there is only ever one underlying variable, tunablePeakThreshold.
void musicMotorSetPeakThreshold(float value) {
  if (isnan(value) || value < 0.0f || value > 1.0f) {
    Serial.printf("[MUSIC MOTOR] rejected peak threshold %.3f: must be within 0.0-1.0\n", (double)value);
    return;
  }
  if (value <= tunableHighThreshold) {
    Serial.printf("[MUSIC MOTOR] rejected peak threshold %.3f:\n              must be greater than high threshold %.3f\n",
                  (double)value, (double)tunableHighThreshold);
    return;
  }
  tunablePeakThreshold = value;
  Serial.printf("[MUSIC MOTOR] peak threshold set to %.3f\n", (double)value);
}

// Manual override for calibration: forces every subsequent spin (automatic
// or manual-triggered) to a fixed M100/<ms> profile instead of the
// context-based NORMAL/FAST/EXTENDED_DROP selection -- see pickSpinProfile().
void musicMotorSetSpinTimeMs(uint32_t ms) {
  spinTimeManualOverride = true;
  spinTimeManualOverrideMs = ms;
  Serial.printf("[MUSIC MOTOR] Spin duration overridden to M%u for %lums (fixed)\n", (unsigned)MUSIC_MOTOR_SPIN_FAST_PERCENT,
                (unsigned long)ms);
}

void musicMotorSetSpinCooldownMs(uint32_t ms) {
  tunableSpinCooldownMs = ms;
  Serial.printf("[MUSIC MOTOR] Spin cooldown set to %lums\n", (unsigned long)ms);
}

// Revision 6 -- see Config.h's MUSIC_MOTOR_MIN_ROTATION_HOLD_MS comment.
void musicMotorSetMinRotationHoldMs(uint32_t ms) {
  tunableMinRotationHoldMs = ms;
  Serial.printf("[MUSIC MOTOR] Minimum rotation-commitment hold set to %lums\n", (unsigned long)ms);
}

void musicMotorTriggerSpin() {
  unsigned long now = millis();
  if (state == MusicMotorState::OFF) {
    Serial.println(F("[MUSIC MOTOR] spin rejected reason=state (not enabled)"));
    return;
  }
  if (state == MusicMotorState::EXTENDED_SPIN) {
    Serial.println(F("[MUSIC MOTOR] spin rejected reason=state (already spinning)"));
    return;
  }
  if (now - lastSpinMs < tunableSpinCooldownMs) {
    Serial.println(F("[MUSIC MOTOR] spin rejected reason=cooldown"));
    return;
  }
  // Stop/exit any incompatible in-flight movement cleanly -- a manual spin
  // takes priority over whatever transitional phase (ramp/hold/pending
  // coast) was in progress. pickSpinProfile() (called from enterExtendedSpin())
  // falls back to the NORMAL M90/2000ms profile when there's no audio
  // context (e.g. triggered from SILENT) to pick a more specific one.
  coastingForReversal = false;
  accentActive = false;
  enterExtendedSpin(now);
}

void musicMotorPrintStatus() {
  unsigned long now = millis();
  Serial.println(F("[MUSIC MOTOR STATUS]"));
  Serial.printf("  Firmware: %s (built %s)   Feature revision: %s\n", FIRMWARE_REVISION_TAG, FIRMWARE_BUILD_IDENTIFIER,
                FIRMWARE_MUSIC_MOTOR_FEATURE_REVISION);
  Serial.printf("  Enabled: %s\n", state != MusicMotorState::OFF ? "yes" : "no");
  Serial.printf("  Movement state: %s\n", reportedStateName());
  Serial.printf("  Direction (active/driving): %s   Original/resting direction: %s\n", dirName(activeDrivingDirection()),
                dirName(currentDirection));
  Serial.printf("  Pending direction: %s\n", coastingForReversal ? dirName(pendingDirection) : "-");
  Serial.printf("  Current speed (actual/commanded): %u%% (pwm %u/255)\n", (unsigned)currentSpeedPercent,
                percentToMotorPwm(currentSpeedPercent));
  Serial.printf("  Intensity target (live, band-clamped): %u%%\n", (unsigned)intensityTargetPercent);
  Serial.printf("  Raw normalized: %.3f\n", (double)rawEnergy);
  Serial.printf("  Fast energy: %.3f   Song energy: %.3f   Baseline energy: %.3f\n", (double)fastEnergy,
                (double)songEnergy, (double)baselineEnergy);
  Serial.printf("  Transient delta: %.3f\n", (double)transientDelta);
  Serial.printf("  Performance energy (musical momentum): %.3f\n", (double)performanceEnergy);
  Serial.printf("  Intensity band (measured): %s   Effective band (lent by dropHold/performance): %s\n",
                intensityBandName(intensityBand), intensityBandName(effectiveBand()));
  Serial.printf("  Beat detected (this tick): %s\n", beatDetectedThisTick ? "yes" : "no");
  Serial.printf("  Strong hit detected (this tick): %s\n", strongHitDetectedThisTick ? "yes" : "no");
  Serial.printf("  Last selected beat action: %s\n", beatActionName(lastSelectedAction));
  Serial.printf("  Time since last reversal: %lums\n", (unsigned long)(now - lastReversalMs));
  Serial.printf("  Time in current direction: %lums\n", (unsigned long)(now - directionStartMs));
  Serial.printf("  Rotation commitment: %lu/%lums (favor-continuation gate; strong accent/phrase boundary bypass it)\n",
                (unsigned long)min((unsigned long)(now - directionStartMs), (unsigned long)tunableMinRotationHoldMs),
                (unsigned long)tunableMinRotationHoldMs);
  Serial.printf("  Time since last spin: %lums   Spin cooldown: %lums\n", (unsigned long)(now - lastSpinMs),
                (unsigned long)tunableSpinCooldownMs);
  Serial.printf("  Spin remaining: %lums\n", (unsigned long)spinRemainingMs(now));
  Serial.printf("  Quiet timer: %lums / %lums (stops at timeout)\n",
                belowSilenceThresholdSinceMs != 0 ? (unsigned long)(now - belowSilenceThresholdSinceMs) : 0UL,
                (unsigned long)MUSIC_MOTOR_SILENCE_TIMEOUT_MS);
  Serial.printf("  Target duration (current phase): %lums\n", (unsigned long)currentTargetDurationMs(now));
  Serial.printf("  LOW target range: %u-%u%%   Hip-shake target: %u%%\n", (unsigned)tunableLowMinPercent,
                (unsigned)tunableLowMaxPercent, (unsigned)tunableFastPercent);
  Serial.printf("  Beat threshold: %.3f   Strong-hit threshold: %.3f\n", (double)tunableBeatThreshold,
                (double)tunableStrongHitThreshold);
  Serial.printf("  Intensity thresholds: LOW=%.2f MEDIUM=%.2f HIGH=%.2f PEAK=%.2f (hysteresis %.2f)\n",
                (double)tunableLowThreshold, (double)tunableMediumThreshold, (double)tunableHighThreshold,
                (double)tunablePeakThreshold, (double)MUSIC_MOTOR_INTENSITY_HYSTERESIS);
  Serial.printf("  Accel: %lums   Hold: %lu-%lums   Decel: %lums\n", (unsigned long)tunableAccelMs,
                (unsigned long)tunableHoldMinMs, (unsigned long)tunableHoldMaxMs, (unsigned long)tunableDecelMs);
  if (spinTimeManualOverride) {
    Serial.printf("  Spin profile: MANUAL OVERRIDE -- M%u for %lums\n", (unsigned)MUSIC_MOTOR_SPIN_FAST_PERCENT,
                  (unsigned long)spinTimeManualOverrideMs);
  } else {
    Serial.printf("  Spin profiles: NORMAL M%u/%lums   FAST M%u/%lums   EXTENDED_DROP M%u/%lu-%lums (max %lums)\n",
                  (unsigned)MUSIC_MOTOR_SPIN_NORMAL_PERCENT, (unsigned long)MUSIC_MOTOR_SPIN_NORMAL_MS,
                  (unsigned)MUSIC_MOTOR_SPIN_FAST_PERCENT, (unsigned long)MUSIC_MOTOR_SPIN_FAST_MS,
                  (unsigned)MUSIC_MOTOR_SPIN_EXTENDED_PERCENT, (unsigned long)MUSIC_MOTOR_SPIN_EXTENDED_MIN_MS,
                  (unsigned long)MUSIC_MOTOR_SPIN_EXTENDED_MAX_MS, (unsigned long)MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS);
  }
  Serial.printf("  Drop hold: %s   remaining=%lums   session started %lums ago\n", dropHoldActive ? "ACTIVE" : "inactive",
                dropHoldActive && dropHoldUntilMs > now ? (unsigned long)(dropHoldUntilMs - now) : 0UL,
                dropHoldActive ? (unsigned long)(now - dropHoldStartMs) : 0UL);
  {
    bool sdActive = (state == MusicMotorState::SUSTAINED_DRIVE);
    unsigned long phraseElapsedMs = sdActive ? (unsigned long)(now - sustainedDriveEnteredMs) : 0UL;
    unsigned long minCommitmentRemainingMs =
        (sdActive && (now - sustainedDriveDirectionCommitStartMs) < sustainedDriveMinCommitmentMs)
            ? (unsigned long)(sustainedDriveMinCommitmentMs - (now - sustainedDriveDirectionCommitStartMs))
            : 0UL;
    unsigned long nextReviewInMs =
        (sdActive && sustainedDriveNextReviewMs > now) ? (unsigned long)(sustainedDriveNextReviewMs - now) : 0UL;
    unsigned long sdCooldownRemainingMs =
        ((long)(sustainedDriveCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedDriveCooldownUntilMs - now) : 0UL;
    unsigned long switchCooldownRemainingMs =
        ((long)(sustainedSwitchCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedSwitchCooldownUntilMs - now) : 0UL;
    bool switchEligibleNow = sdActive && minCommitmentRemainingMs == 0;
    bool sdEligibleNow =
        !sdActive && state != MusicMotorState::OFF && state != MusicMotorState::SILENT &&
        (int)effectiveBand() >= (int)MusicIntensityBand::BAND_MEDIUM && sdCooldownRemainingMs == 0;
    bool renewableEligible = sdActive && minCommitmentRemainingMs == 0;  // may EXTEND at (or past) its next review, indefinitely, while energy supports it
    bool promotionEligible = sdActive && sustainedDrivePhraseTier == SustainedDrivePhraseTier::SHORT;
    Serial.printf("  Sustained drive: %s   direction=%s   tier=%s   eligibleNow=%s\n", sdActive ? "ACTIVE" : "inactive",
                  dirLetter(sustainedDriveDirection), phraseTierName(sustainedDrivePhraseTier), sdEligibleNow ? "yes" : "no");
    Serial.printf(
        "  Phrase: elapsedMs=%lu minCommitmentRemainingMs=%lu nextReviewInMs=%lu extensions=%u lastExtensionMs=%lu "
        "lastExtensionReason=%s\n",
        phraseElapsedMs, minCommitmentRemainingMs, nextReviewInMs, (unsigned)sustainedDriveExtensionCount,
        (unsigned long)sustainedDriveLastExtensionMs, sustainedDriveLastExtensionReason);
    Serial.printf("  Low-energy grace: %s (%lums / %lums)   renewableEligible=%s   promotionEligible=%s\n",
                  sustainedDriveLowEnergySinceMs != 0 ? "ACTIVE" : "inactive",
                  sustainedDriveLowEnergySinceMs != 0 ? (unsigned long)(now - sustainedDriveLowEnergySinceMs) : 0UL,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_LOW_GRACE_MS, renewableEligible ? "yes" : "no",
                  promotionEligible ? "yes" : "no");
    Serial.printf(
        "  Sustained-direction switch: eligibleNow=%s   switchesInPhrase=%u   switchCooldownRemainingMs=%lu   "
        "lastExitReason=%s\n",
        switchEligibleNow ? "yes" : "no", (unsigned)sustainedDriveSwitchCount, switchCooldownRemainingMs, sustainedDriveExitReason);
    Serial.printf("  Sustained drive re-entry cooldown remaining: %lums\n", sdCooldownRemainingMs);
  }
  Serial.printf("  Wobble cue (this tick): %s\n", wobbleCueActive ? "yes" : "no");
  Serial.printf("  Hip-shake cooldown remaining: %lums\n",
                (now - lastHipShakeStartMs < hipShakeStartCooldownMs) ? (unsigned long)(hipShakeStartCooldownMs - (now - lastHipShakeStartMs))
                                                                       : 0UL);
  if (state == MusicMotorState::REVERSE_HIP_SHAKE) {
    Serial.printf("  Reverse hip shake: variant=%s phase=%u/%u originalDirection=%s\n", reverseHipShakeHeavy ? "HEAVY" : "REGULAR",
                  (unsigned)(reverseHipShakePhaseIndex + 1), (unsigned)reverseHipShakeTotalPhases,
                  dirName(reverseHipShakeOriginalDirection));
  }
  if (state == MusicMotorState::SUSTAINED_DRIVE) {
    Serial.printf("  Sustained drive detail: direction=%s targetIntensity=M%u consecutiveSameDirPhrases=%u\n",
                  dirName(sustainedDriveDirection), (unsigned)sustainedDriveTargetPercent,
                  (unsigned)consecutiveSustainedDriveSameDirectionCount);
  }
  if (state == MusicMotorState::MUSICAL_RAMP_DOWN) {
    unsigned long rampElapsed = now - rampStartMs;
    uint8_t progressPercent = (rampDurationMs > 0) ? (uint8_t)min(100UL, (unsigned long)(100UL * rampElapsed / rampDurationMs)) : 100;
    unsigned long rampRemainingMs = (rampStartMs + rampDurationMs > now) ? (unsigned long)(rampStartMs + rampDurationMs - now) : 0UL;
    Serial.printf(
        "  Musical ramp-down: style=%s reason=%s startSpeed=M%u target=M0 progress=%u%% remainingMs=%lu "
        "interruptEligible=yes (resumes on any non-QUIET reading)\n",
        stopStyleName(lastStopStyle), lastStopStyleReason, (unsigned)rampFromPercent, (unsigned)progressPercent, rampRemainingMs);
  }
  Serial.printf("  Last stop: style=%s reason=%s\n", stopStyleName(lastStopStyle), lastStopStyleReason);
  Serial.printf("  Motor ownership: %s\n", state != MusicMotorState::OFF ? "MusicMotorController" : "none (available)");
  Serial.printf("  Emergency-stop state: %s\n", lastStopWasEmergency ? "LATCHED (last stop was emergency/mstop)" : "clear");
  Serial.printf("  Debug logging: %s\n", debugLoggingEnabled ? "ON" : "OFF");
  {
    // Revision 9 -- relative/song-adaptive drop detection. Distinguishes,
    // per the explicit diagnostic requirement: absolute-band qualification
    // (measured/effective band above), Revision 8 sustained-drive
    // qualification (the "Sustained drive" block above -- band-weighted
    // roll or persistent-energy dwell), relative EDM drop qualification
    // (this block), and the combined final entry reason actually used
    // (lastSelectionReason, printed below).
    Serial.printf("  Relative drop detection (EDM/dubstep, A/B toggle): %s\n", relativeDropDetectionEnabled ? "ON" : "OFF");
    unsigned long sectionElapsedMs = now - musicalSectionPhaseEnteredMs;
    unsigned long dropActiveElapsedMs = (dropActiveSinceMs != 0) ? (now - dropActiveSinceMs) : 0UL;
    Serial.printf(
        "  Musical section phase: %s (elapsed %lums)   Drop confidence: %.2f (tier=%s)   Drop-active elapsed: %lums\n",
        musicalSectionPhaseName(musicalSectionPhase), sectionElapsedMs, (double)dropConfidence,
        dropConfidenceTierName(dropConfidenceTier), dropActiveElapsedMs);
    Serial.printf(
        "  Drop signals: energyRise=%.2f sectionContrast=%.2f bassImpact=%.2f bassDensity=%.2f beatDensity=%.2f "
        "transientDensity=%.2f buildupResolution=%.2f sustainedEnergy=%.2f\n",
        (double)dropEnergyRiseScore, (double)dropSectionContrastScore, (double)dropBassImpactScore, (double)dropBassDensityScore,
        (double)dropBeatDensityScore, (double)dropTransientDensityScore, (double)dropBuildupResolutionScore,
        (double)dropSustainedEnergyScore);
    Serial.printf("  Sustained phrase started during current drop: %s   Sustained speed floor tier: %s\n",
                  sustainedPhraseStartedThisDrop ? "yes" : "no", lastSustainedFloorTierName);
    Serial.printf("  Combined final entry reason (last selection): %s\n", lastSelectionReason);
  }
  {
    // Revision 10 -- speed authority / motion tier / drop phrase.
    Serial.printf("  Motion tier: %s   measuredBand=%s effectiveBand=%s\n", motionTierName(currentMotionTier),
                  intensityBandName(intensityBand), intensityBandName(effectiveBand()));
    Serial.printf("  Speed authority: cap=M%u source=%s   quietBuildupMotion=%s\n", (unsigned)lastAuthorityCapPercent,
                  lastAuthorityCapSource, quietBuildupMotionEnabled ? "ON" : "OFF");
    Serial.printf("  Movement duty: %s (window ends in %lums)\n", motionDutyPulseOn ? "PULSE_ON" : "REST",
                  (long)(motionDutyWindowEndMs - now) > 0 ? (unsigned long)(motionDutyWindowEndMs - now) : 0UL);
    if (state == MusicMotorState::SUSTAINED_DRIVE) {
      const char *transitionPhaseName = dropPhraseTransitionPhase == DropPhraseTransitionPhase::DRIVING     ? "DRIVING"
                                         : dropPhraseTransitionPhase == DropPhraseTransitionPhase::DECEL     ? "RAMP_DOWN"
                                                                                                              : "COAST";
      Serial.printf(
          "  Drop phrase: type=%s reason=%s step=%u/%u transitionPhase=%s   boothShakesThisDrop=%u "
          "sustainedReversalsThisDrop=%u directionChangesThisDrop=%u\n",
          dropPhraseTypeName(currentDropPhraseType), lastDropPhraseSelectionReason, (unsigned)dropPhraseStepIndex,
          (unsigned)dropPhraseStepCount, transitionPhaseName, (unsigned)dropPhraseBoothShakesThisDrop,
          (unsigned)dropPhraseSustainedReversalsThisDrop, (unsigned)dropPhraseTotalDirectionChangesThisDrop);
      Serial.printf("  Invariant: violationSinceMs=%lu (0=clear)   stoppedInvariantCount=%lu   recoveryCount=%lu   abortCount=%lu\n",
                    dropPhraseInvariantViolationSinceMs != 0 ? (unsigned long)(now - dropPhraseInvariantViolationSinceMs) : 0UL,
                    (unsigned long)summarySustainedDriveStoppedInvariantCount, (unsigned long)summaryDropPhraseRecoveryCount,
                    (unsigned long)summaryDropPhraseAbortCount);
    }
  }
  if (state == MusicMotorState::OFF || currentSpeedPercent == 0) {
    Serial.println(F("  GPIO8 (IN1): LOW   GPIO9 (IN2): LOW"));
  } else if (activeDrivingDirection() == MusicMotorDirection::FORWARD) {
    Serial.printf("  GPIO8 (IN1): PWM duty=%u/255   GPIO9 (IN2): LOW\n", percentToMotorPwm(currentSpeedPercent));
  } else {
    Serial.printf("  GPIO8 (IN1): LOW   GPIO9 (IN2): PWM duty=%u/255\n", percentToMotorPwm(currentSpeedPercent));
  }
}

void musicMotorPrintIntensity() {
  unsigned long now = millis();
  Serial.println(F("[MUSIC MOTOR INTENSITY]"));
  Serial.printf("  rawNormalized=%.3f fastEnergy=%.3f songEnergy=%.3f baselineEnergy=%.3f transientDelta=%.3f "
                "performanceEnergy=%.3f\n",
                (double)rawEnergy, (double)fastEnergy, (double)songEnergy, (double)baselineEnergy,
                (double)transientDelta, (double)performanceEnergy);
  Serial.printf("  measuredBand=%s effectiveBand=%s intensityTarget=M%u actual=M%u commanded=M%u\n",
                intensityBandName(intensityBand), intensityBandName(effectiveBand()), (unsigned)intensityTargetPercent,
                (unsigned)currentSpeedPercent, (unsigned)currentSpeedPercent);
  Serial.printf("  thresholds: LOW=%.3f MEDIUM=%.3f HIGH=%.3f PEAK=%.3f hysteresis=%.3f\n", (double)tunableLowThreshold,
                (double)tunableMediumThreshold, (double)tunableHighThreshold, (double)tunablePeakThreshold,
                (double)MUSIC_MOTOR_INTENSITY_HYSTERESIS);
  Serial.printf("  beatThreshold=%.3f strongHitThreshold=%.3f\n", (double)tunableBeatThreshold,
                (double)tunableStrongHitThreshold);
  Serial.printf("  sustainedHighPeakMs=%lu / %lums (alternate drop-hold qualification)\n",
                sustainedHighSinceMs != 0 ? (unsigned long)(now - sustainedHighSinceMs) : 0UL,
                (unsigned long)MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS);
  Serial.printf("  quietTimerMs=%lu / %lums   dropHold=%d\n",
                belowSilenceThresholdSinceMs != 0 ? (unsigned long)(now - belowSilenceThresholdSinceMs) : 0UL,
                (unsigned long)MUSIC_MOTOR_SILENCE_TIMEOUT_MS, dropHoldActive ? 1 : 0);
  Serial.printf("  [revision 9] relativeDropDetection=%s musicalSectionPhase=%s dropConfidence=%.2f dropTier=%s\n",
                relativeDropDetectionEnabled ? "ON" : "OFF", musicalSectionPhaseName(musicalSectionPhase), (double)dropConfidence,
                dropConfidenceTierName(dropConfidenceTier));
}

// 'musicmotor debug on'/'off': enables/disables the revision-4 detailed
// decision diagnostics (dropHold/band/strongHit/choreography evaluation
// lines). Purely a logging toggle -- reading debugLoggingEnabled never
// influences any threshold, cooldown, or choreography decision anywhere in
// this file; every log*Evaluation()/logChoreographySelection() function
// early-returns on this flag before computing or printing anything.
void musicMotorSetDebugLogging(bool enabled) {
  debugLoggingEnabled = enabled;
  Serial.printf("[MUSIC MOTOR] debug logging %s\n", enabled ? "ON" : "OFF");
}

bool musicMotorIsDebugLoggingEnabled() { return debugLoggingEnabled; }

// 'musicmotor debug status'
void musicMotorPrintDebugStatus() {
  Serial.printf("[MUSIC MOTOR DEBUG] logging=%s rateLimitMs=%lu\n", debugLoggingEnabled ? "ON" : "OFF",
                (unsigned long)MUSIC_MOTOR_DEBUG_RATE_LIMIT_MS);
}

// 'musicmotor dropdetect on'/'off': the Revision 9 relative/song-adaptive
// drop-detection A/B toggle -- see Config.h's
// MUSIC_MOTOR_RELATIVE_DROP_ENABLED_DEFAULT comment. Persists across
// musicmotor on/off within a session, same treatment as debugLoggingEnabled
// (only reset to the Config.h default at true boot, in
// initMusicMotorController()) -- an operator doing an A/B comparison
// shouldn't have the toggle silently reset between the two passes.
void musicMotorSetRelativeDropEnabled(bool enabled) {
  relativeDropDetectionEnabled = enabled;
  Serial.printf("[MUSIC MOTOR] relative drop detection (EDM/dubstep, revision 9) %s\n", enabled ? "ON" : "OFF");
}

bool musicMotorIsRelativeDropEnabled() { return relativeDropDetectionEnabled; }

// 'musicmotor quietmotion on'/'off' -- Revision 10 QUIET_BUILDUP motion
// toggle, independent of the broader relative-drop A/B toggle above.
void musicMotorSetQuietBuildupMotionEnabled(bool enabled) {
  quietBuildupMotionEnabled = enabled;
  Serial.printf("[MUSIC MOTOR] quiet-buildup motion %s\n", enabled ? "ON" : "OFF");
}
bool musicMotorIsQuietBuildupMotionEnabled() { return quietBuildupMotionEnabled; }

// 'musicmotor switchchance <0-100>' -- Revision 10 drop-phrase mid-drop
// reselection roll chance (see maybeTriggerDropPhraseReselection()).
void musicMotorSetSwitchChancePercent(uint8_t percent) {
  tunableSwitchChancePercent = (uint8_t)constrain((int)percent, 0, 100);
  Serial.printf("[MUSIC MOTOR] drop-phrase switch chance set to %u%%\n", (unsigned)tunableSwitchChancePercent);
}
// 'musicmotor switchcooldown <ms>' -- cooldown after a booty-shake/punch
// sequence completes before another non-FULL_SUSTAIN sequence may begin.
void musicMotorSetSwitchCooldownMs(uint32_t ms) {
  tunableDropPhraseSequenceCooldownMs = ms;
  Serial.printf("[MUSIC MOTOR] drop-phrase sequence cooldown set to %lums\n", (unsigned long)tunableDropPhraseSequenceCooldownMs);
}
// 'musicmotor switchlimit <count>' -- maximum committed sustained
// reversals per drop (the "unless the drop is unusually long" extra
// allowance in Config.h's MUSIC_MOTOR_DROP_PHRASE_LONG_DROP_EXTRA_REVERSAL_MS
// is still added on top of whatever this is set to).
void musicMotorSetSwitchLimit(uint8_t count) {
  tunableDropPhraseSwitchLimit = count;
  Serial.printf("[MUSIC MOTOR] drop-phrase sustained-reversal limit set to %u per drop\n", (unsigned)tunableDropPhraseSwitchLimit);
}

// 'musicmotor dynamics status' -- Revision 10 config surface: speed-
// authority cap, motion palette, duty-cycle timing, drop-phrase limits.
void musicMotorPrintDynamicsStatus() {
  Serial.println(F("[MUSIC MOTOR DYNAMICS]"));
  Serial.printf("  Quiet-buildup motion: %s   Quiet cap grace: %lums   Room noise floor: %.3f\n",
                quietBuildupMotionEnabled ? "ON" : "OFF", (unsigned long)MUSIC_MOTOR_QUIET_CAP_GRACE_MS,
                (double)MUSIC_MOTOR_ROOM_NOISE_FLOOR);
  Serial.printf("  Medium lending bounded raise: +%u%%\n", (unsigned)MUSIC_MOTOR_MEDIUM_LENDING_BOUNDED_RAISE_PERCENT);
  Serial.printf("  Motion palette: QUIET_BUILDUP=M%u MELLOW=M%u-%u GROOVE=M%u-%u HIGH_ENERGY=M%u-%u "
                "CONFIRMED_DROP=M%u MAJOR_DROP=M%u\n",
                (unsigned)MUSIC_MOTOR_MOTION_QUIET_BUILDUP_PERCENT, (unsigned)MUSIC_MOTOR_MOTION_MELLOW_MIN_PERCENT,
                (unsigned)MUSIC_MOTOR_MOTION_MELLOW_MAX_PERCENT, (unsigned)MUSIC_MOTOR_MOTION_GROOVE_MIN_PERCENT,
                (unsigned)MUSIC_MOTOR_MOTION_GROOVE_MAX_PERCENT, (unsigned)MUSIC_MOTOR_MOTION_HIGH_ENERGY_MIN_PERCENT,
                (unsigned)MUSIC_MOTOR_MOTION_HIGH_ENERGY_MAX_PERCENT, (unsigned)MUSIC_MOTOR_MOTION_CONFIRMED_DROP_PERCENT,
                (unsigned)MUSIC_MOTOR_MOTION_MAJOR_DROP_PERCENT);
  Serial.printf("  QUIET_BUILDUP duty: pulse %lu-%lums / rest %lu-%lums\n", (unsigned long)MUSIC_MOTOR_QUIET_BUILDUP_PULSE_MIN_MS,
                (unsigned long)MUSIC_MOTOR_QUIET_BUILDUP_PULSE_MAX_MS, (unsigned long)MUSIC_MOTOR_QUIET_BUILDUP_REST_MIN_MS,
                (unsigned long)MUSIC_MOTOR_QUIET_BUILDUP_REST_MAX_MS);
  Serial.printf("  MELLOW duty: pulse %lu-%lums / rest %lu-%lums\n", (unsigned long)MUSIC_MOTOR_MELLOW_PULSE_MIN_MS,
                (unsigned long)MUSIC_MOTOR_MELLOW_PULSE_MAX_MS, (unsigned long)MUSIC_MOTOR_MELLOW_REST_MIN_MS,
                (unsigned long)MUSIC_MOTOR_MELLOW_REST_MAX_MS);
  Serial.printf("  Drop entry: major immediate=%u%% guaranteed after %lums   confirmed guaranteed after %lums\n",
                (unsigned)MUSIC_MOTOR_EDM_ENTRY_MAJOR_IMMEDIATE_PERCENT, (unsigned long)MUSIC_MOTOR_EDM_ENTRY_MAJOR_GUARANTEE_AFTER_MS,
                (unsigned long)MUSIC_MOTOR_EDM_ENTRY_CONFIRMED_GUARANTEE_AFTER_MS);
  Serial.printf("  Drop phrase: punch=M%u %lu-%lums   decel=%lums   switchChance=%u%%   sequenceCooldown=%lums   "
                "sustainedReversalLimit=%u/drop (+1 after %lums)   boothShakeLimit=%u/drop (max %u reversals/shake)\n",
                (unsigned)MUSIC_MOTOR_DROP_PHRASE_PUNCH_PERCENT, (unsigned long)MUSIC_MOTOR_DROP_PHRASE_PUNCH_MIN_MS,
                (unsigned long)MUSIC_MOTOR_DROP_PHRASE_PUNCH_MAX_MS, (unsigned long)MUSIC_MOTOR_DROP_PHRASE_DECEL_MS,
                (unsigned)tunableSwitchChancePercent, (unsigned long)tunableDropPhraseSequenceCooldownMs,
                (unsigned)tunableDropPhraseSwitchLimit, (unsigned long)MUSIC_MOTOR_DROP_PHRASE_LONG_DROP_EXTRA_REVERSAL_MS,
                (unsigned)MUSIC_MOTOR_DROP_PHRASE_MAX_BOOTY_SHAKES_PER_DROP,
                (unsigned)MUSIC_MOTOR_DROP_PHRASE_MAX_REVERSALS_PER_SHAKE);
  Serial.printf("  Minimum drop-active duration -- reversal: %lums   shake: %lums   anti-repeat weight multiplier: %.2f\n",
                (unsigned long)MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_REVERSAL,
                (unsigned long)MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_SHAKE,
                (double)MUSIC_MOTOR_DROP_PHRASE_ANTIREPEAT_WEIGHT_MULTIPLIER);
}

// 'musicmotor test': one-command physical-validation setup. Re-enables
// through the SAME safe path 'musicmotor on' always uses (musicMotorEnable()
// -- ownership preemption, resetRuntimeState(), and the same
// lastStopWasEmergency=false clear every ordinary enable already performs;
// nothing here bypasses or duplicates that logic with a second, competing
// mechanism). If something else currently owns the motor, enable is
// refused exactly as it always is and this reports that plainly rather
// than silently doing nothing.
void musicMotorEnterTestMode() {
  Serial.println(F("[MUSIC MOTOR TEST] Physical validation mode enabling..."));
  if (state != MusicMotorState::OFF) {
    musicMotorDisable();  // guarantees any stale phrase/sustained-drive/ramp state is fully cleared, not merely reset in place
  }
  musicMotorEnable();
  if (state == MusicMotorState::OFF) {
    Serial.println(F("[MUSIC MOTOR TEST] FAILED -- musicmotor on was refused (another motor system currently owns the motor)"));
    return;
  }
  musicMotorSetRelativeDropEnabled(true);
  musicMotorSetDebugLogging(true);
  musicMotorSetQuietBuildupMotionEnabled(true);
  resetRuntimeState();  // explicit fresh summary counters, per the requested numbered step -- already clean via musicMotorEnable() above, this just makes the guarantee explicit rather than incidental
  Serial.println(F("[MUSIC MOTOR TEST] Physical validation mode enabled"));
  Serial.printf("  musicMotor=%s\n", state != MusicMotorState::OFF ? "ON" : "OFF");
  Serial.printf("  dropDetection=%s\n", relativeDropDetectionEnabled ? "ON" : "OFF");
  Serial.printf("  debug=%s\n", debugLoggingEnabled ? "ON" : "OFF");
  Serial.println(F("  summary=RESET"));
  Serial.printf("  movementState=%s\n", reportedStateName());
  Serial.printf("  phraseState=%s (%s)\n", dropPhraseTypeName(currentDropPhraseType),
                (dropPhraseStepIndex < dropPhraseStepCount) ? "ACTIVE" : "IDLE");
  Serial.printf("  emergencyStop=%s\n", lastStopWasEmergency ? "LATCHED" : "CLEARED");
  musicMotorPrintDynamicsStatus();
}

// 'musicmotor test stop': safely stops, finalizes/prints the session
// summary while it's still populated, disables verbose test logging, and
// returns to a clean idle (disabled) state via the same musicMotorDisable()
// path every ordinary stop uses.
void musicMotorExitTestMode() {
  Serial.println(F("[MUSIC MOTOR TEST] Stopping physical validation mode..."));
  musicMotorPrintSummary();
  musicMotorSetDebugLogging(false);
  musicMotorDisable();
  Serial.printf("[MUSIC MOTOR TEST] Physical validation mode ended -- movementState=%s\n", reportedStateName());
}

// 'musicmotor summary': compact post-song session statistics -- see
// Config.h's/this file's "Revision 9" comments. Counters accumulate from
// the most recent 'musicmotor on' (see resetRuntimeState()); does not
// persist across a musicmotor off/on cycle or a reboot.
void musicMotorPrintSummary() {
  Serial.println(F("[MUSIC MOTOR SUMMARY]"));
  Serial.printf("  Firmware: %s (built %s)   Feature revision: %s\n", FIRMWARE_REVISION_TAG, FIRMWARE_BUILD_IDENTIFIER,
                FIRMWARE_MUSIC_MOTOR_FEATURE_REVISION);
  Serial.printf("  Relative drop detection (A/B toggle): %s\n", relativeDropDetectionEnabled ? "ON" : "OFF");
  Serial.printf("  Band time distribution: QUIET=%lums LOW=%lums MEDIUM=%lums HIGH=%lums PEAK=%lums\n", summaryBandMs[0],
                summaryBandMs[1], summaryBandMs[2], summaryBandMs[3], summaryBandMs[4]);
  Serial.printf("  Strong-hit count: %lu   DropHold start count: %lu\n", (unsigned long)summaryStrongHitCount,
                (unsigned long)summaryDropHoldStartCount);
  Serial.printf("  Relative drop-active count: %lu   Drops with no sustained-drive opportunity taken: %lu\n",
                (unsigned long)summaryRelativeDropActiveCount, (unsigned long)summaryDropsWithNoSustainedDrive);
  Serial.printf("  Sustained-drive entry count: %lu   Extension count: %lu   Direction-switch count: %lu\n",
                (unsigned long)summarySustainedDriveEntryCount, (unsigned long)summarySustainedDriveExtensionCount,
                (unsigned long)summarySustainedDriveSwitchCount);
  unsigned long avgPhraseMs = (summaryPhraseCount != 0) ? (summaryPhraseDurationSumMs / summaryPhraseCount) : 0UL;
  Serial.printf("  Phrase count: %lu   Average duration: %lums   Max duration: %lums\n", (unsigned long)summaryPhraseCount,
                avgPhraseMs, summaryMaxPhraseDurationMs);
  Serial.printf("  Max commanded/applied speed this session: target=M%u applied=M%u\n", (unsigned)summaryMaxTargetPercent,
                (unsigned)summaryMaxAppliedPercent);
  Serial.printf(
      "  Relative-drop entry rejections: alreadyActive=%lu silent=%lu cooldownActive=%lu weightRollFailed=%lu\n",
      (unsigned long)summaryRelativeEntryRejectAlreadyActive, (unsigned long)summaryRelativeEntryRejectSilent,
      (unsigned long)summaryRelativeEntryRejectCooldown, (unsigned long)summaryRelativeEntryRejectRollFailed);
  Serial.printf("  Current musical section phase: %s   Drop confidence: %.2f (tier=%s)\n",
                musicalSectionPhaseName(musicalSectionPhase), (double)dropConfidence, dropConfidenceTierName(dropConfidenceTier));
  // Revision 10 -- motion tier time + drop-phrase-vocabulary stats.
  Serial.printf("  Motion tier time: MELLOW=%lums GROOVE=%lums CONFIRMED_DROP_DRIVE=%lums MAJOR_DROP_DRIVE=%lums\n",
                summaryMellowMotionMs, summaryGrooveMotionMs, summaryConfirmedDropDriveMs, summaryMajorDropDriveMs);
  Serial.printf("  Quiet-buildup motions: %lu\n", (unsigned long)summaryQuietBuildupMotionCount);
  Serial.printf(
      "  Speed-authority regression check (should be ~0): QUIET-over-cap=%lums   LOW-over-cap=%lums\n",
      summaryQuietOverCapMs, summaryLowOverCapMs);
  Serial.printf(
      "  Drop phrase counts: FULL_SUSTAIN=%lu SUSTAINED_REVERSAL=%lu DROP_BOOTY_SHAKE=%lu DROP_PUNCH_AND_HOLD=%lu "
      "DOUBLE_PUNCH=%lu SUSTAIN_WITH_ACCENTS=%lu\n",
      (unsigned long)summaryFullSustainCount, (unsigned long)summarySustainedReversalCount, (unsigned long)summaryDropBootyShakeCount,
      (unsigned long)summaryDropPunchAndHoldCount, (unsigned long)summaryDoublePunchCount, (unsigned long)summarySustainWithAccentsCount);
  unsigned long avgConfirmedChanges =
      (summaryConfirmedDropCountForAvg != 0) ? (summaryConfirmedDropDirectionChangesSum / summaryConfirmedDropCountForAvg) : 0UL;
  unsigned long avgMajorChanges =
      (summaryMajorDropCountForAvg != 0) ? (summaryMajorDropDirectionChangesSum / summaryMajorDropCountForAvg) : 0UL;
  Serial.printf(
      "  Direction changes per drop: avgConfirmed=%lu avgMajor=%lu max=%u   Drops using >1 phrase: %lu   Repeated "
      "identical selections: %lu   Safety-blocked reversals: %lu\n",
      avgConfirmedChanges, avgMajorChanges, (unsigned)summaryMaxDirectionChangesInOneDrop,
      (unsigned long)summaryDropsUsingMultiplePhrases, (unsigned long)summaryRepeatedIdenticalPhraseSelections,
      (unsigned long)summarySafetyBlockedReversalCount);
  // Revision 10.1 -- invariant/recovery counters. Expected to read zero
  // (or very near it) for a normal, healthy session.
  Serial.printf(
      "  Invariant violations: %lu   Recoveries (restarted FULL_SUSTAIN): %lu   Aborts (exited sustained-drive): %lu   "
      "Unexpected-stopped time: %lums\n",
      (unsigned long)summarySustainedDriveStoppedInvariantCount, (unsigned long)summaryDropPhraseRecoveryCount,
      (unsigned long)summaryDropPhraseAbortCount, summaryUnexpectedStoppedMs);
}

// 'musicmotor motion': the physical-calibration-derived tuning surface --
// see Config.h's "Physical calibration (revision 3)" comment for the
// source measurements this is all derived from.
void musicMotorPrintMotion() {
  Serial.println(F("[MUSIC MOTOR MOTION]"));
  Serial.printf("  Active-movement floor: M%u (validated minimum reliable command)\n",
                (unsigned)MUSIC_MOTOR_ACTIVE_MIN_PERCENT);
  Serial.printf("  LOW: M%u-%u   MEDIUM: M%u-%u   HIGH: M%u-%u   PEAK: M%u-%u\n", (unsigned)tunableLowMinPercent,
                (unsigned)tunableLowMaxPercent, (unsigned)MUSIC_MOTOR_MEDIUM_MIN_PERCENT,
                (unsigned)MUSIC_MOTOR_MEDIUM_MAX_PERCENT, (unsigned)MUSIC_MOTOR_HIGH_MIN_PERCENT,
                (unsigned)MUSIC_MOTOR_HIGH_MAX_PERCENT, (unsigned)MUSIC_MOTOR_PEAK_MIN_PERCENT,
                (unsigned)MUSIC_MOTOR_PEAK_MAX_PERCENT);
  Serial.printf("  Drop hold: initial=%lums   max continuous=%lums   sustainConfirm=%lums (HIGH/PEAK dwell, no hit needed)\n",
                (unsigned long)MUSIC_MOTOR_DROP_HOLD_INITIAL_MS, (unsigned long)MUSIC_MOTOR_DROP_HOLD_MAX_MS,
                (unsigned long)MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS);
  Serial.printf("  Silence timeout (persistent-quiet stop): %lums\n", (unsigned long)MUSIC_MOTOR_SILENCE_TIMEOUT_MS);
  Serial.printf("  Performance energy: attack=%.3f/tick   release=%.4f/tick   strongHitBump=%.2f   beatBump=%.2f\n",
                (double)MUSIC_MOTOR_PERFORMANCE_ATTACK_PER_TICK, (double)MUSIC_MOTOR_PERFORMANCE_RELEASE_PER_TICK,
                (double)MUSIC_MOTOR_PERFORMANCE_STRONG_HIT_BUMP, (double)MUSIC_MOTOR_PERFORMANCE_BEAT_BUMP);
  Serial.printf("  Wobble: deltaThreshold=%.3f   refractory=%lums\n", (double)MUSIC_MOTOR_WOBBLE_DELTA_THRESHOLD,
                (unsigned long)MUSIC_MOTOR_WOBBLE_REFRACTORY_MS);
  Serial.printf("  Hip-shake start cooldown: %lu-%lums\n", (unsigned long)MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MIN_MS,
                (unsigned long)MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MAX_MS);
  Serial.printf("  Regular reverse hip shake: M%u, %lums x %u phases (opposite/original/opposite/original), ~%lums total\n",
                (unsigned)MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PERCENT, (unsigned long)MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_MS,
                (unsigned)MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_COUNT,
                (unsigned long)(MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_MS * MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_COUNT));
  Serial.printf("  Heavy reverse hip shake:   M%u, %lums x %u phases, ~%lums total\n",
                (unsigned)MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PERCENT, (unsigned long)MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_MS,
                (unsigned)MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_COUNT,
                (unsigned long)(MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_MS * MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_COUNT));
  if (spinTimeManualOverride) {
    Serial.printf("  Spin: MANUAL OVERRIDE -- M%u for %lums\n", (unsigned)MUSIC_MOTOR_SPIN_FAST_PERCENT,
                  (unsigned long)spinTimeManualOverrideMs);
  } else {
    Serial.printf("  Spin NORMAL: M%u/%lums   Spin FAST: M%u/%lums   Spin EXTENDED_DROP: M%u/%lu-%lums\n",
                  (unsigned)MUSIC_MOTOR_SPIN_NORMAL_PERCENT, (unsigned long)MUSIC_MOTOR_SPIN_NORMAL_MS,
                  (unsigned)MUSIC_MOTOR_SPIN_FAST_PERCENT, (unsigned long)MUSIC_MOTOR_SPIN_FAST_MS,
                  (unsigned)MUSIC_MOTOR_SPIN_EXTENDED_PERCENT, (unsigned long)MUSIC_MOTOR_SPIN_EXTENDED_MIN_MS,
                  (unsigned long)MUSIC_MOTOR_SPIN_EXTENDED_MAX_MS);
  }
  Serial.printf("  Spin absolute max (hard ceiling): %lums   Spin cooldown: %lums\n",
                (unsigned long)MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS, (unsigned long)tunableSpinCooldownMs);
  Serial.printf("  Rotation-commitment hold (favor continuation): %lums -- bypassed by a strong accent (real PEAK "
                "or drop hold) or a phrase boundary (band transition)\n",
                (unsigned long)tunableMinRotationHoldMs);
  {
    unsigned long now = millis();
    bool sdActive = (state == MusicMotorState::SUSTAINED_DRIVE);
    unsigned long sdCooldownRemainingMs =
        ((long)(sustainedDriveCooldownUntilMs - now) > 0) ? (unsigned long)(sustainedDriveCooldownUntilMs - now) : 0UL;
    bool sdEligibleNow =
        !sdActive && state != MusicMotorState::OFF && state != MusicMotorState::SILENT &&
        (int)effectiveBand() >= (int)MusicIntensityBand::BAND_MEDIUM && sdCooldownRemainingMs == 0;
    Serial.printf("  Sustained drive: %s   direction=%s   tier=%s   eligibleNow=%s   re-entry cooldown remaining=%lums\n",
                  sdActive ? "ACTIVE" : "inactive", dirLetter(sustainedDriveDirection), phraseTierName(sustainedDrivePhraseTier),
                  sdEligibleNow ? "yes" : "no", sdCooldownRemainingMs);
    Serial.println(F("  Phrase-tier duration/review ranges:"));
    Serial.printf("    SHORT commitment: %lu-%lums (own floor -- does NOT use the %lums minimum below)\n",
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MAX_MS,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS);
    Serial.printf("    STANDARD/EXTENDED/RENEWABLE minimum commitment: %lums\n",
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS);
    Serial.printf("    Initial review -- MEDIUM: %lu-%lums   HIGH: %lu-%lums   PEAK/DropHold: %lu-%lums\n",
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_MEDIUM_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_MEDIUM_MAX_MS,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_HIGH_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_HIGH_MAX_MS,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_PEAK_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_PEAK_MAX_MS);
    Serial.printf("    Extension -- MEDIUM: %lu-%lums   HIGH: %lu-%lums   PEAK/DropHold: %lu-%lums (repeats indefinitely while supported)\n",
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_MEDIUM_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_MEDIUM_MAX_MS,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_HIGH_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_HIGH_MAX_MS,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_PEAK_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_PEAK_MAX_MS);
    Serial.printf("    Tier labels (diagnostic only): EXTENDED at %lums elapsed, RENEWABLE at %lums elapsed\n",
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_EXTENDED_ELAPSED_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_RENEWABLE_ELAPSED_MS);
    Serial.printf("  Short-tier entry weight: MEDIUM=%u%% HIGH=%u%% PEAK=%u%% (halved if DropHold active, +%u%% on a major "
                  "transient >=%.1fx strong-hit threshold)   promotion chance=%u%%\n",
                  (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_MEDIUM_PERCENT,
                  (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_HIGH_PERCENT,
                  (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_PEAK_PERCENT,
                  (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_MAJOR_TRANSIENT_BOOST_PERCENT,
                  (double)MUSIC_MOTOR_SUSTAINED_DRIVE_MAJOR_TRANSIENT_MULTIPLIER, (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_PROMOTION_PERCENT);
    Serial.printf("  Low-energy grace: %lums (effectiveBand < MEDIUM)   Re-entry cooldown: %lu-%lums (randomized per true exit)\n",
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_LOW_GRACE_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MIN_MS,
                  (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MAX_MS);
    Serial.printf(
        "  Direct sustained-direction switch: ordinary cooldown=%lu-%lums   exceptional-bypass floor=%lums "
        "(real PEAK + active DropHold together, on a qualified strong hit only)   flip chance=%u%%\n",
        (unsigned long)MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MIN_MS, (unsigned long)MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MAX_MS,
        (unsigned long)MUSIC_MOTOR_SUSTAINED_SWITCH_EXCEPTIONAL_MIN_MS, (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT);
    Serial.printf("  Cross-phrase consecutive-same-direction cap: %u\n",
                  (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_MAX_CONSECUTIVE_SAME_DIRECTION);
    Serial.printf(
        "  Entry weight (per qualifying strong hit, and per persistent-energy opportunity): MEDIUM=%u%% HIGH=%u%% "
        "PEAK=%u%%\n",
        (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_MEDIUM_PERCENT, (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_HIGH_PERCENT,
        (unsigned)MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_PEAK_PERCENT);
    Serial.printf(
        "  Persistent-energy entry opportunity (no fresh strong hit needed): requires %lums continuous HIGH/PEAK dwell, "
        "re-evaluated at most every %lums\n",
        (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_DWELL_MS,
        (unsigned long)MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_REVIEW_MS);
  }
  Serial.printf(
      "  Musical silence stop: gradual ramp-down %lu-%lums (low energy) / %lu-%lums (high energy)   abrupt-stop "
      "chance=%u%% normal / %u%% sharp cutoff (DropHold active within %lums)\n",
      (unsigned long)MUSIC_MOTOR_RAMP_DOWN_LOW_MIN_MS, (unsigned long)MUSIC_MOTOR_RAMP_DOWN_LOW_MAX_MS,
      (unsigned long)MUSIC_MOTOR_RAMP_DOWN_HIGH_MIN_MS, (unsigned long)MUSIC_MOTOR_RAMP_DOWN_HIGH_MAX_MS,
      (unsigned)MUSIC_MOTOR_ABRUPT_STOP_NORMAL_PERCENT, (unsigned)MUSIC_MOTOR_ABRUPT_STOP_SHARP_CUTOFF_PERCENT,
      (unsigned long)MUSIC_MOTOR_SHARP_CUTOFF_DROPHOLD_RECENCY_MS);
  Serial.println(F("  Safety stops (emergency stop/'k'/disable/hardware fault) always bypass musical ramp-down entirely."));
  Serial.printf("  Debug logging: %s (rate limit %lums) -- see 'musicmotor debug on/off/status'\n",
                debugLoggingEnabled ? "ON" : "OFF", (unsigned long)MUSIC_MOTOR_DEBUG_RATE_LIMIT_MS);
  {
    // Revision 9 -- EDM/dubstep relative-drop detector config surface. See
    // 'musicmotor dropdetect on/off' for the A/B toggle.
    Serial.printf("  Relative drop detection: %s (genre profile: EDM_DUBSTEP)\n",
                  relativeDropDetectionEnabled ? "ON" : "OFF");
    Serial.printf(
        "  Drop-confidence weights: energyRise=%.2f sectionContrast=%.2f bassImpact=%.2f bassDensity=%.2f "
        "beatDensity=%.2f transientDensity=%.2f buildupResolution=%.2f sustainedEnergy=%.2f\n",
        (double)MUSIC_MOTOR_EDM_WEIGHT_ENERGY_RISE, (double)MUSIC_MOTOR_EDM_WEIGHT_SECTION_CONTRAST,
        (double)MUSIC_MOTOR_EDM_WEIGHT_BASS_IMPACT, (double)MUSIC_MOTOR_EDM_WEIGHT_BASS_DENSITY,
        (double)MUSIC_MOTOR_EDM_WEIGHT_BEAT_DENSITY, (double)MUSIC_MOTOR_EDM_WEIGHT_TRANSIENT_DENSITY,
        (double)MUSIC_MOTOR_EDM_WEIGHT_BUILDUP_RESOLUTION, (double)MUSIC_MOTOR_EDM_WEIGHT_SUSTAINED_ENERGY);
    Serial.printf("  Drop-confidence tiers: POSSIBLE>=%.2f CONFIRMED>=%.2f MAJOR>=%.2f (active-sustain floor=%.2f)\n",
                  (double)MUSIC_MOTOR_EDM_POSSIBLE_DROP_THRESHOLD, (double)MUSIC_MOTOR_EDM_CONFIRMED_DROP_THRESHOLD,
                  (double)MUSIC_MOTOR_EDM_MAJOR_DROP_THRESHOLD, (double)MUSIC_MOTOR_EDM_ACTIVE_SUSTAIN_FLOOR);
    Serial.printf(
        "  Phase timing: buildupMin=%lums armedTimeout=%lums impactConfirm=%lums releaseGrace=%lums refractory=%lums\n",
        (unsigned long)MUSIC_MOTOR_EDM_BUILDUP_MIN_MS, (unsigned long)MUSIC_MOTOR_EDM_ARMED_TIMEOUT_MS,
        (unsigned long)MUSIC_MOTOR_EDM_IMPACT_CONFIRM_MS, (unsigned long)MUSIC_MOTOR_EDM_RELEASE_GRACE_MS,
        (unsigned long)MUSIC_MOTOR_EDM_REFRACTORY_MS);
    Serial.printf(
        "  Drop entry escalation: initial=%u%% escalated=%u%% (after %lums) guaranteed=%u%% (after %lums)   "
        "review every %lums\n",
        (unsigned)MUSIC_MOTOR_EDM_ENTRY_INITIAL_PERCENT, (unsigned)MUSIC_MOTOR_EDM_ENTRY_ESCALATED_PERCENT,
        (unsigned long)MUSIC_MOTOR_EDM_ENTRY_ESCALATE_AFTER_MS, (unsigned)MUSIC_MOTOR_EDM_ENTRY_GUARANTEED_PERCENT,
        (unsigned long)MUSIC_MOTOR_EDM_ENTRY_GUARANTEE_AFTER_MS, (unsigned long)MUSIC_MOTOR_EDM_ENTRY_REVIEW_MS);
    Serial.printf("  Sustained speed floor tiers: NORMAL=M%u PERFORMANCE=M%u PEAK=M%u (max safe=M%u)\n",
                  (unsigned)MUSIC_MOTOR_SUSTAINED_NORMAL_FLOOR_PERCENT, (unsigned)MUSIC_MOTOR_SUSTAINED_PERFORMANCE_FLOOR_PERCENT,
                  (unsigned)MUSIC_MOTOR_SUSTAINED_PEAK_FLOOR_PERCENT, (unsigned)MUSIC_MOTOR_PEAK_MAX_PERCENT);
    Serial.printf("  Bass-energy proxy: single-pole low-pass RMS (NOT true FFT bass) -- impactDeltaThreshold=%.2f\n",
                  (double)MUSIC_MOTOR_BASS_IMPACT_DELTA_THRESHOLD);
  }
  Serial.println(F("  Revision 10 motion palette/duty-cycle/drop-phrase config: see 'musicmotor dynamics status'"));
  Serial.println(F("  Physical calibration (approximate, current mechanical load):"));
  Serial.println(F("    M80:  quarter=1200ms  half=2200ms  full=3000ms"));
  Serial.println(F("    M90:  quarter=500ms   half=1000ms  full=2000ms"));
  Serial.println(F("    M100: quarter=250ms   half=500ms   full=1000ms"));
}
