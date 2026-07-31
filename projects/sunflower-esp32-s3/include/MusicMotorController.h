#pragma once
// MusicMotorController -- music-reactive "dance phrase" motor behavior,
// separate from DanceEngine:
//
//   AudioAnalyzer -> MusicMotorController -> MotorDriver / PWM motor primitives
//
// Revision 2 (first physical test feedback): the original implementation
// swayed and reversed smoothly but (a) did not visibly respond to sustained
// song-intensity changes and (b) reversed far too often instead of
// occasionally committing to one direction long enough to look like a full
// rotation/spin. This revision separates three previously-conflated
// concepts:
//
//   sustained song intensity   -- see fastEnergy/songEnergy/baselineEnergy
//   individual beat/transient events -- see transientDelta/beat/strongHit
//   movement PHRASE selection  -- see MusicMotorBeatAction / EXTENDED_SPIN
//
// Target visual phrase:
//
//   slow sway (M80-M83 -- see MUSIC_MOTOR_ACTIVE_MIN_PERCENT: M80 is the
//     physically-validated minimum reliable movement command)
//     -> song intensity rises -> motor speed rises progressively (M80..M100)
//     -> strong beat -> accent / reversal / hip-shake / reverse hip-shake /
//        extended spin (which one depends on intensity band + timing +
//        bounded counters, never plain randomness)
//     -> occasional committed one-direction spin at HIGH/PEAK intensity,
//        calibrated to the real physical rotation timing (see Config.h)
//     -> smooth deceleration back toward whatever the CURRENT song
//        intensity actually calls for (not always back to M80-M83)
//
// Revision 3 adds: an M80 active-movement floor (no LOW/MEDIUM/HIGH/PEAK/
// accent/reversal/hip-shake/spin action may intentionally command below
// it); a sustained drop-hold choreography-permission signal so HIGH/PEAK
// choreography stays available briefly even if songEnergy dips into MEDIUM
// right after a bass drop; a REVERSE_HIP_SHAKE multi-phase action; and a
// lightweight fastEnergy-delta "wobble" cue for sustained bass-modulation
// sections that don't produce sharp transients.
//
// Reuses AudioAnalyzer's existing AudioFeatures.normalized (already
// noise-floor-subtracted and 0..1 normalized -- see include/AudioAnalyzer.h)
// as the only microphone signal; no second capture pipeline, no FFT/
// frequency-band analysis, no BPM tracking. Beat/strong-hit/intensity-band
// detection is a simple energy-transient approximation -- NOT true
// frequency-isolated bass detection.
//
// Speed is expressed as a 0-100 percent throughout (matching the existing
// "M" scale used by MotorPwmCalibration's 'm20'..'m100' and DanceEngine),
// converted to raw 8-bit PWM duty in exactly one place
// (MusicMotorController.cpp's percentToMotorPwm()) -- never scattered.
//
// Like DanceEngine, this module never calls MotorPowerGuard and never
// reads or writes LED mute state at any point -- no muting, dimming, or
// power-mitigation of any kind. See Config.h's "Music-reactive motor
// movement" section for every tunable constant.
//
// Disabled by default; the user must explicitly send 'musicmotor on'.
// There is no position sensor/encoder anywhere in this project --
// EXTENDED_SPIN is an open-loop, time-based committed rotation, not a
// guaranteed exact 360 degrees; see its own comment below.
//
// Revision 7 adds SUSTAINED_DRIVE: an occasional, weighted (not rigidly
// timed) 5-10 second committed continuous FORWARD/REVERSE hold, mixed into
// the existing choreography at effective MEDIUM/HIGH/PEAK intensity so the
// sunflower reads as "committing to a high-energy dance move" rather than
// reacting to every beat. Layered on top of, not a replacement for, the
// existing hip-shake/reversal/spin/accent/momentum/drop-hold machinery --
// see MusicMotorController.cpp's "Revision 7" section for the original
// design (eligibility + weighted entry roll, direction selection, the
// rotation-commitment/consecutive-direction rules, and exit-via-
// DECELERATING). See Config.h's MUSIC_MOTOR_SUSTAINED_DRIVE_* constants for
// every tunable.
//
// Revision 8 evolves SUSTAINED_DRIVE from a single fixed 5-10s roll into a
// renewable, music-driven PERFORMANCE PHRASE: a minimum directional
// commitment, an initial review point sized by the entry intensity, and
// then repeated EXTEND/SWITCH/EXIT decisions at each review -- capable of
// running well past 30s (even past 60s) while HIGH/PEAK energy keeps
// supporting it, with occasional safe direct FORWARD<->REVERSE switches
// mid-phrase when the music strongly justifies one. It also adds a SHORT
// phrase tier (a brief, intentional 1-5s committed burst, distinct from
// both an ordinary beat pulse and a failed long attempt) that may
// occasionally PROMOTE into a longer phrase. See
// MusicMotorController.cpp's "Revision 8" section
// (computeSustainedDriveContinuationDecision()/chooseSustainedDriveEntryTier()/
// performSustainedDriveExtension()/performSustainedDriveDirectionSwitch())
// and Config.h's own "Revision 8" comments for the full design.
//
// Revision 8 also adds MUSICAL_RAMP_DOWN (below): most genuine musical
// endings now wind the motor down gradually rather than stopping instantly
// -- see its own enum comment and MusicMotorController.cpp's
// beginMusicalSilenceStop(). This NEVER affects real safety stops
// (emergency stop/'k'/disable/hardware faults), which still call
// hardStop() directly, completely bypassing this state.

#include <stdint.h>

enum class MusicMotorState {
  OFF,
  SILENT,             // no meaningful audio (intensity band QUIET)
  INTENSITY_SWAY,     // continuous movement, speed tracks the live song-intensity target (M80-M100)
  BASS_ACCENT,        // transitional: optional reversal + acceleration toward the hip-shake target
  HIP_SHAKE,          // energetic burst near the fast/hip-shake target
  REVERSE_HIP_SHAKE,  // distinct multi-phase opposite/original alternation (see below); NOT a rename of HIP_SHAKE
  EXTENDED_SPIN,      // committed one-direction open-loop rotation (see below)
  SUSTAINED_DRIVE,    // revision 7/8: committed, renewable FORWARD/REVERSE performance phrase (see below)
  DECELERATING,       // smooth ramp back toward the current song-intensity target (phrase/burst exits)
  MUSICAL_RAMP_DOWN,  // revision 8: lifelike graceful wind-down toward a full stop on confirmed silence (see below)
};
// NOTE: "REVERSAL_COAST" is intentionally not a distinct value in this
// enum. The safe coast-before-reversal sequence is a single, shared,
// cross-cutting mechanism (see MusicMotorController.cpp's
// tryRequestReversal()/coastingForReversal) used by every state that can
// reverse -- duplicating it as a top-level state per the suggested list
// would mean re-deriving which state to RETURN to afterward, which is
// exactly what the existing shared mechanism already avoids needing.
// Status/diagnostic output reports "REVERSAL_COAST" as the *visible*
// movement state whenever a reversal is in flight, regardless of which
// real state initiated it -- see reportedStateName() in the .cpp.
//
// MUSICAL_RAMP_DOWN vs. DECELERATING: DECELERATING always ramps toward the
// LIVE, currently-valid intensityTargetPercent (i.e. "ease into whatever
// the music is calling for right now") and hands back to INTENSITY_SWAY --
// it is a PHRASE/BURST-EXIT mechanism, not a stop. MUSICAL_RAMP_DOWN always
// ramps toward EXACTLY ZERO and finishes by actually stopping (the same
// coast+state=SILENT stopCleanly() every other stop path uses) -- it is
// entered only once genuine silence has already been confirmed via the
// existing MUSIC_MOTOR_SILENCE_TIMEOUT_MS hysteresis, as the NORMAL
// (gradual) alternative to an immediate stopCleanly() call. A qualifying
// dramatic musical cutoff may still skip straight to the immediate stop --
// see MusicMotorController.cpp's beginMusicalSilenceStop().
enum class MusicMotorDirection { FORWARD, REVERSE };

// Sustained song-intensity classification (see Config.h's
// MUSIC_MOTOR_LOW_THRESHOLD/_MEDIUM_THRESHOLD/_HIGH_THRESHOLD/_PEAK_THRESHOLD
// and MUSIC_MOTOR_INTENSITY_HYSTERESIS). Derived from `songEnergy` (the
// slow-attack/slow-release sustained signal), NOT the fast/transient one.
enum class MusicIntensityBand { BAND_QUIET, BAND_LOW, BAND_MEDIUM, BAND_HIGH, BAND_PEAK };

// What a detected beat/strong-hit event may do -- selected deterministically
// (bounded modular counters + timing gates, never raw random()) by
// MusicMotorController.cpp's selectBeatAction(). See Config.h and the .cpp
// for the exact per-band decision rules.
enum class MusicMotorBeatAction {
  NONE,
  ACCENT_CURRENT_DIRECTION,
  REVERSE_DIRECTION,
  START_HIP_SHAKE,
  START_REVERSE_HIP_SHAKE,  // regular or heavy -- variant decided at trigger time, see enterReverseHipShake()
  START_EXTENDED_SPIN,
  START_SUSTAINED_DRIVE,  // revision 7 -- direction decided by trySustainedDriveEntry(), consumed by the caller entering it
};

// Resets all state (OFF, no PWM attached). Call once during setup(),
// after initMotor().
void initMusicMotorController();

// Advances the fastEnergy/songEnergy/baselineEnergy smoothing, intensity-
// band classification, beat/strong-hit detection, and the movement-phrase
// state machine. Non-blocking; no delay() anywhere in this module. Must be
// called every loop() iteration -- energy smoothing updates every call,
// while the motor-output/decision tick is internally rate-limited to
// ~MUSIC_MOTOR_TICK_MS (see Config.h).
void updateMusicMotorController(unsigned long now);

// True whenever MusicMotorController currently owns the motor -- i.e.
// enabled (any state except OFF), including while quietly SILENT.
// Consulted by main.cpp's isAnyMotorDiagnosticActive() (which every other
// motor behavior/diagnostic already checks), so nothing else can start
// while this controller is enabled, and it itself refuses to enable while
// anything else owns the motor.
bool isMusicMotorControllerActive();

// --- Serial-facing commands (see Controls.cpp's dispatchCommand()) ---

// 'musicmotor on': enables music-reactive motor movement. Refused if
// another motor diagnostic, MotorPwmCalibration test, or expressive motion
// currently owns the motor. Preempts IDLE_SWAY (setMotorBehavior(OFF)),
// stops DanceEngine cleanly if it is active (a choreographed dance and
// music-reactive movement are never allowed to drive the motor at once),
// and turns off ExpressiveMotionMode::AUDIO_REACTIVE if selected.
void musicMotorEnable();

// 'musicmotor off': coasts the motor immediately, detaches PWM (GPIO8/
// GPIO9 return to plain digital LOW via MotorDriver's deinitMotorPWM()),
// and cancels every pending ramp/hold/reversal/spin. Does not automatically
// resume any previous behavior.
void musicMotorDisable();

// 'musicmotor status': prints full status -- see MusicMotorController.cpp
// for the exact field list.
void musicMotorPrintStatus();

// 'musicmotor intensity': prints just the energy-pipeline values (raw,
// fastEnergy, songEnergy, baselineEnergy, transientDelta), the current
// intensity band, and every threshold -- a lighter, tuning-focused view
// than the full status dump.
void musicMotorPrintIntensity();

// --- Revision 4: detailed decision diagnostics ('musicmotor debug
// on'/'off'/'status') -- see MusicMotorController.cpp for the exact field
// list of each new diagnostic line. Purely additive/informational: does
// NOT change any threshold, EMA, cooldown, or choreography decision.
// Default OFF at boot/enable, matching this project's convention of
// diagnostic features being opt-in (e.g. MotorPwmCalibration, DanceEngine's
// own 'dancetest'). ---
void musicMotorSetDebugLogging(bool enabled);  // 'musicmotor debug on'/'off'
bool musicMotorIsDebugLoggingEnabled();
void musicMotorPrintDebugStatus();  // 'musicmotor debug status'

// 'musicmotor motion': prints the physical-calibration-derived tuning
// surface -- active-movement floor, LOW/MEDIUM/HIGH/PEAK ranges, drop-hold
// durations, wobble threshold/refractory, hip-shake cooldown, regular/heavy
// reverse-hip-shake PWM/timing/phase counts, and spin durations. See
// MusicMotorController.cpp for the exact field list.
void musicMotorPrintMotion();

// Called from main.cpp's serviceEmergencyStop() ('k') and from the manual
// motor-command system's 'mstop' handler (Controls.cpp) -- immediately
// coasts, detaches PWM, and disables. Idempotent/silent no-op if not
// currently enabled. Does NOT automatically re-enable; stays OFF
// until an explicit 'musicmotor on'.
void cancelMusicMotorController();

// --- Temporary physical-tuning commands (see Controls.cpp) -- values do
// not persist through reboot; Config.h's constants remain the defaults
// used on every 'musicmotor on'/reset. ---
void musicMotorSetSlowPercent(uint8_t percent);        // 'musicmotor slow <percent>' -- LOW-band target range
void musicMotorSetFastPercent(uint8_t percent);        // 'musicmotor fast <percent>' -- hip-shake target ceiling
void musicMotorSetStrongHitThreshold(float delta);     // 'musicmotor hitthreshold <value>'
void musicMotorSetBeatThreshold(float delta);          // 'musicmotor beatthreshold <value>'
void musicMotorSetAccelMs(uint32_t ms);                // 'musicmotor accel <ms>'
void musicMotorSetHoldMs(uint32_t ms);                 // 'musicmotor hold <ms>' (fixes both min/max to this value)
void musicMotorSetDecelMs(uint32_t ms);                // 'musicmotor decel <ms>'

void musicMotorSetLowThreshold(float value);           // 'musicmotor lowthreshold <value>'
void musicMotorSetMediumThreshold(float value);        // 'musicmotor mediumthreshold <value>'
void musicMotorSetHighThreshold(float value);          // 'musicmotor highthreshold <value>'
void musicMotorSetPeakThreshold(float value);          // 'musicmotor peakthreshold <value>'

// 'musicmotor spintime <ms>': sets the nominal extended-spin duration
// (fixes both the min/max range to this one value, matching 'hold').
void musicMotorSetSpinTimeMs(uint32_t ms);
// 'musicmotor spincooldown <ms>': minimum gap between two extended spins.
void musicMotorSetSpinCooldownMs(uint32_t ms);
// 'musicmotor rotationhold <ms>': revision 6 "favor continuation over
// interruption" gate -- minimum time committed to the current direction
// before an ORDINARY (non-accent, non-phrase-boundary) beat/strong-hit-
// triggered reversal is allowed. See Config.h's
// MUSIC_MOTOR_MIN_ROTATION_HOLD_MS comment for the full rationale.
void musicMotorSetMinRotationHoldMs(uint32_t ms);

// 'musicmotor spin': manually triggers one safe extended spin for visual
// calibration. Only works while enabled; obeys the same reverse-coast
// protection and spin-cooldown as an automatically-triggered spin; locks
// the CURRENT direction (does not force a reversal); returns to normal
// music-reactive operation (via DECELERATING) once the spin completes.
void musicMotorTriggerSpin();

// --- Revision 9: relative/song-adaptive musical-section (EDM/dubstep
// "drop") recognition -- see Config.h's own "Revision 9" comment block and
// MusicMotorController.cpp's "Revision 9" function section for the full
// design (drop-confidence scoring, the BUILDUP/DROP_ARMED/DROP_IMPACT/
// DROP_ACTIVE/DROP_RELEASE phase state machine, the escalating
// sustained-drive entry opportunity, and the tiered NORMAL/PERFORMANCE/PEAK
// sustained speed floor). ADDITIVE ONLY: layered alongside, and never
// replacing, Revision 7/8's absolute-band sustained-drive machinery. ---

// 'musicmotor dropdetect on'/'off': A/B toggle for the entire relative-drop
// layer, so the SAME firmware image can be compared with it enabled vs.
// disabled (a clean "Revision 8 baseline" run) without a rebuild. Defaults
// to MUSIC_MOTOR_RELATIVE_DROP_ENABLED_DEFAULT; persists across musicmotor
// on/off within a session (only reset to the Config.h default at true
// boot), same treatment as debug logging.
void musicMotorSetRelativeDropEnabled(bool enabled);
bool musicMotorIsRelativeDropEnabled();

// 'musicmotor summary': compact post-song session statistics -- band time
// distribution, strong-hit/DropHold/relative-drop counts, sustained-drive
// entry/extension/switch counts, phrase duration stats, max commanded/
// applied speed, and relative-drop entry rejection counts. Accumulates from
// the most recent 'musicmotor on'; see MusicMotorController.cpp for the
// exact field list.
void musicMotorPrintSummary();

// --- Revision 10: physical choreography and dynamic-range refinement --
// see Config.h's own "Revision 10" comment block and
// MusicMotorController.cpp's "Revision 10" function sections for the full
// design (the speed-authority cap/"bounded lending," the MotionTier
// choreography-role palette, QUIET_BUILDUP/MELLOW movement duty-cycle
// pulses, and the FULL_SUSTAIN/SUSTAINED_REVERSAL/DROP_BOOTY_SHAKE/
// DROP_PUNCH_AND_HOLD/DOUBLE_PUNCH/SUSTAIN_WITH_ACCENTS drop choreography
// phrase vocabulary). Layered alongside, never weakening, Revision 9's
// drop DETECTION. ---

// 'musicmotor quietmotion on'/'off': toggles QUIET_BUILDUP motion
// specifically (independent of 'musicmotor dropdetect', which gates ALL
// relative-drop machinery). Persists across musicmotor on/off, same
// treatment as debug logging/dropdetect.
void musicMotorSetQuietBuildupMotionEnabled(bool enabled);
bool musicMotorIsQuietBuildupMotionEnabled();

// 'musicmotor switchchance <0-100>': mid-drop phrase-reselection roll
// chance (see maybeTriggerDropPhraseReselection() in the .cpp) -- how
// often a qualifying reversal/reselection cue is actually acted on.
void musicMotorSetSwitchChancePercent(uint8_t percent);
// 'musicmotor switchcooldown <ms>': cooldown after a booty-shake/punch
// choreography sequence completes before another non-FULL_SUSTAIN
// sequence may begin.
void musicMotorSetSwitchCooldownMs(uint32_t ms);
// 'musicmotor switchlimit <count>': maximum committed sustained
// reversals per drop (an unusually long drop still earns one extra
// allowance beyond this -- see Config.h's
// MUSIC_MOTOR_DROP_PHRASE_LONG_DROP_EXTRA_REVERSAL_MS comment).
void musicMotorSetSwitchLimit(uint8_t count);

// 'musicmotor dynamics status': Revision 10 config surface -- speed-
// authority cap, motion palette, duty-cycle timing, drop-entry escalation,
// and drop-phrase vocabulary limits. See MusicMotorController.cpp for the
// exact field list.
void musicMotorPrintDynamicsStatus();

// --- Revision 10.1: frozen-state regression fix + one-command physical
// test mode -- see Config.h's and MusicMotorController.cpp's own
// "Revision 10.1" comments for the full design (the direction-change
// comparison bug this fixes, the SUSTAINED_DRIVE invariant/recovery layer,
// and the quiet-buildup-vs-SILENT wake fix). ---

// 'musicmotor test': one-command routine physical-test setup -- enables
// MusicMotor (through the same safe musicMotorEnable() path 'musicmotor
// on' always uses, clearing any stale phrase/sustained-drive/emergency-stop
// state), enables relative drop detection + debug logging + quiet-buildup
// motion, resets summary counters, and prints a confirmation plus the
// current dynamics configuration.
void musicMotorEnterTestMode();
// 'musicmotor test stop': prints the final session summary, disables
// verbose test logging, and safely stops via musicMotorDisable().
void musicMotorExitTestMode();
