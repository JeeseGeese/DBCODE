#pragma once

// Centralized tunables for the sunflower LED/audio firmware. Grouped by
// subsystem; each nontrivial constant documents what raising/lowering it
// does so tuning doesn't require reading the implementation.

#include <Arduino.h>

// ============================================================================
// Firmware identity -- printed once at startup (see main.cpp's setup()) and
// exposed via 'musicmotor status'/'musicmotor summary' so a captured serial
// log can always be matched back to the exact firmware that produced it.
// This exists because a physical-test log was once mistaken for a
// then-current build when it actually predated several revisions -- see
// MusicMotorController.cpp's Revision 9 section. These two defines are the
// SINGLE SOURCE OF TRUTH for the firmware's overall identity -- every
// other reference (boot banner, 'musicmotor status', 'musicmotor summary')
// reads these macros rather than duplicating a literal string, so bumping
// them here is sufficient; nothing else needs to change. Bump
// FIRMWARE_REVISION_TAG any time MusicMotorController behavior changes in
// a way worth distinguishing in a future log. (Per-feature "this shipped
// in Revision N" labels elsewhere, e.g. the relative-drop-detection
// diagnostics, are deliberately left as their own historical revision
// number -- they describe when that specific feature was introduced, which
// does not change as the overall build tag advances.)
// ============================================================================
#define FIRMWARE_REVISION_TAG "sunflower-esp32-s3 rev10.1-expressive-motion"
#define FIRMWARE_MUSIC_MOTOR_FEATURE_REVISION "MusicMotorController rev10.1"
// __DATE__/__TIME__ are compiler-supplied at build time (e.g. "Jul 30 2026",
// "14:32:01") -- a cheap, always-correct build identifier with no extra
// build-system plumbing required.
#define FIRMWARE_BUILD_IDENTIFIER (__DATE__ " " __TIME__)

// ============================================================================
// Hardware pin map (verified)
// ============================================================================
#define LED_PIN 4
// CORRECTED 2026-08-08 (Sunny V1.1 LED-count audit): Sunny physically has
// 36 WS2812-compatible LEDs, confirmed by direct physical count. This
// firmware previously carried NUM_LEDS=58 (a V1-era value; see docs/V1/ for
// the frozen historical record of what the firmware reported at that tag)
// and, before that, a documented-but-unconfirmed "42-LED assembly" theory
// (see the retired LED_ROW_1/2/3 block below) -- neither was an
// independently-verified physical count. 36 is the current, physically
// confirmed value; NUM_LEDS is the single source of truth every render
// loop, buffer, and the power estimator already read from (see
// docs/current/LED_ENGINE.md and docs/current/SPEAKER.md's sibling lesson
// in docs/lessons/ for why raising NUM_LEDS above the true physical count
// is harmless but UNDER-counting the power estimate is not -- see
// applyPowerLimit() in main.cpp).
#define NUM_LEDS 36

// ============================================================================
// LED row/region metadata for the 'ledmap' ('6') diagnostic tool
// ============================================================================
// Previously described a "42-LED assembly (3 rows: 10+10+22)" theory that
// predates the 2026-08-08 physical LED-count audit above and is now known
// to be inconsistent with the confirmed 36-LED count (10+10+22=42 != 36).
// That row breakdown was never independently verified in the first place
// (see git history for the original wording) -- it was always metadata for
// a human to visually confirm via the 'ledmap' tool, not a verified fact.
// PHYSICAL_LED_COUNT is retired as a concept distinct from NUM_LEDS: the
// physical count is now confirmed to equal NUM_LEDS exactly (36), so there
// is no longer a "logical vs. physical" gap to track. The three-row split
// below is left ONLY as an unverified placeholder so the 'ledmap' tool's
// existing per-row dim-check still compiles and runs -- it is NOT a
// confirmed physical row layout. Re-derive the real boundaries by running
// 'ledmap' and watching where the lit LEDs actually fall; update this block
// once that's done, rather than trusting these numbers.
struct LedRegion {
  uint16_t start;
  uint16_t count;
};

// UNVERIFIED PLACEHOLDER split (equal thirds of 36) -- see comment above.
constexpr LedRegion LED_ROW_1{0, 12};
constexpr LedRegion LED_ROW_2{12, 12};
constexpr LedRegion LED_ROW_3{24, 12};
constexpr uint16_t PHYSICAL_LED_COUNT = 36;

static_assert(LED_ROW_1.start + LED_ROW_1.count == LED_ROW_2.start, "Row 1 must end exactly where Row 2 begins");
static_assert(LED_ROW_2.start + LED_ROW_2.count == LED_ROW_3.start, "Row 2 must end exactly where Row 3 begins");
static_assert(LED_ROW_3.start + LED_ROW_3.count == PHYSICAL_LED_COUNT, "Row 3 must end exactly at PHYSICAL_LED_COUNT");
static_assert(PHYSICAL_LED_COUNT <= NUM_LEDS,
              "PHYSICAL_LED_COUNT must fit within NUM_LEDS -- row indices address the existing strip object");
static_assert(PHYSICAL_LED_COUNT == NUM_LEDS,
              "As of the 2026-08-08 correction, the physical LED count is confirmed to equal NUM_LEDS exactly");

#define BUTTON_MODE_PIN 10
#define BUTTON_MUTE_PIN 11
#define BUTTON_BRIGHTNESS_PIN 17
#define BUTTON4_PIN 5

// Single shared full-duplex I2S bus (see include/SharedI2S.h) -- ONE port,
// ONE i2s_driver_install() call, serving both the INMP441 microphone (RX)
// and the MAX98357A amplifier (TX) simultaneously. A prior two-controller
// design (I2S_NUM_0 master RX + I2S_NUM_1 slave TX sharing BCLK/WS) was
// tried and conclusively failed -- i2s_write() on the slave TX port never
// obtained DMA space at any bounded wait, and an unbounded wait froze the
// whole application. Full duplex on a single master port sidesteps that
// entirely: there is only one clock domain, generated once, for both
// directions. See docs/... and SharedI2S.cpp's initSharedI2S() for the
// full rationale.
#define I2S_PORT I2S_NUM_0
#define I2S_BCLK_PIN 6
#define I2S_WS_PIN 7
#define I2S_DIN_PIN 15         // INMP441 SD (mic data into the ESP32)
#define I2S_SPEAKER_DOUT_PIN 16  // MAX98357A DIN (speaker data out of the ESP32)
#define I2S_SAMPLE_RATE 16000

// Which 32-bit slot of each RIGHT_LEFT stereo RX frame carries the
// INMP441's active sample (its L/R pin is tied to GND). Standard I2S
// framing begins each frame with WS LOW = left channel, so the first word
// of each captured pair is expected to be LEFT -- empirically confirmed
// against real hardware in AudioAnalyzer.cpp's boot-time RX trace (see
// docs/.../ and the write-path report) rather than assumed; update this
// constant (and the trace) if real captured evidence ever shows otherwise.
constexpr int MIC_I2S_SLOT_INDEX = 0;  // 0 = first word in each RX pair, 1 = second

// Number of captureSamples() calls, starting from initAudioAnalyzer(), to
// trace in detail (raw stereo frame hex + extracted samples) -- temporary,
// see AudioAnalyzer.cpp's captureSamples().
constexpr int MIC_RX_TRACE_CALL_COUNT = 5;

// ============================================================================
// Button timing
// ============================================================================
#define DEBOUNCE_DELAY_MS 40

// Button4-specific debounce, separate from DEBOUNCE_DELAY_MS (which the
// other three buttons still use). Raised from 40ms after physical testing
// suggested Button4 needed more settling margin; scoped to this one
// button so Mode/Mute/Brightness timing is untouched.
constexpr uint32_t BUTTON4_DEBOUNCE_MS = 75;

// Button4 dual-purpose threshold (see Controls.cpp's handleButton4()):
// short press (release before this elapses) toggles the audio-reactive
// LED overlay, unchanged from the original single-purpose design; a
// press held at least this long instead toggles expressive audio-reactive
// motor movement (see include/ExpressiveMotion.h) once, on threshold
// crossing -- not on release, so holding longer never fires it twice.
// No previous hold/long-press constant existed for Button4 to preserve;
// 900ms was chosen as a comfortably-distinguishable middle of the
// requested 800-1200ms range.
constexpr uint32_t BUTTON4_LONG_PRESS_MS = 900;

// A press within this window of the previous press is treated as the second
// half of a double-press instead of two independent single-presses. Larger
// = more forgiving of slow double-clicks, but adds that much latency before
// a genuine single click fires its action.
#define DOUBLE_PRESS_WINDOW_MS 350

// ============================================================================
// Frame timing
// ============================================================================
// Target interval between composed LED frames (base effect + overlay +
// power limiting + strip.show()). Mic sampling and button polling are NOT
// gated by this -- they run every loop() iteration regardless. Lower =
// smoother animation but more CPU/bus time in strip.show(); higher = less
// load but choppier motion. 16-25ms (40-60 FPS) is the requested range.
#define FRAME_INTERVAL_MS 20

// ============================================================================
// Brightness levels
// ============================================================================
// Perceptual step table: roughly doubling-feel steps rather than linear,
// so low settings are genuinely dim and usable, not just "slightly less
// bright". raw = round(percent * 255 / 100). Index 0 is the boot default.
constexpr uint8_t BRIGHTNESS_PERCENTS[] = {3, 7, 12, 20, 32, 48, 68, 85, 100};
constexpr uint8_t BRIGHTNESS_RAW[] = {8, 18, 31, 51, 82, 122, 173, 217, 255};
constexpr uint8_t NUM_BRIGHTNESS_LEVELS =
    sizeof(BRIGHTNESS_PERCENTS) / sizeof(BRIGHTNESS_PERCENTS[0]);
static_assert(sizeof(BRIGHTNESS_RAW) == sizeof(BRIGHTNESS_PERCENTS),
              "brightness tables must be the same length");
#define DEFAULT_BRIGHTNESS_INDEX 3 // 20% -- comparable to the old default (20/255 = ~8%), a bit brighter but still conservative

// ============================================================================
// Power-aware output limiting (Phase 7)
// ============================================================================
// This is a software ESTIMATE, not a substitute for correct electrical
// power design (adequate supply sizing, wiring gauge, fusing). It scales
// the composed frame down when the estimated draw would exceed the limit;
// it cannot protect against a supply that's simply undersized for the
// LEDs' physical maximum.
//
// Estimation model: each WS2812-class LED draws roughly LED_IDLE_MA_PER_LED
// at rest (controller quiescent current) plus up to LED_MAX_MA_PER_CHANNEL
// per color channel at full (255) drive, scaling linearly with channel
// value. This is a standard conservative approximation, not a per-LED
// current measurement.
constexpr uint16_t LED_CURRENT_LIMIT_MA = 1000; // raise if your supply is known to handle more; lower for USB-only bring-up
constexpr uint8_t LED_MAX_MA_PER_CHANNEL = 20;  // typical WS2812 full-drive per-channel current
constexpr uint8_t LED_IDLE_MA_PER_LED = 1;      // controller quiescent current, all LEDs, any color
#define POWER_WARNING_INTERVAL_MS 2000 // minimum gap between repeated [POWER] throttle warnings

// ============================================================================
// Visual cue (overlay enable/disable flash)
// ============================================================================
// Deliberately independent of the user-selected brightness -- always safe
// even if the user is currently at 100%. Raised from an earlier 30%
// (raw 77) after physical testing showed that cap, combined with
// desaturated cue colors, was too dim/muddy to read clearly on real
// hardware. 115 is exact, not formula-derived, per hardware validation.
constexpr uint8_t VISUAL_CUE_BRIGHTNESS_RAW = 115; // ~45%

// ============================================================================
// Audio: capture / diagnostics
// ============================================================================
#define MIC_PRINT_INTERVAL_MS 120        // ~8Hz analysis window; also the AudioFeatures update rate
#define AUDIO_DIAG_PRINT_INTERVAL_MS 2000 // default low-rate heartbeat print, so mic health is visible without flooding serial; 'd'/status give full detail on demand
#define MIC_ZERO_BYTE_WARN_THRESHOLD 50  // consecutive empty i2s_read() calls before warning
#define MIC_STUCK_WARN_MS 3000           // how long "stuck" must persist before warning
#define MIC_SATURATION_THRESHOLD 8300000 // near max of the 24-bit signed sample range

// ============================================================================
// Audio: normalization / envelope tuning
// ============================================================================
// Hardware-observed RMS ranges on this exact mic/board (unchanged from the
// verified AUDIO_PULSE defaults): quiet ~8k-20k, taps ~19k-23k, speech
// ~40k-360k+, claps ~100k-900k+ (often 500k+ for a sharp clap specifically).
// These are preserved as defaults per instructions -- NOT re-tuned here.
#define AUDIO_NOISE_FLOOR 20000.0f      // starting point for the adaptive floor below
#define AUDIO_MAX_RMS 200000.0f         // normalization ceiling; loud speech/music reaches ~1.0 here
#define AUDIO_CLAP_THRESHOLD 400000.0f  // above loud speech (~360k), below typical clap RMS (~500k+)
#define AUDIO_ATTACK_SMOOTHING 0.6f     // fast rise: speech/claps register almost immediately. Higher = snappier, more jittery
#define AUDIO_RELEASE_SMOOTHING 0.08f   // slow fall: avoids visible flicker between samples. Higher = faster decay, more responsive but twitchier
#define AUDIO_CLAP_DECAY 0.85f          // per-frame decay of the clap flash (~150-200ms to fade). Lower = faster fade

// Slow automatic ambient-noise adaptation. The floor drifts toward the
// current RMS only while things are already quiet (see AudioAnalyzer.cpp),
// so a sustained loud passage of music cannot itself become "the new
// silence" -- it's bounded to [AUDIO_NOISE_FLOOR_MIN, AUDIO_NOISE_FLOOR_MAX].
#define AUDIO_NOISE_FLOOR_ADAPT_RATE 0.0008f // fraction of the gap closed per ~120ms window. Higher = adapts faster (risk: chases real quiet passages of music)
#define AUDIO_NOISE_FLOOR_MIN (AUDIO_NOISE_FLOOR * 0.5f)
#define AUDIO_NOISE_FLOOR_MAX (AUDIO_NOISE_FLOOR * 2.5f)
#define AUDIO_NOISE_FLOOR_ADAPT_MARGIN 1.2f // only adapt while rms < floor * this margin (i.e. "currently quiet")

// Transient = envelope rising faster than this per second. Independent of
// the absolute clap threshold, so quieter sharp sounds (taps, consonants)
// can still register as transients. Higher = only sharper attacks count.
#define AUDIO_TRANSIENT_RISE_THRESHOLD 1.8f // normalized units/second

// Cooldowns prevent a single sustained loud passage from re-firing an
// event every analysis window. Higher = fewer, more separated events.
#define AUDIO_CLAP_COOLDOWN_MS 250
#define AUDIO_LIGHTNING_COOLDOWN_MS 900

// ============================================================================
// Audio: low-frequency ("bass") proxy
// ============================================================================
// NOT a real band-pass filter or FFT bin -- this is a single-pole IIR
// low-pass applied to the same DC-corrected sample stream used for the
// main RMS, documented explicitly per instructions rather than presented
// as accurate frequency-selective analysis. Good enough to distinguish
// "energy skews low" from "energy skews high" for a visual bloom; not
// good enough for beat-detection-grade bass extraction.
//
// Alpha for a ~200Hz one-pole cutoff at a 16kHz sample rate:
// alpha = 2*pi*fc/fs = 2*pi*200/16000 ~= 0.0785
#define AUDIO_BASS_LP_ALPHA 0.0785f
// These are NOT hardware-calibrated (unlike the main RMS constants above)
// -- they're a starting estimate and will likely need adjustment once
// tested against real material. See README tuning guide.
#define AUDIO_BASS_NOISE_FLOOR 4000.0f
#define AUDIO_BASS_MAX_RMS 60000.0f

// ============================================================================
// Audio visual-control derivation (AudioVisualState) -- Phase "richer overlays"
// ============================================================================
// NOT real frequency bands -- this project has no FFT/filter bank. `bass`
// reuses the existing single-pole low-pass proxy (AUDIO_BASS_* above);
// `highRange`/`transient` is a decaying "spike" signal seeded by
// transient/clap edges; `midRange` is the broadband envelope with the
// low/high contributions subtracted out. See AudioVisualState.h and the
// README "Audio controls" section for the full explanation.
#define AUDIO_VISUAL_TRANSIENT_DECAY_PER_SEC 3.0f // linear decay rate of the derived "high" spike; ~333ms to fully decay
#define AUDIO_VISUAL_MID_BASS_WEIGHT 0.5f   // how much of `bass` is subtracted out of level to derive midRange
#define AUDIO_VISUAL_MID_HIGH_WEIGHT 0.3f   // how much of the high spike is subtracted out of level to derive midRange

// ============================================================================
// Audio overlay tuning
// ============================================================================
#define AUDIO_PULSE_GAIN 1.6f          // how much louder audio boosts base-effect intensity. Higher = more dramatic pulsing
#define AUDIO_PULSE_CLAP_BOOST 0.9f    // additional brief boost added on clap, on top of the envelope-driven pulse
#define PULSE_MIN_WIDTH_LEDS 2.0f       // pulse ring width floor, in LEDs, at silence
#define PULSE_BASS_WIDTH_GAIN 0.6f      // how much bass thickens the pulse ring
#define PULSE_CLAP_WAVE_SPEED_LEDS_PER_SEC 90.0f
#define PULSE_CLAP_WAVE_WIDTH_LEDS 4.0f
#define PULSE_CLAP_WAVE_LIFETIME_MS 500

#define RIPPLE_MAX_COUNT 6             // simultaneous ripples; fixed-size, no heap allocation
#define RIPPLE_SPEED_LEDS_PER_SEC 40.0f
#define RIPPLE_WIDTH_LEDS 5.0f
#define RIPPLE_LIFETIME_MS 900
#define RIPPLE_SPEECH_INTENSITY 0.5f   // relative size of a ripple spawned by a plain transient
#define RIPPLE_CLAP_INTENSITY 1.0f     // relative size of a ripple spawned by a clap
#define RIPPLE_BASS_SPEED_SCALE 0.55f   // bass slows + widens ripples toward this fraction of normal speed
#define RIPPLE_BASS_WIDTH_SCALE 1.8f    // bass widens ripples up to this multiple
#define RIPPLE_TRANSIENT_SPEED_SCALE 1.6f // transients speed ripples up to this multiple
#define RIPPLE_AMBIENT_SPAWN_CHANCE_PER_FRAME 0.02f // small chance of an ambient ripple even without a transient, scaled by envelope

#define SPARK_MAX_COUNT 10
#define SPARK_LIFETIME_MS 220
#define SPARK_MAX_SPAWN_CHANCE_PER_FRAME 0.35f // spawn probability at envelope==1.0; scales down to ~0 in quiet
#define SPARK_BASS_EMBER_CHANCE 0.5f    // fraction of bass-triggered spawns that become larger, slower "embers"
#define SPARK_EMBER_LIFETIME_MS 500
#define SPARK_CLAP_BURST_COUNT 5        // sparks spawned across the strip on a clap burst

#define LIGHTNING_FLASH_FRAMES 4       // frames in the bright-dim-bright-dim flash pattern
#define LIGHTNING_MAX_INTENSITY 0.75f  // caps peak flash brightness (fraction of full channel range) so it stays comfortable to look at
#define LIGHTNING_MAX_BOLTS 3           // simultaneous bolt segments, fixed-size pool
#define LIGHTNING_BOLT_SUBFRAME_MS 55
#define LIGHTNING_STORM_GLOW_GAIN 0.25f // how much sustained bass adds a low background glow between strikes

#define BASS_BLOOM_GAIN 1.4f           // how strongly lowFrequencyEnergy expands/brightens the central bloom
#define BASS_BLOOM_MIRROR_THRESHOLD 0.55f // bass level above which a second mirrored bloom anchor activates
#define BASS_BLOOM_DECAY_PER_SEC 1.2f    // how fast the trailing bloom color fades once bass drops
#define BASS_BLOOM_OUTLINE_TRANSIENT_GAIN 0.6f // transient-driven bright outline strength around the bloom edge

#define SPECTRUM_WAVE_HUE_SPEED_DEG_PER_SEC 40.0f
#define SPECTRUM_WAVE_BASE_AMPLITUDE 0.35f
#define SPECTRUM_WAVE_LEVEL_AMPLITUDE_GAIN 0.65f

#define COLOR_FLOOD_CLAP_FADE_MS 600

#define COMET_MAX_COUNT 6               // simultaneous comets; fixed-size, no heap allocation
#define COMET_BASE_SPEED_LEDS_PER_SEC 25.0f
#define COMET_TRANSIENT_SPEED_SCALE 2.2f
#define COMET_BASS_SPEED_SCALE 0.5f
#define COMET_TAIL_LEDS 6.0f

// ============================================================================
// Base effect speeds (larger = slower for *_PERIOD_MS values)
// ============================================================================
#define PETAL_BREATHE_PERIOD_MS 4200
#define PETAL_BREATHE_HUE_DRIFT_PERIOD_MS 26000
#define COLOR_WAVE_PERIOD_MS 2600
#define SUNSET_SPIN_PERIOD_MS 9000
#define RAINBOW_FLOW_PERIOD_MS 3400
#define RAINBOW_FLOW_SECONDARY_PERIOD_MS 5200
#define SPARKLE_BLOOM_SPAWN_CHANCE_PER_FRAME 0.10f
#define SPARKLE_BLOOM_LIFETIME_MS 1400
#define FIREFLY_COUNT 6
#define FIREFLY_LIFETIME_MS 3200
#define AURORA_BAND_PERIOD_MS 7000
#define SOLAR_FLARE_FLARE_PERIOD_MS 3800
#define SOLAR_FLARE_FLARE_SPEED_LEDS_PER_SEC 60.0f

// ============================================================================
// AUTO_SHOWCASE (automatic base-effect cycling)
// ============================================================================
constexpr uint32_t AUTO_SHOWCASE_EFFECT_DURATION_MS = 15000; // how long each real effect runs before advancing
constexpr uint32_t AUTO_SHOWCASE_TRANSITION_MS = 1500;       // crossfade duration between effects

// ============================================================================
// Expressive motion (see include/ExpressiveMotion.h and
// docs/EXPRESSIVE_MOTION_DEVELOPMENT.md) -- development-branch feature,
// disabled by default (ExpressiveMotionMode::OFF at boot). All timings
// below are STARTING VALUES for physical tuning against the actual
// mechanism/belt, not immutable requirements -- expect to revisit every
// range in this section after the physical validation checklist in
// docs/EXPRESSIVE_MOTION_DEVELOPMENT.md.
// ============================================================================

// --- Pulse duration tiers, shared by every movement pattern (see
// ExpressiveMotion.cpp's pattern step tables) -- a pattern is built from
// these tiers rather than each pattern hardcoding its own timing, so
// tuning "how strong is a dramatic pulse" in one place retunes every
// pattern that uses that tier. TUNE FIRST: these three ranges are the
// single biggest lever on how dramatic movement feels overall.
constexpr uint32_t MOTION_GENTLE_PULSE_MIN_MS = 120;
constexpr uint32_t MOTION_GENTLE_PULSE_MAX_MS = 220;
constexpr uint32_t MOTION_MEDIUM_PULSE_MIN_MS = 220;
constexpr uint32_t MOTION_MEDIUM_PULSE_MAX_MS = 380;
constexpr uint32_t MOTION_DRAMATIC_PULSE_MIN_MS = 380;
constexpr uint32_t MOTION_DRAMATIC_PULSE_MAX_MS = 550;  // still well under the 2000ms max-energized safeguard
// Pause between two grouped pulses within one pattern (e.g. the gap in a
// double twitch) -- deliberately shorter than a full idle rest.
constexpr uint32_t MOTION_MICRO_PAUSE_MIN_MS = 100;
constexpr uint32_t MOTION_MICRO_PAUSE_MAX_MS = 220;
// Longer settle pause used only by DRAMATIC_SWEEP's trailing "extended
// rest" step, before it releases -- distinct from the micro-pause above.
constexpr uint32_t MOTION_EXTENDED_PAUSE_MIN_MS = 400;
constexpr uint32_t MOTION_EXTENDED_PAUSE_MAX_MS = 700;

// Defensive backstop: no single continuous energized segment may exceed
// this, regardless of what a pattern's rolled step duration works out to.
// Mirrors the same 2000ms ceiling used elsewhere in this codebase (see
// main.cpp's MOTOR BREAKAWAY TEST) -- should never actually trigger given
// the tiers above, but guards against a future misconfiguration.
constexpr uint32_t MOTION_MAX_ENERGIZED_MS = 2000;

constexpr uint8_t MOTION_MAX_CONSECUTIVE_SAME_DIR = 2; // never more than this many same-direction pulses in a row

// --- IDLE_ALIVE: weighted randomized idle pattern selection (see
// ExpressiveMotion.cpp's pickWeightedIdlePattern()) -- must sum to 1.0.
// TUNE: raise DRAMATIC_SWEEP/EXCITED_TRIPLE for a livelier flower, raise
// GENTLE_SWAY for a calmer one.
constexpr float MOTION_WEIGHT_GENTLE_SWAY = 0.25f;
constexpr float MOTION_WEIGHT_MEDIUM_SWAY = 0.20f;
constexpr float MOTION_WEIGHT_LONG_LEAN = 0.15f;
constexpr float MOTION_WEIGHT_DOUBLE_TWITCH = 0.15f;
constexpr float MOTION_WEIGHT_FORWARD_REVERSE_NOD = 0.10f;
constexpr float MOTION_WEIGHT_EXCITED_TRIPLE = 0.08f;
constexpr float MOTION_WEIGHT_DRAMATIC_SWEEP = 0.07f;

constexpr uint32_t MOTION_REST_MIN_MS = 600;          // minimum rest between movements
constexpr uint32_t MOTION_REST_MAX_MS = 2200;         // typical maximum rest between movements
constexpr uint32_t MOTION_LONG_REST_MIN_MS = 2500;    // occasional longer pause, lower bound
constexpr uint32_t MOTION_LONG_REST_MAX_MS = 5000;    // occasional longer pause, upper bound
constexpr float MOTION_LONG_REST_CHANCE = 0.15f;      // fraction of idle cycles that use the long-rest range instead

// A slow, minimal SETTLE movement instead of the normal weighted pick,
// used only within MOTION_SETTLE_RECENT_ACTIVITY_MS of the last audio
// trigger (QUIET after recent conversation/sound, not QUIET all along).
constexpr float MOTION_SETTLE_CHANCE = 0.12f;
constexpr uint32_t MOTION_SETTLE_RECENT_ACTIVITY_MS = 5000;

// --- AUDIO_REACTIVE: activity bands (hysteresis) + cooldowns ---
// Based on AudioFeatures.envelope (already attack/release-smoothed, 0..1)
// -- never raw per-sample audio. Enter/exit thresholds differ so the band
// doesn't chatter right at a boundary.
constexpr float MOTION_AUDIO_ACTIVE_ENTER = 0.20f;
constexpr float MOTION_AUDIO_ACTIVE_EXIT = 0.12f;
constexpr float MOTION_AUDIO_STRONG_ENTER = 0.55f;
constexpr float MOTION_AUDIO_STRONG_EXIT = 0.40f;
constexpr uint32_t MOTION_AUDIO_ACTIVE_COOLDOWN_MS = 700;   // min gap between ordinary ACTIVE reactions
constexpr uint32_t MOTION_AUDIO_STRONG_COOLDOWN_MS = 1400;  // min gap between STRONG reactions
// AudioFeatures.clap is already edge-triggered and cooldown-gated inside
// AudioAnalyzer.cpp (AUDIO_CLAP_COOLDOWN_MS=250ms there) -- this is a
// SEPARATE, slightly longer cooldown scoped to how often a clap may
// additionally trigger a *motor* reaction, independent of the STRONG band
// cooldown above (a clap can fire its own recoil even if a STRONG
// band-rising reaction just cooled down, and vice versa).
constexpr uint32_t MOTION_AUDIO_CLAP_COOLDOWN_MS = 800;

// Chance an ACTIVE reaction uses the two-pulse FORWARD_REVERSE_NOD
// instead of a single AUDIO_ACTIVE_PULSE -- "one medium pulse or a
// two-pulse conversational nod".
constexpr float MOTION_ACTIVE_NOD_CHANCE = 0.4f;

// --- Speech dynamics: occasional grouped movement during a burst of
// ACTIVE events (e.g. sustained speech), instead of identical single
// pulses every time. Fixed-size counter + timestamp only -- no queue, no
// heap allocation, bounded by MOTION_SPEECH_WINDOW_MS resetting the count.
constexpr uint32_t MOTION_SPEECH_WINDOW_MS = 4000;       // bounded window for counting recent ACTIVE events
constexpr uint8_t MOTION_SPEECH_GROUP_THRESHOLD = 3;     // ACTIVE events within the window before grouping is eligible
constexpr float MOTION_SPEECH_GROUP_CHANCE = 0.35f;      // chance of actually using a grouped pattern once eligible

// --- motion demo: pause between each demonstrated pattern (distinct from
// the intra-pattern micro-pause above) -- a clearly visible "safe stop
// interval" so each pattern in the demo reads as a separate demonstration.
constexpr uint32_t MOTION_DEMO_INTER_PATTERN_PAUSE_MIN_MS = 400;
constexpr uint32_t MOTION_DEMO_INTER_PATTERN_PAUSE_MAX_MS = 600;

// --- Motion-motion LED brightness: reuses MotorPowerGuard's existing
// DIM_DURING_MOTION test-level selection (see MotorPowerGuard.h) rather
// than a separate constant -- "begin conservatively at the already
// validated selected motion brightness" per the development plan.

// ============================================================================
// Behavior Engine (see include/BehaviorEngine.h and
// docs/BEHAVIOR_ENGINE_DEVELOPMENT.md) -- development-branch feature,
// disabled by default (BehaviorState::MANUAL at boot). Drives movement
// exclusively through ExpressiveMotion's requestExpressivePattern() on its
// own schedule (see that function's comment) -- these intervals are
// independent of, and not shared with, the MOTION_* idle-timer constants
// above. All timings below are STARTING VALUES for physical/personality
// tuning, not immutable requirements.
// ============================================================================

// CURIOUS: fairly frequent small investigative movements with occasional
// longer observation pauses.
constexpr uint32_t BEHAVIOR_CURIOUS_ACTION_MIN_MS = 1500;
constexpr uint32_t BEHAVIOR_CURIOUS_ACTION_MAX_MS = 4000;

// LISTENING: begins with one immediate gentle lean/nod (see
// BehaviorEngine.cpp's setBehaviorState()), then a long, conservative
// interval between occasional small nods -- "mostly still".
constexpr uint32_t BEHAVIOR_LISTENING_NOD_MIN_MS = 4000;
constexpr uint32_t BEHAVIOR_LISTENING_NOD_MAX_MS = 9000;
// A recent AudioFeatures.clap during LISTENING pulls the next nod's
// deadline in to no more than this many ms away (never fires instantly) --
// simple, bounded "activity awareness" without a second mic reader/pipeline.
constexpr uint32_t BEHAVIOR_LISTENING_CLAP_NUDGE_MS = 500;

// THINKING: slow, sparse, gentle movement; long pauses.
constexpr uint32_t BEHAVIOR_THINKING_ACTION_MIN_MS = 3000;
constexpr uint32_t BEHAVIOR_THINKING_ACTION_MAX_MS = 7000;

// EXCITED: a finite, energetic episode -- frequent lively movement for a
// bounded duration, then automatic return to IDLE. The action interval is
// deliberately several times longer than any single pattern's own duration
// (all well under MOTION_MAX_ENERGIZED_MS), so a genuine rest/"recovery" gap
// always separates one excited movement from the next.
constexpr uint32_t BEHAVIOR_EXCITED_ACTION_MIN_MS = 900;
constexpr uint32_t BEHAVIOR_EXCITED_ACTION_MAX_MS = 2200;
constexpr uint32_t BEHAVIOR_EXCITED_EPISODE_MIN_MS = 6000;
constexpr uint32_t BEHAVIOR_EXCITED_EPISODE_MAX_MS = 12000;

// If requestExpressivePattern() is refused (ExpressiveMotion busy finishing
// a previous pattern, or a diagnostic started concurrently), retry this
// much sooner than a full action interval rather than waiting out the whole
// randomized gap again.
constexpr uint32_t BEHAVIOR_MOVEMENT_RETRY_MS = 300;

// --- 'behavior demo': fixed dwell time in each state, in sequence
// (IDLE -> CURIOUS -> LISTENING -> THINKING -> EXCITED -> SLEEPING), before
// returning to MANUAL. Chosen so the total (35s) sits comfortably inside
// the requested 25-45s range while still letting each state's movement
// character read clearly. Cancelable at any point via 'k'.
constexpr uint32_t BEHAVIOR_DEMO_IDLE_MS = 5000;
constexpr uint32_t BEHAVIOR_DEMO_CURIOUS_MS = 6000;
constexpr uint32_t BEHAVIOR_DEMO_LISTENING_MS = 6000;
constexpr uint32_t BEHAVIOR_DEMO_THINKING_MS = 6000;
constexpr uint32_t BEHAVIOR_DEMO_EXCITED_MS = 8000;
constexpr uint32_t BEHAVIOR_DEMO_SLEEPING_MS = 4000;

// ============================================================================
// Dance Engine (see include/DanceEngine.h and src/DanceEngine.cpp) --
// microphone-driven PWM dancing, built on the physically-validated PWM
// range/kick/coast behavior from MotorPwmCalibration. Disabled by default
// (DanceState::DISABLED at boot); the user must explicitly send 'danceon'.
// All timings/thresholds below are STARTING VALUES per the V1 spec -- not
// yet physically validated against how the actual motor+mechanism reads as
// "dancing"; expect to retune after real listening tests.
// ============================================================================

// PWM config -- identical to the physically-validated MotorPwmCalibration
// values (19kHz/8-bit). Kept as its own named pair here (rather than
// importing MotorPwmCalibration's) so DanceEngine's PWM config is not
// coupled to a calibration-test module's header; MotorDriver's
// initMotorPWM() is idempotent and the two modules are mutually exclusive
// (never both attached at once), so this never double-configures LEDC.
constexpr uint32_t DANCE_PWM_FREQUENCY_HZ = 19000;
constexpr uint8_t DANCE_PWM_RESOLUTION_BITS = 8;
constexpr uint32_t DANCE_PWM_UPDATE_MS = 15;  // duty-update/decision tick interval

// Physically-validated active speed range -- 80% is the lowest duty that
// produces useful visible movement, 100% is fast/high-energy. Never command
// an active (non-zero) speed outside this range; 0 remains valid for
// stopping/coasting.
constexpr uint8_t DANCE_MIN_SPEED_PERCENT = 80;
constexpr uint8_t DANCE_MAX_SPEED_PERCENT = 100;

// Safe reversal sequence -- ramp to 0, both inputs LOW, coast, then reverse.
// 75ms is the physically-validated default coast interval (see
// MotorPwmCalibration's DRV8833 bring-up notes).
constexpr uint32_t DANCE_REVERSE_COAST_MS = 75;

// Duty ramp rates (percent of full range per second) -- moderate ramp-up,
// a slightly slower ramp-down during normal dancing, and a quicker
// ramp-down specifically when a direction reversal is imminent.
constexpr float DANCE_RAMP_UP_PERCENT_PER_SEC = 220.0f;
constexpr float DANCE_RAMP_DOWN_PERCENT_PER_SEC = 150.0f;
constexpr float DANCE_RAMP_DOWN_FAST_PERCENT_PER_SEC = 500.0f;  // used only when ramping down to reverse

// Startup kick -- reused concept from MotorPwmCalibration: briefly command
// 100% when starting movement from a dead stop at a target below the
// threshold, then settle to the requested speed. Only applied when actually
// starting movement (RESTING->STARTING or after a reversal coast), never on
// ordinary in-flight speed updates.
constexpr uint8_t DANCE_STARTUP_KICK_PERCENT = 100;
constexpr uint32_t DANCE_STARTUP_KICK_MS = 100;
constexpr uint8_t DANCE_STARTUP_KICK_THRESHOLD_PERCENT = 90;

// Direction-change timing -- see DanceEngine.cpp's reversal-decision logic.
// A minimum hold time before ANY reversal, plus a cooldown after any
// reversal, so repeated transients can't cause rapid oscillation. Medium
// transients additionally require a longer hold than strong ones/claps.
constexpr uint32_t DANCE_MIN_DIRECTION_HOLD_MS = 700;         // strong transient/clap eligible after this long
constexpr uint32_t DANCE_MEDIUM_DIRECTION_HOLD_MS = 1200;     // medium transient eligible after this long
constexpr uint32_t DANCE_REVERSAL_COOLDOWN_MS = 1000;         // minimum gap between any two reversals

// Transient-strength thresholds (same units as AudioFeatures.transientStrength
// / Config.h's AUDIO_TRANSIENT_RISE_THRESHOLD -- normalized envelope units
// per second). MEDIUM matches the existing analyzer-wide "a transient just
// happened" bar; STRONG is deliberately higher, aiming at sharp
// clap/drop-like rises. AudioFeatures.clap is treated as an additional
// always-strong trigger.
constexpr float DANCE_TRANSIENT_MEDIUM_THRESHOLD = AUDIO_TRANSIENT_RISE_THRESHOLD;
constexpr float DANCE_TRANSIENT_STRONG_THRESHOLD = 3.5f;

// Dance-energy smoothing -- independent of AudioAnalyzer's own
// envelope smoothing (AUDIO_ATTACK_SMOOTHING/AUDIO_RELEASE_SMOOTHING in the
// Audio section above), applied on top of AudioFeatures.normalized (already
// 0..1 against the adaptive noise floor) so the dance-speed curve can be
// tuned independently of the LED-facing envelope. Fast attack when audio
// gets louder, slower release when it quiets down, so the motor doesn't
// twitch on every sample.
constexpr float DANCE_ENERGY_ATTACK = 0.45f;
constexpr float DANCE_ENERGY_RELEASE = 0.06f;

// Rest/silence hysteresis -- start threshold is higher than stop threshold
// so the motor doesn't chatter start/stop right at one boundary; the stop
// threshold must additionally be held for DANCE_SILENCE_HOLD_MS before
// actually ramping down to rest, so a single quiet instant doesn't cut a
// movement short.
constexpr float DANCE_START_ENERGY_THRESHOLD = 0.18f;
constexpr float DANCE_STOP_ENERGY_THRESHOLD = 0.10f;
constexpr uint32_t DANCE_SILENCE_HOLD_MS = 500;

// Defensive backstop, mirroring MOTION_MAX_ENERGIZED_MS/BREAKAWAY_MAX_ENERGIZED_MS
// elsewhere in this codebase -- but deliberately much longer than those:
// this module's whole point (unlike ExpressiveMotion's short pulses) is
// long sustained single-direction movement across many seconds of
// unbroken high energy with no sharp transient, which is normal/intended
// operation here, not a fault. No single continuous energized segment may
// exceed this, regardless of how long the transient-driven reversal logic
// would otherwise let it run -- a genuine backstop against a stuck/runaway
// condition, not a normal operational ceiling. An earlier, much shorter
// value (2500ms) was found during dancetest validation to trip on ordinary
// no-transient sustained playback (the low/medium/high energy steps run
// back-to-back with no transient in between); raised well above any
// realistic legitimate segment.
constexpr uint32_t DANCE_MAX_SEGMENT_MS = 10000;

// Diagnostic print rate while DanceEngine is active (see DanceEngine.cpp) --
// independent of AUDIO_DIAG_PRINT_INTERVAL_MS above, deliberately fast
// enough to watch a dance session live without flooding the serial monitor.
constexpr uint32_t DANCE_DIAG_PRINT_INTERVAL_MS = 350;

// LED power handling during dancing: DanceEngine does not read or write LED
// mute state at all -- no MotorPowerGuard request/release, and no
// DanceEngine-owned mute/suppression of its own either (an earlier
// short ~150ms startup/reversal suppression window was removed by explicit
// request). LEDs are left exactly as the user set them throughout startup,
// ramping, sustained movement, and reversal. See the README's Power
// section for the still-unmanaged DRV8833/LED shared-supply caveat this
// leaves in place, and DanceEngine.h's header comment for the full history.

// ============================================================================
// Music-reactive motor movement (see include/MusicMotorController.h and
// src/MusicMotorController.cpp) -- a separate, first-implementation
// choreographed "dance phrase" controller: slow sway -> bass-hit-triggered
// safe reversal -> rapid acceleration -> brief hip-shake burst -> smooth
// deceleration -> back to slow sway. Built directly on MotorDriver's PWM
// primitives (reusing DANCE_PWM_FREQUENCY_HZ/DANCE_PWM_RESOLUTION_BITS --
// the same validated 19kHz/8-bit config, no duplicate LEDC setup). Reuses
// AudioAnalyzer's existing AudioFeatures.normalized/.clap -- no second
// microphone pipeline, no FFT/frequency-band analysis, no BPM tracking.
// Disabled by default; the user must explicitly send 'musicmotor on'.
// Like DanceEngine, this module never calls MotorPowerGuard and never
// reads/writes LED mute state -- see DanceEngine's own physically-validated
// LED-coexistence result; this module follows the same approach from the
// start rather than adding and later removing suppression.
//
// Speed is expressed throughout as a 0-100 percent (the same "M" scale
// already used by MotorPwmCalibration's 'm20'..'m100' and DanceEngine),
// converted to raw 8-bit PWM duty in exactly one place --
// MusicMotorController.cpp's percentToMotorPwm() -- never scattered.
// ============================================================================

// --- Physical calibration (revision 3): the motor has now been tested in
// both directions on the real mechanism. M80 is the validated MINIMUM
// reliable command -- below it, movement is not dependable. Forward and
// reverse are effectively symmetric. Approximate observed rotation timing
// under the current sunflower mechanical load (NOT derivable from a linear
// PWM-percent formula -- the response is nonlinear):
//
//   M80:  quarter=1200ms  half=2200ms  full=3000ms
//   M90:  quarter=500ms   half=1000ms  full=2000ms
//   M100: quarter=250ms   half=500ms   full=1000ms
//
// See README.md's MusicMotorController section for the full writeup. No
// active movement target (LOW/MEDIUM/HIGH/PEAK/accent/reversal/hip-shake/
// spin) may intentionally command below this floor -- only QUIET (M0) and
// deceleration-toward-QUIET may pass below it, per clampTargetForBand() and
// updateDecelerating() in MusicMotorController.cpp.
constexpr uint8_t MUSIC_MOTOR_ACTIVE_MIN_PERCENT = 80;

// --- LOW-band speed (bounded variation between these two bounds each time
// a new LOW-band sway target is rolled) -- MUSIC_MOTOR_SLOW_PERCENT is the
// nominal/documented center value; the live target is
// randomRange(MIN, MAX). Runtime-tunable via 'musicmotor slow <percent>'
// (see MusicMotorController.cpp). Floor raised to MUSIC_MOTOR_ACTIVE_MIN_PERCENT
// (M80) per physical calibration -- below M80 is not a reliable movement
// command on the real mechanism. ---
constexpr uint8_t MUSIC_MOTOR_SLOW_PERCENT = 81;      // nominal reference value
constexpr uint8_t MUSIC_MOTOR_SLOW_PERCENT_MIN = 80;
constexpr uint8_t MUSIC_MOTOR_SLOW_PERCENT_MAX = 83;
constexpr uint8_t MUSIC_MOTOR_FAST_PERCENT = 100;     // hip-shake burst target, tunable via 'musicmotor fast <percent>'

// --- Ramp durations (time-based lerp, not rate-based -- see
// MusicMotorController.cpp's applyRampTick()) ---
constexpr uint32_t MUSIC_MOTOR_ACCEL_MS = 140;         // toward hip-shake/spin target, on an accepted/attempted bass hit
constexpr uint32_t MUSIC_MOTOR_FAST_HOLD_MIN_MS = 350; // hip-shake hold duration, bounded variation
constexpr uint32_t MUSIC_MOTOR_FAST_HOLD_MAX_MS = 800;
constexpr uint32_t MUSIC_MOTOR_DECEL_MS = 650;         // ramp back toward the current song-intensity target
// Absolute ceiling on total time in HIP_SHAKE even if repeated strong hits
// keep extending the hold -- guarantees eventual decay during continuous
// loud music rather than sitting at the fast target indefinitely. Not
// explicitly requested as a named constant but required by "must
// eventually decay"; set to roughly 2x the longest ordinary hold.
constexpr uint32_t MUSIC_MOTOR_HIP_SHAKE_MAX_TOTAL_MS = 1600;

// --- LOW-band periodic sway timing (how long a sway holds one direction
// before the next periodic, non-beat-driven direction change is
// attempted). Per the direction-phrase rules, this periodic timer is only
// active at LOW intensity -- at MEDIUM/HIGH/PEAK, direction changes are
// entirely beat/hit-driven (see selectBeatAction()), since longer
// one-direction movement reads as more energetic at higher intensity. ---
constexpr uint32_t MUSIC_MOTOR_NORMAL_SWAY_MIN_MS = 500;
constexpr uint32_t MUSIC_MOTOR_NORMAL_SWAY_MAX_MS = 1400;

// --- Reversal safety (shared gate used by every reversal-requesting
// caller -- periodic LOW-band sway, beat-action REVERSE_DIRECTION, and the
// mid-hip-shake case) ---
constexpr uint32_t MUSIC_MOTOR_MIN_DIRECTION_HOLD_MS = 300; // minimum time in a direction before ANY reversal
constexpr uint32_t MUSIC_MOTOR_REVERSE_COAST_MS = 40;       // both GPIO8/GPIO9 LOW between opposite-direction drive
constexpr uint32_t MUSIC_MOTOR_REVERSAL_COOLDOWN_MS = 250;  // minimum gap between two actually-accepted reversals
// Additional gate specific to extended spins: after a spin ends, a further
// reversal is refused until this much time has passed, even if the
// ordinary hold/cooldown above would otherwise allow it -- avoids an
// abrupt direction flip immediately following a committed rotation.
constexpr uint32_t MUSIC_MOTOR_POST_SPIN_DIRECTION_HOLD_MS = 350;

// ---------------------------------------------------------------------------
// Revision 6 -- "favor continuation over interruption": a MUSICAL/
// BEHAVIORAL gate, layered ON TOP OF (not a replacement for)
// MUSIC_MOTOR_MIN_DIRECTION_HOLD_MS above. That 300ms figure is a hardware-
// safety floor and stays exactly as small as the DRV8833 needs -- it still
// applies to every reversal, including the two exemptions below. This gate
// is much longer and answers a different question: has the sunflower
// actually COMMITTED to the current rotation long enough to read as a
// deliberate dance move, rather than flip-flopping on every qualifying
// strong hit? Applied only to selectBeatAction()'s "ordinary reversal slot"
// choices (LOW/MEDIUM/HIGH's own modular-counter reversal branches in
// MusicMotorController.cpp) -- NOT to the periodic LOW-band idle-sway timer
// (that's an unrelated, explicitly gentler, non-beat-driven cadence by
// design) and NOT to EXTENDED_SPIN/REVERSE_HIP_SHAKE (which already never
// interrupt an in-progress committed movement for a plain reversal).
// A reversal is exempted from this longer wait -- i.e. still allowed
// immediately (subject only to the 300ms hardware floor) -- for either of:
//   - a strong musical accent: the qualifying strong hit lands while the
//     REAL measured intensityBand is BAND_PEAK, or while a drop hold is
//     active (both already represent this module's existing notion of "the
//     biggest moments")
//   - a phrase boundary: the qualifying strong hit lands on the exact tick
//     the intensity band itself just transitioned (a genuine section
//     change, not mid-phrase reactivity)
// Runtime-tunable via 'musicmotor rotationhold <ms>' (temporary, does not
// persist through reboot, matching every other 'musicmotor <param> <value>'
// tuning command).
// ---------------------------------------------------------------------------
constexpr uint32_t MUSIC_MOTOR_MIN_ROTATION_HOLD_MS = 1800;

// --- Beat/strong-hit event cooldowns (independent of the reversal
// cooldown above -- these gate how often a beat/strong-hit EVENT itself
// can be recognized at all, separate from whether its resulting action
// specifically involves a reversal) ---
constexpr uint32_t MUSIC_MOTOR_BEAT_COOLDOWN_MS = 160;
constexpr uint32_t MUSIC_MOTOR_STRONG_HIT_COOLDOWN_MS = 300;

// Revision 5: raised from 600ms -- physical-test feedback was explicit that
// the motor must NOT stop until real quiet has persisted for "at least 5-10
// seconds" (the wind-down section should keep slowly rotating, decaying
// with it, not snap off the instant the measured band first reads QUIET).
// 7000ms sits in the middle of that requested window. This timer is keyed
// to the REAL measured intensityBand (see updateIntensitySway()), never to
// performanceEnergy below -- performanceEnergy is what keeps the motor
// visibly, gradually decaying THROUGHOUT this same window; if the timer
// itself waited on performanceEnergy too, the two multi-second decays would
// stack instead of overlap, well overshooting the requested 5-10s.
constexpr uint32_t MUSIC_MOTOR_SILENCE_TIMEOUT_MS = 7000;

// ---------------------------------------------------------------------------
// Sustained song-intensity tracking (revision 2) -- THREE independently
// smoothed signals, each serving a different purpose. Do not reuse one EMA
// for all three:
//
//   fastEnergy     fast attack, moderate release -- reacts quickly to
//                  individual beats/immediate volume changes. Feeds beat/
//                  strong-hit transient detection (with baselineEnergy).
//   songEnergy     slow attack, slower release -- represents the SUSTAINED
//                  intensity of the current musical section. Feeds the
//                  intensity-band classification and the continuous
//                  normal-movement speed target. This is the signal that
//                  was missing before: it rises when a song enters a
//                  louder section and stays elevated long enough to
//                  actually change motor behavior, instead of decaying
//                  back down between individual beats like fastEnergy does.
//   baselineEnergy very slow, unconditional (both directions) EMA of
//                  fastEnergy -- the "recent normal level" a transient
//                  must rise above to register as a beat/strong hit.
//
// All three reuse AudioAnalyzer's existing AudioFeatures.normalized
// (already noise-floor-subtracted and 0..1 normalized) as their only raw
// input -- no second microphone pipeline. Revision 5 deliberately does NOT
// add a local input-gain/rescale stage here: listening tests across
// multiple songs showed this module's existing thresholds already classify
// bands and beats reasonably on most tracks, so a blanket gain increase
// (tried and reverted during this revision) risked over-saturating already-
// well-behaved songs to fix one unusually quiet/compressed outlier. See
// this revision's report for the full reasoning and the residual
// per-track-tuning path (runtime 'musicmotor lowthreshold'/etc.) for a
// track that still under-reacts after this revision.
//
// Revision 5: these attack/release fractions are now applied exactly once
// per MUSIC_MOTOR_TICK_MS decision tick (see MusicMotorController.cpp's
// updateMusicMotorController()) instead of once per loop() iteration. The
// original per-loop-iteration placement meant the EMAs closed toward `raw`
// faster in wall-clock time than these per-tick fractions were tuned to
// represent whenever loop() ran faster than one tick -- a real cadence bug,
// independent of any threshold value, that this revision fixes universally
// (affects every song equally, does not change what counts as "loud").
// ---------------------------------------------------------------------------
constexpr float MUSIC_MOTOR_FAST_ATTACK = 0.60f;
constexpr float MUSIC_MOTOR_FAST_RELEASE = 0.15f;
constexpr float MUSIC_MOTOR_SONG_ATTACK = 0.10f;
constexpr float MUSIC_MOTOR_SONG_RELEASE = 0.035f;
// Revision 5: modestly slowed from 0.03 -- this exact failure mode (adaptive
// baseline catching up to a sustained loud section and suppressing
// transientDelta even though the section is still going) was already
// called out by this module's OWN pre-existing comment on
// MUSIC_MOTOR_DROP_HOLD_INITIAL_MS below, i.e. justified by the existing
// code's own documented reasoning, not solely by the one-song diagnostic --
// a moderate (not drastic) slowdown gives transientDelta more room to stay
// above the beat/strong-hit thresholds for a second/third hit inside the
// same sustained section, supporting "continued energy refreshes drop
// hold" without materially changing ordinary single-beat detection.
constexpr float MUSIC_MOTOR_BASELINE_ADAPT_RATE = 0.022f;

// Beat/strong-hit transient thresholds -- compared against
// transientDelta = max(0, fastEnergy - baselineEnergy). NOT true
// frequency-isolated bass detection (no FFT/band-pass filter exists in
// this codebase) -- an energy-transient approximation only.
// AudioFeatures.clap (existing raw-RMS threshold, see AUDIO_CLAP_THRESHOLD)
// is additionally treated as an automatic strong hit. Runtime-tunable via
// 'musicmotor beatthreshold'/'musicmotor hitthreshold'. Revision 5: modestly
// (not drastically) lowered from 0.12/0.28 -- multi-song listening feedback
// was that beat/intensity detection already works on several tracks but
// sensitivity is still lacking overall; this is a measured, moderate
// tightening of the margin, not a rescale of the underlying signal, so
// already-working tracks keep the same relative separation between
// ordinary beats and strong hits. Sustained-but-weak-transient sections
// (quiet/compressed mastering) are primarily addressed instead by
// MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS below and performanceEnergy
// further down, neither of which requires transientDelta to cross these
// thresholds at all.
constexpr float MUSIC_MOTOR_BEAT_DELTA_THRESHOLD = 0.10f;
constexpr float MUSIC_MOTOR_STRONG_HIT_DELTA_THRESHOLD = 0.22f;

// ---------------------------------------------------------------------------
// Intensity bands -- songEnergy classified with hysteresis (must fall this
// far BELOW a band's own entry threshold to drop out of it, not just
// below the threshold itself, so it doesn't flicker rapidly right at a
// boundary). Runtime-tunable via 'musicmotor lowthreshold'/
// 'mediumthreshold'/'highthreshold'/'peakthreshold'. Unvalidated starting
// points, per the explicit request.
// ---------------------------------------------------------------------------
constexpr float MUSIC_MOTOR_LOW_THRESHOLD = 0.10f;
constexpr float MUSIC_MOTOR_MEDIUM_THRESHOLD = 0.25f;
constexpr float MUSIC_MOTOR_HIGH_THRESHOLD = 0.48f;
constexpr float MUSIC_MOTOR_PEAK_THRESHOLD = 0.72f;
constexpr float MUSIC_MOTOR_INTENSITY_HYSTERESIS = 0.04f;

// ---------------------------------------------------------------------------
// Revision 5 -- performanceEnergy: a slowly-decaying "musical momentum"
// signal, distinct from songEnergy. songEnergy is the direct, symmetric-ish
// measurement of what the music is doing RIGHT NOW; performanceEnergy is
// what the SUNFLOWER's performance state should do, which is allowed to lag
// behind a real dip -- it ramps up quickly (comparable to songEnergy's own
// attack, so it never undercuts a genuine build) but releases over several
// seconds, so a brief lull between phrases or the tail of a big section
// doesn't collapse movement immediately. See effectiveBand() in
// MusicMotorController.cpp -- performanceEnergy can LEND a higher effective
// band (for both the live intensityTargetPercent and choreography-action
// eligibility) than the real measured intensityBand, exactly like
// dropHoldActive already lends MEDIUM->HIGH, generalized to also cover the
// LOW/QUIET wind-down case dropHold was never meant to address. Continuous
// driver is songEnergy itself (so sustained HIGH/PEAK energy keeps
// performanceEnergy elevated for as long as the section lasts); qualifying
// beats/strong hits additionally bump it, so a percussive phrase sustains
// momentum even through a brief measured dip between hits.
// MUSIC_MOTOR_PERFORMANCE_RELEASE_PER_TICK's ~0.006 gives roughly a 2.5s
// e-folding time (~7s to become negligible) -- deliberately shorter than
// MUSIC_MOTOR_SILENCE_TIMEOUT_MS's hard 7000ms stop, so by the time the
// timer actually stops the motor, performanceEnergy has already decayed
// down near zero on its own rather than being cut off mid-decay.
// ---------------------------------------------------------------------------
constexpr float MUSIC_MOTOR_PERFORMANCE_ATTACK_PER_TICK = 0.12f;
constexpr float MUSIC_MOTOR_PERFORMANCE_RELEASE_PER_TICK = 0.006f;
constexpr float MUSIC_MOTOR_PERFORMANCE_STRONG_HIT_BUMP = 0.18f;
constexpr float MUSIC_MOTOR_PERFORMANCE_BEAT_BUMP = 0.05f;

// Each non-QUIET band maps songEnergy to a motor-speed percent range;
// MusicMotorController.cpp interpolates continuously within the active
// band (bottom-of-band -> MIN_PERCENT, top-of-band -> MAX_PERCENT) rather
// than jumping straight to one fixed value. Only the LOW range is
// currently runtime-tunable (via the pre-existing 'musicmotor slow'
// command, repurposed -- see MUSIC_MOTOR_SLOW_PERCENT_MIN/_MAX above);
// MEDIUM/HIGH/PEAK stay fixed constants for this revision. Revision 3:
// re-based on MUSIC_MOTOR_ACTIVE_MIN_PERCENT (M80) instead of the earlier,
// physically-unvalidated M75 floor -- LOW/MEDIUM/HIGH/PEAK now tile
// M80-M100 contiguously.
constexpr uint8_t MUSIC_MOTOR_MEDIUM_MIN_PERCENT = 84;
constexpr uint8_t MUSIC_MOTOR_MEDIUM_MAX_PERCENT = 89;
constexpr uint8_t MUSIC_MOTOR_HIGH_MIN_PERCENT = 90;
constexpr uint8_t MUSIC_MOTOR_HIGH_MAX_PERCENT = 96;
constexpr uint8_t MUSIC_MOTOR_PEAK_MIN_PERCENT = 97;
constexpr uint8_t MUSIC_MOTOR_PEAK_MAX_PERCENT = 100;

// Non-blocking speed slew limiter -- the live "song-intensity target"
// percent (see MusicMotorController.cpp's intensityTargetPercent) moves
// toward the raw band-derived value at these rates, so it never jumps
// instantly between intensity-band speeds. Faster rise than fall: quicker
// to respond when the music intensifies, slower/more natural when it
// settles down. Does NOT apply to the very first movement out of SILENT
// (that snaps directly, matching the physically-validated original
// startup behavior -- see enterIntensitySway()) or to resuming after a
// reversal coast (also snaps, matching the original safe-reversal
// behavior) -- only to ONGOING target changes while already moving.
constexpr float MUSIC_MOTOR_SPEED_RISE_PERCENT_PER_SECOND = 70.0f;
constexpr float MUSIC_MOTOR_SPEED_FALL_PERCENT_PER_SECOND = 30.0f;

// Small, bounded, deterministic (not randomized) speed accent an ordinary
// beat may add on top of the live intensity target -- scales up slightly
// with intensity band (a "stronger accent" at HIGH than at LOW) -- see
// "motion variation" in MusicMotorController.cpp's applyBeatAccent().
constexpr uint32_t MUSIC_MOTOR_BEAT_ACCENT_MS = 150;
constexpr uint8_t MUSIC_MOTOR_BEAT_ACCENT_PERCENT = 6;

// ---------------------------------------------------------------------------
// Extended spin -- a committed, OPEN-LOOP one-direction rotation. There is
// no position sensor/encoder anywhere in this project, so this cannot
// guarantee an exact number of degrees; durations below are derived from
// the physical calibration table (see MUSIC_MOTOR_ACTIVE_MIN_PERCENT's
// comment) rather than a guess -- M90/2000ms and M100/1000ms are each
// approximately one full physical rotation; the drop-spin duration covers
// roughly 2-3 rotations at M100. Revision 3 replaces the earlier
// randomized 700-1800ms range (which was not tied to any physical
// measurement) with three named profiles, selected by context in
// pickSpinProfile() (MusicMotorController.cpp):
//   NORMAL       M90/~2000ms  -- HIGH-band-triggered spin, one rotation
//   FAST         M100/~1000ms -- PEAK-band-triggered spin, one rotation
//   EXTENDED_DROP M100/2000-3000ms -- triggered while dropHoldActive, 2-3 rotations
// 'musicmotor spintime <ms>' still works as a manual override (forces
// M100 for the given duration, for calibration) -- see
// musicMotorSetSpinTimeMs()/pickSpinProfile().
// ---------------------------------------------------------------------------
constexpr uint8_t MUSIC_MOTOR_SPIN_NORMAL_PERCENT = 90;
constexpr uint32_t MUSIC_MOTOR_SPIN_NORMAL_MS = 2000;
constexpr uint8_t MUSIC_MOTOR_SPIN_FAST_PERCENT = 100;
constexpr uint32_t MUSIC_MOTOR_SPIN_FAST_MS = 1000;
constexpr uint8_t MUSIC_MOTOR_SPIN_EXTENDED_PERCENT = 100;
constexpr uint32_t MUSIC_MOTOR_SPIN_EXTENDED_MIN_MS = 2000;
constexpr uint32_t MUSIC_MOTOR_SPIN_EXTENDED_MAX_MS = 3000;
constexpr uint32_t MUSIC_MOTOR_SPIN_COOLDOWN_MS = 3500;
// Minimum intensity BAND LEVEL required for a beat/strong-hit to be
// eligible to start a spin (0=QUIET, 1=LOW, 2=MEDIUM, 3=HIGH, 4=PEAK --
// matches MusicIntensityBand's declaration order in MusicMotorController.h
// exactly). Expressed as a plain integer rather than the enum itself so
// Config.h does not need to depend on MusicMotorController.h. Default HIGH
// (3) -- deliberately excludes MEDIUM as the authoritative gate, even
// though the prose spec's MEDIUM-band example loosely mentions "extended
// spin occasionally allowed"; the explicit constant here is treated as the
// binding rule for this revision (see MusicMotorController.cpp's
// selectBeatAction() comment for MEDIUM, which documents this choice).
constexpr uint8_t MUSIC_MOTOR_SPIN_MIN_INTENSITY_LEVEL = 3;  // HIGH
// Hard safety ceiling -- a spin is force-ended at this elapsed time
// regardless of its own (possibly beat-extended/drop-extended) nominal
// duration or any other consideration. Never run indefinitely, even though
// the mechanism physically supports continuous rotation. Raised from 2200ms
// (revision 2) to 4000ms so it no longer clips the new EXTENDED_DROP
// profile's own 2000-3000ms nominal range.
constexpr uint32_t MUSIC_MOTOR_SPIN_ABSOLUTE_MAX_MS = 4000;

// ---------------------------------------------------------------------------
// Sustained drop-hold (revision 3) -- addresses the physical-test finding
// that movement could weaken during the actual bass drop because the
// adaptive baseline (MUSIC_MOTOR_BASELINE_ADAPT_RATE) catches up to a loud
// sustained section and transientDelta falls even though the drop is still
// going. dropHoldActive is a CHOREOGRAPHY PERMISSION signal only -- it
// never overwrites the real measured MusicIntensityBand (diagnostics
// always show the true value); see selectBeatAction()'s use of a separate
// "choreography band" derived from (intensityBand, dropHoldActive).
// ---------------------------------------------------------------------------
constexpr uint32_t MUSIC_MOTOR_DROP_HOLD_INITIAL_MS = 2200;
// A later qualifying strong hit while already in a drop hold extends the
// deadline by another MUSIC_MOTOR_DROP_HOLD_INITIAL_MS from now, but never
// beyond MUSIC_MOTOR_DROP_HOLD_MAX_MS total continuous hold measured from
// when the CURRENT hold session first started -- repeated hits cannot hold
// it forever.
constexpr uint32_t MUSIC_MOTOR_DROP_HOLD_MAX_MS = 4000;

// Revision 5 -- alternate, non-transient qualification path: a sustained
// section that stays in BAND_HIGH or BAND_PEAK continuously for at least
// this long qualifies for drop hold on its own, WITHOUT needing a
// qualifying strong hit at all (see startOrRefreshDropHold()'s caller in
// MusicMotorController.cpp). Addresses sustained/compressed-mastering
// sections whose real measured intensity genuinely reaches HIGH/PEAK but
// whose transient peaks are too weak/rare to reliably cross
// MUSIC_MOTOR_STRONG_HIT_DELTA_THRESHOLD -- "do not make strong hits
// mandatory for sustained dancing or drop hold." The original strong-hit+
// PEAK path is unchanged and still fires immediately/independently; this is
// a genuinely additional OR, not a replacement.
constexpr uint32_t MUSIC_MOTOR_DROP_HOLD_SUSTAIN_CONFIRM_MS = 1800;

// ---------------------------------------------------------------------------
// Wobby tone-shift response (revision 3) -- a lightweight modulation/wobble
// cue, NOT a new frequency-analysis subsystem: just the absolute change in
// fastEnergy tick-to-tick, thresholded and debounced. Only evaluated while
// dropHoldActive AND the measured band is MEDIUM/HIGH/PEAK (see
// updateMusicMotorController()) -- an eligibility signal for an occasional
// extra REVERSE_HIP_SHAKE/accent, not a full detector on its own.
// ---------------------------------------------------------------------------
constexpr float MUSIC_MOTOR_WOBBLE_DELTA_THRESHOLD = 0.10f;
constexpr uint32_t MUSIC_MOTOR_WOBBLE_REFRACTORY_MS = 300;

// ---------------------------------------------------------------------------
// Reverse hip-shake (revision 3) -- a distinct, fully non-blocking,
// millis-based multi-phase choreography action: opposite/original/
// opposite/original (REGULAR) or a 6-phase HEAVY variant for major bass
// drops. Locks in the direction active when the shake begins ("original
// direction") and always returns to it. PWM/phase timing chosen to match
// one clean alternation cycle at the calibrated M90/M100 speeds -- these
// are deliberately NOT derived from the spin timing table (a hip shake is
// a snap side-to-side wobble, not a rotation). A brief
// MUSIC_MOTOR_REVERSE_COAST_MS coast is still inserted between each phase
// (same shared DRV8833 safety margin used by every other reversal in this
// module), so the total sequence runs slightly longer than the raw
// phase-count * phase-duration arithmetic below -- "approximately".
// ---------------------------------------------------------------------------
constexpr uint8_t MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PERCENT = 90;
constexpr uint32_t MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_MS = 200;
constexpr uint8_t MUSIC_MOTOR_REV_HIPSHAKE_REGULAR_PHASE_COUNT = 4;
constexpr uint8_t MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PERCENT = 100;
constexpr uint32_t MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_MS = 180;
constexpr uint8_t MUSIC_MOTOR_REV_HIPSHAKE_HEAVY_PHASE_COUNT = 6;
// Minimum gap between two hip-shake STARTS -- shared by both the existing
// HIP_SHAKE burst state and the new REVERSE_HIP_SHAKE (both read as "a hip
// shake just happened" to a listener, so neither should retrigger the
// other back-to-back). Randomized within [MIN,MAX] each time, matching the
// project's established "bounded variation, not a single fixed value"
// pattern (e.g. MUSIC_MOTOR_NORMAL_SWAY_MIN_MS/_MAX_MS).
constexpr uint32_t MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MIN_MS = 800;
constexpr uint32_t MUSIC_MOTOR_HIP_SHAKE_START_COOLDOWN_MAX_MS = 1200;

// ---------------------------------------------------------------------------
// Revision 7 -- SUSTAINED_DRIVE: an occasional, weighted (NOT a rigid
// repeating timer) committed continuous FORWARD/REVERSE hold, mixed into
// the existing choreography so the sunflower "commits to a high-energy
// dance move" instead of reacting to every beat. See
// MusicMotorController.cpp's "Revision 7"/"Revision 8" sections for the
// full state machine (trySustainedDriveEntry()/enterSustainedDrive()/
// updateSustainedDrive()/exitSustainedDrive()/
// performSustainedDriveExtension()/performSustainedDriveDirectionSwitch()).
//
// Revision 8 replaces the original single fixed 5-10s duration roll with a
// renewable performance-phrase model: a phrase begins with a minimum
// directional commitment, reaches an initial REVIEW point (sized by the
// musical intensity at entry), and from there either EXTENDS (another
// review point, same direction), SWITCHES (safely reverses and keeps
// going), or EXITS back to normal choreography -- see
// computeSustainedDriveContinuationDecision() in the .cpp. A phrase that
// keeps getting extended by sustained HIGH/PEAK energy can run well past
// 30s, even past 60s, with no arbitrary hard cap -- only genuine silence/
// safety/low-energy-grace-expiry end it early.
// ---------------------------------------------------------------------------

// Minimum directional commitment -- applies to STANDARD/EXTENDED/RENEWABLE
// phrases (the "long-form" path) and to each individual direction segment
// after an in-phrase switch. During this window, NOTHING (ordinary beats,
// LOW-band periodic reversal, ordinary choreography, a band dip) may
// interrupt or switch the phrase -- only emergency stop/disable/hardware
// safety/genuine silence. See Config.h's SHORT-tier constants below for the
// one case this does NOT apply to.
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_MIN_COMMITMENT_MS = 5000;

// Revision 8 addition -- SHORT tier: a brief, intentional 1-5s committed
// burst (a quick directional accent, not a failed long attempt and not an
// ordinary beat pulse). Uses ITS OWN commitment (this range), never the
// MIN_COMMITMENT_MS above -- see chooseSustainedDriveEntryTier() and
// enterSustainedDrive()'s tier handling in the .cpp.
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MIN_MS = 1000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_MAX_MS = 5000;

// Initial review point ranges, by effective band at entry (PEAK/dropHold
// share the same, widest range -- both represent "the biggest moments").
// These are REVIEW times, not forced exits -- see
// computeSustainedDriveContinuationDecision().
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_MEDIUM_MIN_MS = 5000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_MEDIUM_MAX_MS = 10000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_HIGH_MIN_MS = 8000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_HIGH_MAX_MS = 18000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_PEAK_MIN_MS = 12000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_REVIEW_PEAK_MAX_MS = 30000;

// Extension ranges -- each time a review confirms the phrase should
// continue, the NEXT review is scheduled this far out (by the effective
// band AT THAT REVIEW, not the entry band -- a phrase can move between
// these ranges as the music itself moves between bands). Repeated
// extensions are what let a genuinely sustained HIGH/PEAK section run the
// phrase past 30s, then past 60s, with no separate "long phrase" code path
// -- it is the same mechanism, just kept alive by real, continuing energy.
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_MEDIUM_MIN_MS = 3000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_MEDIUM_MAX_MS = 7000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_HIGH_MIN_MS = 5000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_HIGH_MAX_MS = 12000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_PEAK_MIN_MS = 8000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTEND_PEAK_MAX_MS = 18000;

// Phrase-TIER labels are purely descriptive/diagnostic once a phrase is on
// the long-form path (SHORT is a distinct entry choice, never relabeled
// except via promotion -- see below): a phrase crossing these total-elapsed
// thresholds is reported as EXTENDED, then RENEWABLE, for
// 'musicmotor status'/logging only. Neither threshold changes any actual
// continuation behavior -- see performSustainedDriveExtension().
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_EXTENDED_ELAPSED_MS = 15000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_RENEWABLE_ELAPSED_MS = 30000;

// Entry TIER weight -- percent chance (0-100) that a NEW phrase entry picks
// the SHORT tier instead of the long-form (STANDARD-and-up) path, by
// effective band. "MEDIUM favors short sustained phrases...HIGH uses a
// mixture...PEAK/DropHold favor standard/extended but may occasionally use
// a short explosive burst" -- see chooseSustainedDriveEntryTier(), which
// also halves this weight further when DropHold is active (favor long even
// more strongly during a confirmed drop) and boosts it on a major
// transient (an explosive-burst opportunity even when the surrounding
// energy alone wouldn't have favored SHORT).
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_MEDIUM_PERCENT = 70;
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_HIGH_PERCENT = 40;
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_WEIGHT_PEAK_PERCENT = 20;
// Additive boost to the short-tier weight when entry coincides with an
// especially large transient (a spike well above the ordinary strong-hit
// threshold) -- "a major transient may trigger a 1-5 second directional
// burst even when the surrounding energy does not justify a long phrase."
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_SHORT_TIER_MAJOR_TRANSIENT_BOOST_PERCENT = 25;
// A major transient's transientDelta must be at least this MULTIPLE of the
// ordinary strong-hit threshold to count as "major" (vs. merely qualifying).
constexpr float MUSIC_MOTOR_SUSTAINED_DRIVE_MAJOR_TRANSIENT_MULTIPLIER = 1.5f;

// After a SHORT phrase's own commitment completes, a qualifying musical
// event (the SAME continuation logic used by long-form reviews) may
// PROMOTE it into a longer phrase or an in-phrase direction switch instead
// of exiting -- gated by this additional percent chance so "avoid turning
// every short phrase into a long phrase" / "short phrases do not always
// promote" both hold. See the .cpp's SHORT-tier review handling in
// updateSustainedDrive().
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_PROMOTION_PERCENT = 30;

// Low-energy grace -- "do not immediately exit when the band dips."
// Tracked from the moment effectiveBand() first drops below BAND_MEDIUM
// while a phrase is active; if it climbs back to MEDIUM+ before this
// elapses, the grace timer simply resets (see
// updateSustainedDrive()'s low-energy-grace tracking) -- only a genuinely
// SUSTAINED dip this long actually ends the phrase. Deliberately much
// shorter than MUSIC_MOTOR_SILENCE_TIMEOUT_MS (7000ms): genuine QUIET/
// silence remains entirely the shared silence timeout's job (see
// checkAndHandleSilenceTimeout()) -- this constant only concerns a
// LOW/MEDIUM dip that never reaches genuine QUIET at all, avoiding two
// competing silence-ish timers.
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_LOW_GRACE_MS = 3500;

// Direct sustained-direction switching (FORWARD<->REVERSE without exiting
// the phrase) -- see performSustainedDriveDirectionSwitch(). Ordinary
// cooldown between two in-phrase switches:
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MIN_MS = 8000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_SWITCH_COOLDOWN_MAX_MS = 15000;
// Exceptional early-reversal floor -- when BAND_PEAK and an active drop
// hold occur TOGETHER (both, not either -- "an exceptional PEAK/drop
// event"), a switch may bypass the ordinary cooldown above, but NEVER
// bypasses this floor measured from the CURRENT direction segment's own
// start -- "shortly after the initial 5-second commitment," not
// immediately at it. Always evaluated on a QUALIFIED STRONG HIT only --
// ordinary beats can never reach this path at all (see
// computeSustainedSwitchQualification()).
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_SWITCH_EXCEPTIONAL_MIN_MS = 5500;

// Persistent-energy entry opportunity (Revision 8) -- lets a sustained
// HIGH/PEAK section start a phrase even without a fresh qualifying strong
// hit ("sustained HIGH-energy songs that do not produce frequent qualified
// transients"). Reuses the EXISTING sustainedHighSinceMs dwell timer
// (Revision 5's drop-hold sustain-confirm signal) rather than a new dwell
// tracker -- a continuous HIGH/PEAK dwell of at least this long makes the
// opportunity available at all:
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_DWELL_MS = 4000;
// ...and even once available, the opportunity itself is only RE-EVALUATED
// this often (a hard rate limit, independent of loop()/tick cadence) --
// "a percentage is meaningless if rolled hundreds of times per second."
// Uses the SAME entry weights as the strong-hit path
// (MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_*_PERCENT below) -- one weight table,
// two independently rate-limited opportunities to roll against it.
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_PERSISTENT_ENTRY_REVIEW_MS = 2500;

// Cooldown -- randomized once per true phrase EXIT (not per entry, and NOT
// applied to an in-phrase direction switch -- see
// performSustainedDriveDirectionSwitch()'s own comment), so consecutive
// sustained drives can't stack back-to-back; combined with the low
// per-strong-hit weight rolls below, this is what keeps SUSTAINED_DRIVE
// "occasional," not "continuous."
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MIN_MS = 8000;
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_COOLDOWN_MAX_MS = 18000;

// Entry weight -- percent chance (0-100), rolled against a single qualifying
// STRONG HIT at effective MEDIUM/HIGH/PEAK (never on an ordinary beat, and
// never on its own timer -- "weighted choreography selection rather than a
// rigid repeating timer"). Checked BEFORE that band's own modular reversal/
// hip-shake/spin counter logic in selectBeatAction(), so it doesn't disturb
// those counters' existing cadence when it doesn't fire. Deliberately small
// at MEDIUM ("uncommon"), larger at HIGH, largest at PEAK ("more likely
// during sustained HIGH or PEAK sections") -- first reasonable values, not
// validated against a physical listening pass; retune here (or via a
// future runtime command, not currently exposed) if it fires too often/
// rarely in practice.
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_MEDIUM_PERCENT = 3;
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_HIGH_PERCENT = 12;
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_WEIGHT_PEAK_PERCENT = 22;

// Direction selection -- "normally prefer the current direction to avoid an
// unnecessary entry reversal... occasionally select the opposite direction
// when a strong musical accent justifies it." This is the percent chance
// (0-100) of flipping, evaluated ONLY when a strong accent is already in
// play (real BAND_PEAK or an active drop hold) -- with no accent, entry
// NEVER flips direction on its own.
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_ACCENT_FLIP_PERCENT = 35;

// "Prevent more than two consecutive sustained drives in the same
// direction unless a continuing PEAK/drop section strongly supports it" --
// the cap itself (a 3rd consecutive same-direction entry is blocked/forced
// to flip) and its override condition (real BAND_PEAK AND an active drop
// hold, together read as "the section is still genuinely going") both live
// in MusicMotorController.cpp's chooseSustainedDriveDirection().
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_DRIVE_MAX_CONSECUTIVE_SAME_DIRECTION = 2;

// ---------------------------------------------------------------------------
// Revision 8 addition -- lifelike silence/low-energy stopping. Most genuine
// musical endings should look like the sunflower naturally running out of
// energy (MUSICAL_RAMP_DOWN state), not an abrupt cut -- see
// MusicMotorController.cpp's beginMusicalSilenceStop()/
// enterMusicalRampDown()/updateMusicalRampDown(). This is entirely separate
// from, and never weakens, real safety stops: emergency stop/'k'/MusicMotor
// disable/hardware faults still call hardStop() directly and are
// completely unaffected by anything below -- only the SHARED SILENCE
// TIMEOUT's own resulting action changes (see checkAndHandleSilenceTimeout()),
// from an unconditional stopCleanly() to a chosen stop STYLE. The timeout
// itself (MUSIC_MOTOR_SILENCE_TIMEOUT_MS, and the hysteresis around it)
// stays completely unchanged and remains the sole authority on "is this
// genuinely silence" -- no second/competing silence timer is introduced.
// ---------------------------------------------------------------------------

// Ramp-down duration ranges, chosen by how fast the motor was actually
// going at the moment silence is confirmed (currentSpeedPercent) -- "lower-
// energy movement may stop in ~1-2s; high-energy sustained movement may
// take ~2-4s to settle."
constexpr uint32_t MUSIC_MOTOR_RAMP_DOWN_LOW_MIN_MS = 1000;
constexpr uint32_t MUSIC_MOTOR_RAMP_DOWN_LOW_MAX_MS = 2000;
constexpr uint32_t MUSIC_MOTOR_RAMP_DOWN_HIGH_MIN_MS = 2000;
constexpr uint32_t MUSIC_MOTOR_RAMP_DOWN_HIGH_MAX_MS = 4000;

// Dramatic-abrupt-stop chance (percent, 0-100) -- "occasional, not the
// default." Two tiers, driven by real musical evidence rather than a flat
// coin flip: the SHARP tier applies when a drop hold was active recently
// (MUSIC_MOTOR_SHARP_CUTOFF_DROPHOLD_RECENCY_MS below) -- "a major drop
// followed by immediate silence" -- which is measurably more likely to be a
// deliberate dramatic cutoff than an ordinary quiet fade.
constexpr uint8_t MUSIC_MOTOR_ABRUPT_STOP_NORMAL_PERCENT = 12;
constexpr uint8_t MUSIC_MOTOR_ABRUPT_STOP_SHARP_CUTOFF_PERCENT = 25;
// How recently dropHoldActive must have been true (see the .cpp's
// lastDropHoldActiveMs) to count as "a major drop just happened" for the
// sharp-cutoff tier above.
constexpr uint32_t MUSIC_MOTOR_SHARP_CUTOFF_DROPHOLD_RECENCY_MS = 3000;

// ============================================================================
// Revision 9 -- relative/song-adaptive musical-section (EDM/dubstep "drop")
// recognition. This is an ADDITIVE layer, not a replacement: it never
// lowers Revision 8's validated MUSIC_MOTOR_LOW/MEDIUM/HIGH/PEAK_THRESHOLD,
// MUSIC_MOTOR_BEAT_DELTA_THRESHOLD, MUSIC_MOTOR_STRONG_HIT_DELTA_THRESHOLD,
// or any Revision 7/8 sustained-drive/speed-floor constant above. It exists
// because the ABSOLUTE band detector (by design) can only ever say "how
// loud is this instant relative to a fixed, song-independent scale" -- it
// has no way to recognize "this section is dramatically more intense than
// the last 10 seconds of the SAME song," which is how EDM/dubstep drops are
// actually defined. Nothing here was tuned against the stale pre-Revision-5
// physical-test log described in this project's Revision 9 discussion --
// that log was confirmed to predate Revisions 5-8 entirely (wrong beat/
// strong-hit thresholds, wrong silence-stop message, no Revision 7/8
// diagnostic lines) and therefore cannot validate or invalidate anything
// about Revision 8. This layer is added because relative, song-adaptive
// drop recognition is independently valuable for EDM/dubstep material, and
// it must be physically A/B tested against Revision 8 on CURRENT firmware
// (see MUSIC_MOTOR_RELATIVE_DROP_ENABLED_DEFAULT below) before any of its
// defaults are trusted.
// ============================================================================

// Master enable switch for the entire relative-drop layer, runtime-tunable
// via 'musicmotor dropdetect on/off' (see musicMotorSetRelativeDropEnabled()
// in MusicMotorController.cpp/Controls.cpp). Defaults to enabled so the
// feature is exercised by default, but can be switched off at runtime with
// no rebuild to get a clean Revision-8-only A/B comparison on the same
// firmware image -- this is the mechanism requested to compare "Revision 8
// qualification alone" against "Revision 8 + relative EDM drop
// qualification" without needing two separate builds.
constexpr bool MUSIC_MOTOR_RELATIVE_DROP_ENABLED_DEFAULT = true;

// --- Rolling reference/density tracking -----------------------------------
// These feed the relative (song-adaptive) signals below. All are simple
// EMAs/leaky integrators over the existing MUSIC_MOTOR_TICK_MS cadence --
// no FFT, no true spectral analysis (see the Revision 9 report for what
// would be needed for genuine bass-band/tone-shift detection).

// Bass-energy proxy EMA -- smooths AudioFeatures.lowFrequencyEnergy (see
// AudioAnalyzer.cpp/AUDIO_BASS_LP_ALPHA) the same way fastEnergy smooths
// rawEnergy, so bass impacts can be compared against a bass baseline the
// same way overall impacts are compared against baselineEnergy. This is a
// single-pole low-pass RMS proxy, NOT true FFT/band-split bass detection --
// documented as an approximation, not pretended to be more than it is.
constexpr float MUSIC_MOTOR_BASS_FAST_ALPHA = 0.35f;
constexpr float MUSIC_MOTOR_BASS_BASELINE_ALPHA = 0.01f;
// A bass-energy rise (bassFastEnergy - bassBaselineEnergy) at or above this
// counts as a qualifying "bass impact" tick for bassImpactContribution/
// bassDensityScore below. Independent of (not derived from) the absolute
// MUSIC_MOTOR_*_THRESHOLD band constants.
constexpr float MUSIC_MOTOR_BASS_IMPACT_DELTA_THRESHOLD = 0.12f;

// Buildup/context reference: a slower EMA of songEnergy than songEnergy's
// own smoothing, representing "what this section has sounded like over the
// last several seconds" -- the yardstick a buildup is measured against.
constexpr float MUSIC_MOTOR_BUILDUP_REFERENCE_ALPHA = 0.006f;
// Long song reference: a much slower EMA of songEnergy, representing "what
// this song has typically sounded like so far" -- the yardstick a genuine
// section-to-section CONTRAST is measured against. Deliberately slow enough
// that a section which simply STAYS loud will, over time, pull this
// reference up to match it -- which is what prevents a continuously loud
// section from repeatedly re-qualifying as a "new" drop (see
// computeSectionContrastScore() in the .cpp).
constexpr float MUSIC_MOTOR_LONG_SONG_REFERENCE_ALPHA = 0.0015f;
// Floor added to both references before use as a ratio denominator, so
// near-silence doesn't produce huge/unstable relative-rise ratios.
constexpr float MUSIC_MOTOR_RELATIVE_REFERENCE_FLOOR = 0.05f;

// Beat/transient/bass "density" leaky integrators -- each ticks up by 1.0
// on a qualifying event and decays by this factor every MUSIC_MOTOR_TICK_MS,
// giving a continuous "how often has this been happening lately" score
// rather than a hard fixed-size window. At MUSIC_MOTOR_TICK_MS=15ms this
// decay retains roughly 72% after 1s and roughly 19% after 5s -- an
// effective memory of a few seconds, matching the "recent window" the
// EDM/dubstep clarification asked for conceptually (this is an exponential
// memory, not a literal sliding window, chosen because it needs no sample
// buffer).
constexpr float MUSIC_MOTOR_DENSITY_DECAY_PER_TICK = 0.995f;
// Score value treated as "maximally dense" when normalizing a density score
// into a [0,1] signal contribution (see computeDropSignalScores()).
constexpr float MUSIC_MOTOR_BEAT_DENSITY_NORMALIZATION = 3.0f;
constexpr float MUSIC_MOTOR_TRANSIENT_DENSITY_NORMALIZATION = 5.0f;
constexpr float MUSIC_MOTOR_BASS_DENSITY_NORMALIZATION = 3.0f;

// --- EDM/dubstep genre drop profile ----------------------------------------
// Centralizes every genre-specific assumption in one place (per the
// explicit "do not scatter genre assumptions throughout the controller"
// requirement) instead of embedding them inline in decision functions.
// Only one profile exists today (EDM_DUBSTEP); MusicGenreProfile in
// MusicMotorController.cpp is deliberately an enum of one so adding
// ROCK/HIP_HOP/POP/METAL/ACOUSTIC/GENERAL later is a matter of adding a
// profile struct + enum value, not restructuring the detector.
//
// Signal weights are the relative importance of each contribution to
// overall drop confidence; they sum to 1.0. Chosen to match the
// conceptual EDM/dubstep weighting requested (bass impact/density,
// relative energy rise, and buildup-resolution weighted highest; beat
// density/pace and transient density next; tone-change/spectrum-expansion
// omitted entirely -- see the Revision 9 report for why: this firmware has
// no spectral/tone-shift measurement today, so a tone-change weight would
// be a placeholder multiplying a signal that doesn't exist yet, which is
// exactly the "don't hard-code weights blindly" pitfall). These are
// starting values, not physically validated -- tune after the Test
// A/Test B physical comparison described in the Revision 9 report.
constexpr float MUSIC_MOTOR_EDM_WEIGHT_ENERGY_RISE = 0.20f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_SECTION_CONTRAST = 0.10f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_BASS_IMPACT = 0.20f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_BASS_DENSITY = 0.15f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_BEAT_DENSITY = 0.12f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_TRANSIENT_DENSITY = 0.12f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_BUILDUP_RESOLUTION = 0.08f;
constexpr float MUSIC_MOTOR_EDM_WEIGHT_SUSTAINED_ENERGY = 0.03f;

// Drop-confidence tier thresholds (on the combined [0,1] dropConfidence
// score) -- POSSIBLE_DROP enhances accents only; CONFIRMED_DROP strongly
// biases sustained-drive entry; MAJOR_DROP strongly biases/guarantees a
// high-speed sustained phrase when safety permits.
constexpr float MUSIC_MOTOR_EDM_POSSIBLE_DROP_THRESHOLD = 0.35f;
constexpr float MUSIC_MOTOR_EDM_CONFIRMED_DROP_THRESHOLD = 0.55f;
constexpr float MUSIC_MOTOR_EDM_MAJOR_DROP_THRESHOLD = 0.75f;
// A lower "sustain floor" (< POSSIBLE) used only to keep an already-ACTIVE
// drop alive through brief dips -- hysteresis, same rationale as
// MUSIC_MOTOR_INTENSITY_HYSTERESIS above.
constexpr float MUSIC_MOTOR_EDM_ACTIVE_SUSTAIN_FLOOR = 0.22f;
// NEUTRAL->BUILDUP entry / BUILDUP->NEUTRAL fade thresholds -- deliberately
// LOWER than MUSIC_MOTOR_EDM_POSSIBLE_DROP_THRESHOLD itself: BUILDUP is
// meant to catch "rising, not yet qualified" evidence early (per "buildup
// alone should raise readiness/confidence, not trigger full-speed drive"),
// while still requiring the sustained MUSIC_MOTOR_EDM_BUILDUP_MIN_MS dwell
// below before promoting further.
constexpr float MUSIC_MOTOR_EDM_BUILDUP_ENTRY_THRESHOLD = 0.18f;
constexpr float MUSIC_MOTOR_EDM_BUILDUP_FADE_THRESHOLD = 0.09f;

// --- Phase timing -----------------------------------------------------------
// How long rising evidence must persist before NEUTRAL->BUILDUP->DROP_ARMED
// promotion, matching "do not define a drop from one isolated sample."
constexpr uint32_t MUSIC_MOTOR_EDM_BUILDUP_MIN_MS = 2000;
// DROP_ARMED expires back toward BUILDUP/NEUTRAL if the anticipated arrival
// never happens within this window.
constexpr uint32_t MUSIC_MOTOR_EDM_ARMED_TIMEOUT_MS = 6000;
// DROP_IMPACT must keep qualifying for this long before promoting to
// DROP_ACTIVE; a qualifying impact that collapses before this elapses is
// classified as an accent/hit, not a drop (per the explicit "a single
// impact followed immediately by low energy should be an accent, not
// necessarily a full drop" requirement).
constexpr uint32_t MUSIC_MOTOR_EDM_IMPACT_CONFIRM_MS = 400;
// DROP_ACTIVE must fall below MUSIC_MOTOR_EDM_ACTIVE_SUSTAIN_FLOOR
// continuously for this long before releasing to DROP_RELEASE -- short
// rhythmic gaps within an active drop are tolerated.
constexpr uint32_t MUSIC_MOTOR_EDM_RELEASE_GRACE_MS = 2500;
// Refractory period after DROP_RELEASE before a new BUILDUP/DROP_ARMED
// cycle may begin -- prevents "one beat during a quiet section" or the
// tail of a just-released drop from immediately re-arming.
constexpr uint32_t MUSIC_MOTOR_EDM_REFRACTORY_MS = 1500;

// --- Sustained-drive entry escalation from an active drop ------------------
// A CONFIRMED_DROP or MAJOR_DROP that has been DROP_ACTIVE without yet
// starting a sustained phrase gets an escalating entry weight over time --
// "prolonged major drop cannot pass with zero sustained-drive opportunity."
// These are ADDITIVE opportunities alongside (never a replacement for)
// Revision 7/8's own computeSustainedDriveDecision() weighting.
constexpr uint8_t MUSIC_MOTOR_EDM_ENTRY_INITIAL_PERCENT = 40;
constexpr uint8_t MUSIC_MOTOR_EDM_ENTRY_ESCALATED_PERCENT = 70;
constexpr uint8_t MUSIC_MOTOR_EDM_ENTRY_GUARANTEED_PERCENT = 100;
constexpr uint32_t MUSIC_MOTOR_EDM_ENTRY_ESCALATE_AFTER_MS = 2500;
constexpr uint32_t MUSIC_MOTOR_EDM_ENTRY_GUARANTEE_AFTER_MS = 6000;
// Rate limit between successive entry-opportunity rolls for the SAME
// ongoing drop, so DROP_ACTIVE doesn't re-roll every single tick.
constexpr uint32_t MUSIC_MOTOR_EDM_ENTRY_REVIEW_MS = 1500;
// MAJOR_DROP entry additionally requires effectiveBand() at or above MEDIUM
// -- i.e. this escalation widens WHEN a section qualifies, not the safety
// floor of WHETHER the motor may run at all.

// --- Sustained speed floor tiers ---------------------------------------
// Reuses ALREADY-VALIDATED percent breakpoints from the Revision 8 band
// table above (MUSIC_MOTOR_HIGH_MIN/PEAK_MIN/PEAK_MAX_PERCENT) rather than
// inventing new, physically-unvalidated speed values -- "using the
// already-validated motor scale, not blindly assuming M100." NORMAL is
// unchanged Revision 8 baseline behavior; PERFORMANCE and PEAK_DROP are new
// SELECTION LOGIC (which floor applies when), not new speed numbers.
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_NORMAL_FLOOR_PERCENT = MUSIC_MOTOR_HIGH_MIN_PERCENT;      // 90
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_PERFORMANCE_FLOOR_PERCENT = MUSIC_MOTOR_PEAK_MIN_PERCENT; // 97
constexpr uint8_t MUSIC_MOTOR_SUSTAINED_PEAK_FLOOR_PERCENT = MUSIC_MOTOR_PEAK_MAX_PERCENT;         // 100
// Ramp-up rate toward these floors intentionally reuses
// MUSIC_MOTOR_SPEED_RISE_PERCENT_PER_SECOND unchanged (70%/s already
// reaches 0->100 in under 1.5s) rather than adding a second, untested,
// even-faster ramp constant. No thermal/continuous-run limit exists
// because this hardware (DRV8833 driving a brushed DC motor) has no
// temperature sensor to enforce one against -- documented here rather than
// simulated.

// ============================================================================
// Revision 10 -- physical choreography and dynamic-range refinement,
// following the first successful Revision 9 physical drop-detection test.
// Two problems observed physically: (1) historical performanceEnergy lent
// an effective HIGH/PEAK band (and near-M90 motor targets) for seconds
// after raw/fast/song energy had genuinely collapsed to QUIET/LOW, so
// mellow/quiet moments kept moving too fast; (2) major drops mostly showed
// as repeated ACCENT_CURRENT_DIRECTION inside plain INTENSITY_SWAY rather
// than decisive, varied SUSTAINED_DRIVE choreography. Nothing here weakens
// Revision 9's relative drop DETECTION (confidence scoring/phase machine
// are untouched) -- this revision only changes what happens with a section
// once it's already classified.
// ============================================================================

// --- Speed-authority cap ("bounded lending") --------------------------------
// performanceEnergy/dropHold/relative-drop MEMORY may still lend a higher
// EFFECTIVE BAND (for drop continuity, short-gap tolerance, phrase
// renewal -- Revision 9's own machinery, unchanged). This section instead
// bounds how much that lending may raise the *commanded motor speed*,
// keyed to the CURRENT MEASURED band -- see
// MusicMotorController.cpp's applySpeedAuthorityCap(). QUIET gets a brief
// grace window (a genuinely momentary dip in a still-energetic song
// shouldn't instantly cap); LOW is capped immediately (computeIntensityBand()'s
// own hysteresis already prevents chatter at the band boundary, so no
// separate grace is needed here); MEDIUM may be raised by at most one
// bounded amount; HIGH/PEAK are never capped (full authority).
constexpr uint32_t MUSIC_MOTOR_QUIET_CAP_GRACE_MS = 700;
constexpr uint8_t MUSIC_MOTOR_MEDIUM_LENDING_BOUNDED_RAISE_PERCENT = 6;

// --- Motion palette (choreography speed roles) ------------------------------
// Centralized speed roles so mellow/normal/drop motion are visually
// distinct. Deliberately ALIASES the already-validated Revision 3/8/9 band
// and sustained-floor percentages rather than inventing new, physically
// untested speed values -- "using the already-validated motor scale."
// MELLOW reuses the existing M80-83 slow-sway range; the perceptible
// "much slower" feel for MELLOW/QUIET_BUILDUP comes from movement DUTY
// CYCLE (pulse/rest choreography, below), not from a lower continuous PWM
// value -- this firmware has never validated reliable continuous rotation
// below MUSIC_MOTOR_ACTIVE_MIN_PERCENT (M80), so none is introduced here.
constexpr uint8_t MUSIC_MOTOR_MOTION_QUIET_BUILDUP_PERCENT = MUSIC_MOTOR_ACTIVE_MIN_PERCENT;       // M80
constexpr uint8_t MUSIC_MOTOR_MOTION_MELLOW_MIN_PERCENT = MUSIC_MOTOR_SLOW_PERCENT_MIN;            // M80
constexpr uint8_t MUSIC_MOTOR_MOTION_MELLOW_MAX_PERCENT = MUSIC_MOTOR_SLOW_PERCENT_MAX;            // M83
constexpr uint8_t MUSIC_MOTOR_MOTION_GROOVE_MIN_PERCENT = MUSIC_MOTOR_MEDIUM_MIN_PERCENT;          // M84
constexpr uint8_t MUSIC_MOTOR_MOTION_GROOVE_MAX_PERCENT = MUSIC_MOTOR_MEDIUM_MAX_PERCENT;          // M89
constexpr uint8_t MUSIC_MOTOR_MOTION_HIGH_ENERGY_MIN_PERCENT = MUSIC_MOTOR_HIGH_MIN_PERCENT;       // M90
constexpr uint8_t MUSIC_MOTOR_MOTION_HIGH_ENERGY_MAX_PERCENT = MUSIC_MOTOR_HIGH_MAX_PERCENT;       // M96
constexpr uint8_t MUSIC_MOTOR_MOTION_CONFIRMED_DROP_PERCENT = MUSIC_MOTOR_SUSTAINED_PERFORMANCE_FLOOR_PERCENT;  // M97
constexpr uint8_t MUSIC_MOTOR_MOTION_MAJOR_DROP_PERCENT = MUSIC_MOTOR_SUSTAINED_PEAK_FLOOR_PERCENT;             // M100

// --- Movement duty cycle (visible-pace choreography, not raw PWM) ----------
// While the chosen motion tier is QUIET_BUILDUP or MELLOW, INTENSITY_SWAY
// alternates between a brief driving "pulse" (at the tier's own palette
// percent above) and a longer coasted "rest," instead of driving
// continuously -- this is what makes mellow sections and quiet buildups
// visibly slower without ever commanding an unvalidated sub-M80 PWM value.
// GROOVE/HIGH_ENERGY/drop tiers are unaffected (always continuous, duty
// 100%) -- matches "normal groove: moderate continuous choreography."
constexpr uint32_t MUSIC_MOTOR_QUIET_BUILDUP_PULSE_MIN_MS = 500;
constexpr uint32_t MUSIC_MOTOR_QUIET_BUILDUP_PULSE_MAX_MS = 900;
constexpr uint32_t MUSIC_MOTOR_QUIET_BUILDUP_REST_MIN_MS = 3000;
constexpr uint32_t MUSIC_MOTOR_QUIET_BUILDUP_REST_MAX_MS = 6000;
// Revision 10.1 -- DROP_ARMED is a strictly more confident/imminent state
// than plain BUILDUP ("noticeably expectant motion" vs. "occasional
// subtle pulse"); it reuses the same pulse duration but a much shorter
// rest, giving denser, more frequent movement without a new speed value.
constexpr uint32_t MUSIC_MOTOR_DROP_ARMED_REST_MIN_MS = 1200;
constexpr uint32_t MUSIC_MOTOR_DROP_ARMED_REST_MAX_MS = 2500;
constexpr uint32_t MUSIC_MOTOR_MELLOW_PULSE_MIN_MS = 1800;
constexpr uint32_t MUSIC_MOTOR_MELLOW_PULSE_MAX_MS = 3200;
constexpr uint32_t MUSIC_MOTOR_MELLOW_REST_MIN_MS = 1000;
constexpr uint32_t MUSIC_MOTOR_MELLOW_REST_MAX_MS = 2200;

// --- Quiet-buildup "alive" qualification ------------------------------------
// Distinguishes a genuine quiet MUSICAL buildup from an empty, silent room
// -- QUIET_BUILDUP motion only ever activates when BOTH there is real audio
// above the room-noise floor AND Revision 9's own relative-drop detector
// independently agrees a buildup/armed section is in progress (reuses
// musicalSectionPhase -- no separate/duplicate buildup heuristic). If
// 'musicmotor dropdetect' is OFF, QUIET_BUILDUP simply never qualifies
// (falls back to ordinary rest/silence handling) -- documented, not a
// silent gap.
constexpr float MUSIC_MOTOR_ROOM_NOISE_FLOOR = 0.04f;
// Safety bound: even if the phase machine were somehow still BUILDUP/
// DROP_ARMED after this much continuous QUIET, stop treating it as a live
// buildup (ARMED_TIMEOUT_MS already bounds this far more tightly in
// practice -- this is a defensive outer limit only).
constexpr uint32_t MUSIC_MOTOR_QUIET_BUILDUP_MAX_QUIET_MS = 25000;

// --- Faster, more decisive drop entry (Section 4) ---------------------------
// MAJOR_DROP gets its own, much shorter escalation ladder than the general
// EDM_ENTRY_* constants (Config.h's Revision 9 section) -- "do not allow a
// major active drop to remain indefinitely in ordinary INTENSITY_SWAY
// because of random weight-roll failures." CONFIRMED_DROP's general
// ladder is also shortened (guarantee arrives sooner than Revision 9's
// original 6000ms).
constexpr uint8_t MUSIC_MOTOR_EDM_ENTRY_MAJOR_IMMEDIATE_PERCENT = 85;
constexpr uint32_t MUSIC_MOTOR_EDM_ENTRY_MAJOR_GUARANTEE_AFTER_MS = 1200;
constexpr uint32_t MUSIC_MOTOR_EDM_ENTRY_CONFIRMED_GUARANTEE_AFTER_MS = 3500;

// --- Drop choreography phrase vocabulary ------------------------------------
// Centralized limits for the FULL_SUSTAIN/SUSTAINED_REVERSAL/
// DROP_BOOTY_SHAKE/DROP_PUNCH_AND_HOLD/DOUBLE_PUNCH/SUSTAIN_WITH_ACCENTS
// phrase vocabulary -- see MusicMotorController.cpp's DropPhraseType/
// selectDropPhraseType()/buildDropPhraseSteps(). Punches deliberately use
// the full validated M100 ceiling ("hit hard"); everything here is a
// TIMING/COUNT limit, never a new speed value beyond what Revision 8/9
// already validated (M80/M90/M97/M100 breakpoints).
constexpr uint8_t MUSIC_MOTOR_DROP_PHRASE_PUNCH_PERCENT = 100;
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_PUNCH_MIN_MS = 350;
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_PUNCH_MAX_MS = 600;
// Brief ramp-down applied before any in-phrase direction change (punch or
// reversal) -- ensures every polarity change is visually a controlled
// deceleration into the existing coast-before-flip safety primitive, never
// an instantaneous full-forward-to-full-reverse command.
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_DECEL_MS = 300;
constexpr uint8_t MUSIC_MOTOR_DROP_PHRASE_MAX_BOOTY_SHAKES_PER_DROP = 2;
constexpr uint8_t MUSIC_MOTOR_DROP_PHRASE_MAX_REVERSALS_PER_SHAKE = 3;
constexpr uint8_t MUSIC_MOTOR_DROP_PHRASE_MAX_SUSTAINED_REVERSALS_PER_DROP = 1;
// An unusually long drop earns one additional sustained reversal allowance
// beyond the normal per-drop cap above.
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_LONG_DROP_EXTRA_REVERSAL_MS = 20000;
// Cooldown after a booty-shake/punch SEQUENCE completes (settles into its
// terminal sustain step) before another non-FULL_SUSTAIN sequence may
// begin -- "do not let ordinary individual beats retrigger a new shake
// sequence while one is already active" (the sequencer itself already
// blocks re-entry while active; this additionally spaces sequences apart
// once one finishes).
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_SEQUENCE_COOLDOWN_MS = 4000;
// "Avoid switching during short drops" -- a reselection cue (mid-drop
// phrase change) is only considered once the drop has been DROP_ACTIVE for
// at least this long; booty-shake specifically uses its own, shorter floor
// (a shake reads fine even in a moderately short drop; a full sustained
// reversal needs more runway to look intentional rather than twitchy).
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_REVERSAL = 6000;
constexpr uint32_t MUSIC_MOTOR_DROP_PHRASE_MIN_ACTIVE_MS_FOR_SHAKE = 3000;
// Anti-repeat: the most-recently-used phrase's selection weight is
// multiplied by this factor (< 1.0) so the same phrase doesn't dominate
// every drop when other phrases are equally eligible -- "musical evidence
// must influence the choice" still dominates; this only tie-breaks.
constexpr float MUSIC_MOTOR_DROP_PHRASE_ANTIREPEAT_WEIGHT_MULTIPLIER = 0.4f;

// ============================================================================
// Revision 10.1 -- frozen-state regression fix. Physical testing found
// SUSTAINED_DRIVE could become permanently stuck at M0 with the phrase
// sequencer repeatedly requesting movement that never actually applied
// (root cause: a direction-change comparison bug in
// advanceDropPhraseStepSequencer() -- see the .cpp's own comment on that
// function and the Revision 10.1 report for the full trace). This section
// is the defense-in-depth invariant/recovery layer added ON TOP OF the
// real fix -- it should see zero violations in normal operation.
// ============================================================================

// Grace period before a "SUSTAINED_DRIVE wants to move but is stopped
// outside any legitimate transition" observation is treated as a genuine
// invariant violation rather than ordinary one-tick command propagation
// (a real DECEL+COAST cycle normally resolves in
// MUSIC_MOTOR_DROP_PHRASE_DECEL_MS + MUSIC_MOTOR_REVERSE_COAST_MS =
// ~340ms; this grace is generous relative to that, not a hidden timeout
// that masks a bug -- it exists only to avoid false-positiving on the
// single tick where a legitimate transition is still in flight).
constexpr uint32_t MUSIC_MOTOR_SUSTAINED_DRIVE_INVARIANT_GRACE_MS = 500;

// Decision-tick interval -- reuses the same cadence already validated for
// this exact motor/PWM setup (see DANCE_PWM_UPDATE_MS above).
constexpr uint32_t MUSIC_MOTOR_TICK_MS = 15;

// Diagnostic print rate while active -- independent of
// DANCE_DIAG_PRINT_INTERVAL_MS, kept within the requested ~5-10 lines/sec.
constexpr uint32_t MUSIC_MOTOR_DIAG_PRINT_INTERVAL_MS = 150;

// ---------------------------------------------------------------------------
// Revision 4 -- detailed decision diagnostics ('musicmotor debug on/off/
// status'). Purely a logging cadence knob: how long a REPEATED rejection
// reason (same category, same reason string) is suppressed before being
// reprinted, so a song section stuck rejecting the same way doesn't flood
// the serial monitor. Significant events (an actual state change: STARTED/
// REFRESHED/a band transition/a qualified strong hit/an action selection
// other than the default ordinary-beat accent) are NEVER subject to this --
// they always print immediately. This is a diagnostic-only constant; it
// does not affect motor behavior in any way.
// ---------------------------------------------------------------------------
constexpr uint32_t MUSIC_MOTOR_DEBUG_RATE_LIMIT_MS = 1000;

// ============================================================================
// Speaker hardware test (see include/SpeakerTest.h) -- temporary, isolated
// MAX98357A bring-up feature. Writes to the shared I2S_PORT/
// I2S_SPEAKER_DOUT_PIN above -- see include/SharedI2S.h.
// ============================================================================

// Test tone: 440Hz, low amplitude, 2s -- see SpeakerTest.cpp's
// nextSpeakerSample() for the ramp-envelope math.
constexpr float SPEAKER_TONE_FREQUENCY_HZ = 440.0f;
constexpr uint32_t SPEAKER_TONE_DURATION_MS = 2000;
// Requested 20-50ms ramp -- 30ms is the midpoint, avoids pops on start/end.
constexpr uint32_t SPEAKER_TONE_RAMP_MS = 30;
// ~7.5% of full scale (requested 5-10%) -- conservative for an 8ohm/0.5W
// speaker. Samples are generated at this 16-bit resolution, then
// left-shifted into the shared bus's 32-bit-per-slot I2S word (see
// include/SharedI2S.h).
constexpr float SPEAKER_TONE_AMPLITUDE_FRACTION = 0.075f;
constexpr int16_t SPEAKER_TONE_AMPLITUDE = (int16_t)(SPEAKER_TONE_AMPLITUDE_FRACTION * 32767.0f);

// Whether SpeakerTest plays a one-shot automatic demonstration tone this
// long after "Digital silence active" first prints. Defaulted OFF
// 2026-08-01 for the Stage S1 speaker bring-up work -- Sunny's default
// boot must stay 100% silent on the speaker (silence only, no automatic
// enable of any tone) until a human deliberately issues a serial tone
// command (Stage S2, e.g. 't' or 'speaker tone'). Flip to 1 to restore
// the old auto-demo behavior for comparison; when 0, the delay constant
// below is simply unused (harmless, not dead-stripped, kept for the
// commented-out behavior's own reference).
#define ENABLE_SPEAKER_AUTO_DEMO_TONE 0

// One-shot automatic demonstration tone, this long after "Digital silence
// active" first prints -- never repeats automatically afterward (see
// SPEAKER_TONE_DURATION_MS's own "no auto-repeat" requirement); only the
// 't' serial command can trigger it again. Only takes effect if
// ENABLE_SPEAKER_AUTO_DEMO_TONE is 1 (see above).
constexpr uint32_t SPEAKER_AUTO_DEMO_DELAY_MS = 5000;

// --- Temporary alternate test mode: 440Hz square wave, 's' command (see
// SpeakerTest.cpp's nextSpeakerSample()) -- reuses SPEAKER_TONE_FREQUENCY_HZ/
// SPEAKER_TONE_DURATION_MS/SPEAKER_TONE_RAMP_MS above, only the waveform
// shape and amplitude differ.
constexpr float SPEAKER_SQUARE_AMPLITUDE_FRACTION = 0.05f;  // requested ~5%
constexpr int16_t SPEAKER_SQUARE_AMPLITUDE = (int16_t)(SPEAKER_SQUARE_AMPLITUDE_FRACTION * 32767.0f);

// --- Temporary diagnostics (see SpeakerTest.cpp) -- number of
// feedSpeakerChunk() calls, starting from each tone trigger, to trace in
// detail (requested/written bytes, any error). The very first traced call
// also prints its first 10 generated samples and first 5 packed stereo
// frames in hex.
constexpr int SPEAKER_DIAG_CALL_COUNT = 10;

// Frames (L+R pairs) written to the I2S TX DMA buffer per updateSpeakerTest()
// call -- deliberately small and written every loop() iteration
// (non-blocking, ticks_to_wait=0) rather than time-paced, so the DMA's own
// natural drain rate paces actual playback; tx_desc_auto_clear covers any
// gap if loop() is ever briefly too slow to keep up.
constexpr int SPEAKER_CHUNK_FRAMES = 64;

// ============================================================================
// Speaker diagnostic suite -- human-audible verification commands beyond the
// original 't'/'s' tones (see SpeakerTest.cpp). All share the same 30ms
// fade-in/out convention as the original tones unless noted, to avoid pops.
// ============================================================================

// --- 'low' / 'mid' / 'high': fixed single-tone sine tests at ~10% ---
constexpr float SPEAKER_LOW_FREQUENCY_HZ = 150.0f;
constexpr float SPEAKER_MID_FREQUENCY_HZ = 440.0f;
constexpr float SPEAKER_HIGH_FREQUENCY_HZ = 1500.0f;
constexpr uint32_t SPEAKER_LMH_DURATION_MS = 2000;
constexpr uint32_t SPEAKER_LMH_RAMP_MS = 30;
constexpr float SPEAKER_LMH_AMPLITUDE_FRACTION = 0.10f;

// --- 'sweep': logarithmic (exponential) chirp, 150Hz -> 3000Hz over 4s ---
constexpr float SPEAKER_SWEEP_START_HZ = 150.0f;
constexpr float SPEAKER_SWEEP_END_HZ = 3000.0f;
constexpr uint32_t SPEAKER_SWEEP_DURATION_MS = 4000;
constexpr uint32_t SPEAKER_SWEEP_RAMP_MS = 30;
constexpr float SPEAKER_SWEEP_AMPLITUDE_FRACTION = 0.10f;

// --- 'melody': C5 E5 G5 C6, 350ms/note + 100ms gap, sequence played twice ---
constexpr float MELODY_NOTE_C5_HZ = 523.25f;
constexpr float MELODY_NOTE_E5_HZ = 659.25f;
constexpr float MELODY_NOTE_G5_HZ = 783.99f;
constexpr float MELODY_NOTE_C6_HZ = 1046.50f;
constexpr uint32_t MELODY_NOTE_DURATION_MS = 350;
constexpr uint32_t MELODY_GAP_MS = 100;
constexpr uint32_t MELODY_NOTE_RAMP_MS = 15;  // shorter than 30ms -- notes are short, avoids an all-ramp note
constexpr float MELODY_AMPLITUDE_FRACTION = 0.10f;
// One repeat's span: 4 notes x (note+gap) = 4 * (350+100) = 1800ms; two repeats = 3600ms.
constexpr uint32_t MELODY_REPEAT_SPAN_MS = (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS) * 4;
constexpr uint32_t MELODY_TOTAL_DURATION_MS = MELODY_REPEAT_SPAN_MS * 2;

// --- 'beep': 1000Hz, 150ms on / 150ms off, 5 repeats ---
constexpr float BEEP_FREQUENCY_HZ = 1000.0f;
constexpr uint32_t BEEP_ON_MS = 150;
constexpr uint32_t BEEP_OFF_MS = 150;
constexpr int BEEP_REPEAT_COUNT = 5;
constexpr uint32_t BEEP_RAMP_MS = 10;  // shorter than 30ms -- each ON segment is only 150ms
constexpr float BEEP_AMPLITUDE_FRACTION = 0.10f;
constexpr uint32_t BEEP_TOTAL_DURATION_MS = (BEEP_ON_MS + BEEP_OFF_MS) * BEEP_REPEAT_COUNT;

// --- 'noise': white noise, 1s, very low amplitude ---
constexpr uint32_t NOISE_DURATION_MS = 1000;
constexpr uint32_t NOISE_RAMP_MS = 30;
constexpr float NOISE_AMPLITUDE_FRACTION = 0.05f;

// --- 'loud': temporary diagnostic only -- HARD CAP at 20% amplitude, never
// to be exceeded. Short duration (500ms) specifically because it is louder
// than every other test here.
constexpr float LOUD_FREQUENCY_HZ = 1000.0f;
constexpr uint32_t LOUD_DURATION_MS = 500;
constexpr uint32_t LOUD_RAMP_MS = 30;
constexpr float LOUD_AMPLITUDE_FRACTION = 0.20f;

// --- 'speaker tone': the deliberate, serial-only Stage S2 bring-up test
// (see docs/SPEAKER_BRINGUP_PLAN.md) -- the first sound a human should
// ever trigger on real hardware, after Stage S0 preflight is complete.
// Deliberately its own constants, never the 'loud' path above: 440Hz
// (a neutral, easily-recognized reference tone, matching this file's
// other tone tests), 300ms (within the requested 250-500ms window --
// long enough to be clearly audible, short enough that a startled human
// can react before it ends), 20ms ramp (matches this file's established
// short-segment convention, e.g. BEEP_RAMP_MS, to avoid a click on
// start/stop without eating too much of a 300ms window), and 5%
// amplitude -- deliberately lower than the standard 7.5% 't'/'s' tests
// and far below 'loud's 20% cap, since gain/volume have not yet been
// physically confirmed at all when this is first used. Mono content
// (like every other test here): the same sample is duplicated into both
// I2S stereo slots by feedSpeakerChunk(), there is no separate L/R
// signal. Sample rate is not configurable per-test -- it is always
// I2S_SAMPLE_RATE (the one shared full-duplex bus's fixed rate).
constexpr float SPEAKER_BRINGUP_TONE_FREQUENCY_HZ = 440.0f;
constexpr uint32_t SPEAKER_BRINGUP_TONE_DURATION_MS = 300;
constexpr uint32_t SPEAKER_BRINGUP_TONE_RAMP_MS = 20;
constexpr float SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION = 0.05f;

// --- Procedural music player: 'music1'-'music4' (see SpeakerTest.cpp's
// Note/Song/songSample()) -- shared amplitude and per-note attack/release
// ramp for every song (the ramp is additionally capped at 40% of each
// note's own duration in songSample(), so short eighth notes still have an
// audible sustained portion).
constexpr float MUSIC_AMPLITUDE_FRACTION = 0.10f;
constexpr uint32_t MUSIC_NOTE_RAMP_MS = 15;

// --- 'speaker t'/'speaker 1'/'speaker 2'/'speaker 3'/'speaker stop'/
// 'speaker v'/'speaker +'/'speaker -'/'speaker h': the physical-bring-up
// test bench requested for the MAX98357A gain/volume sweep. Namespaced
// under the existing 'speaker' word command (see Controls.cpp's
// dispatchSpeakerCommand()) rather than given bare single-char tokens --
// 't'/'s'/'v'/'h'/'+'/'-' are ALL already reserved elsewhere in this
// firmware (t/s = the original speaker sine/square tests, v = printAudioVisualState,
// h = general help, +/- = LED brightness; '1'/'2'/'3' are immediate, no-Enter
// bytes in main.cpp's pollSerialDispatcher bound to real MOTOR tests --
// IDLE_SWAY/priority-test/breakaway-test -- via ENABLE_MOTOR_BEHAVIOR_TEST).
// Repurposing any of those would either break an existing verified control
// or, for '1'/'2'/'3', risk firing a physical motor test instead of (or as
// well as) a tone. The 'speaker' prefix is Enter-terminated and only
// matched once a line is already pending, so none of this can collide with
// main.cpp's reserved immediate bytes -- see that file's pollSerialDispatcher().
constexpr float SPEAKER_BENCH_TONE_RAMP_MS = 20;  // matches this file's established short-segment convention
struct SpeakerBenchPreset {
  float frequencyHz;
  uint32_t durationMs;
};
// Index order matches SpeakerTest.cpp's startSpeakerBenchT()/1()/2()/3()
// exactly: [0]='speaker t', [1]='speaker 1', [2]='speaker 2', [3]='speaker 3'.
constexpr SpeakerBenchPreset SPEAKER_BENCH_PRESETS[] = {
    {440.0f, 750},
    {220.0f, 750},
    {440.0f, 750},
    {880.0f, 500},
};
constexpr uint8_t SPEAKER_BENCH_PRESET_COUNT = sizeof(SPEAKER_BENCH_PRESETS) / sizeof(SPEAKER_BENCH_PRESETS[0]);

// --- V1.1: finalized normal-use volume ladder (see
// docs/current/SPEAKER.md "Physically observed usable range"). Supersedes
// the original Stage S1/S2 bring-up-era 2%-25% ladder that used to live
// here -- that ladder existed only because gain/volume had not yet been
// physically confirmed AT ALL on real hardware. Physical testing since,
// with the current 40mm/4ohm/3W speaker, found roughly 35%-100% digital
// amplitude is the actually useful range (below ~35% not useful; 50-100%
// clearly intelligible; 100% physically validated and selectable, with the
// best observed signal-to-noise ratio). 'speaker t'/'1'/'2'/'3'/'sweep'/
// 'melody'/'chord'/'lowmidhigh'/'speechtest'/'musictest' all read this SAME
// live ladder (see SpeakerTest.cpp) -- one volume model for normal speaker
// output, not two. 'speaker noise' stays independently capped
// (SPEAKER_BENCH_NOISE_MAX_AMPLITUDE_FRACTION below); 'speaker fmt1'/'fmt2'/
// 'fmt3' and 'speaker tone' (Stage S2 bring-up) stay fixed at their own
// conservative constants (SPEAKER_FMT_DIAG_AMPLITUDE_FRACTION/
// SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION above) -- deliberately NOT tied to
// this ladder, so those diagnostics remain a fixed, repeatable reference
// regardless of the user's current normal volume setting.
constexpr float SPEAKER_VOLUME_STEPS_FRACTION[] = {0.35f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f, 1.00f};
constexpr uint8_t SPEAKER_VOLUME_STEP_COUNT =
    sizeof(SPEAKER_VOLUME_STEPS_FRACTION) / sizeof(SPEAKER_VOLUME_STEPS_FRACTION[0]);
// 70% -- below ~35% wasn't physically useful, 50-100% was clearly
// intelligible, 70% is a reasonable normal-use midpoint, and 100% remains
// selectable when it's specifically wanted. Not persisted across reboot --
// no settings/EEPROM system exists in this firmware yet; every boot starts
// at this default.
constexpr uint8_t SPEAKER_VOLUME_DEFAULT_INDEX = 3;

// Consecutive i2s_write() failures required before '[SPEAKER] WARNING:
// repeated write failures' escalates beyond the single per-occurrence
// '[SPEAKER] I2S write error' line -- avoids flooding the serial monitor
// if the bus fails continuously (updateSpeakerTest() runs every loop()).
constexpr uint32_t SPEAKER_WRITE_REPEATED_FAILURE_THRESHOLD = 10;

// --- 'speaker fmt1'/'fmt2'/'fmt3'/'fmtstatus': Stage S3 buzz/distortion
// diagnostic, added to empirically A/B which 32-bit-slot sample packing the
// physically-connected MAX98357A actually reproduces cleanly, after a full
// source-level review of SharedI2S.cpp/SpeakerTest.cpp's sample formatting
// found no mono/stereo, slot-order, sign, overflow, phase-continuity, or
// double-scaling defect (see the task report for the full checklist
// result) -- the shared bus is hard-configured at 32 bits-per-slot for
// both RX and TX (changing it would also change the microphone's verified
// RX format, out of scope here), so what remains genuinely open is only
// which PORTION of that 32-bit slot the amplifier expects real data in.
// Fixed at a conservative 2% amplitude and 440Hz/750ms across all three,
// deliberately independent of the 'speaker v'/'+'/'-' bench volume ladder,
// so the three formats are a fair, consistent A/B comparison. See
// fmtDiagSample() in SpeakerTest.cpp for the exact per-format bit layout.
constexpr float SPEAKER_FMT_DIAG_FREQUENCY_HZ = 440.0f;
constexpr uint32_t SPEAKER_FMT_DIAG_DURATION_MS = 750;
constexpr uint32_t SPEAKER_FMT_DIAG_RAMP_MS = 20;  // matches this file's established short-segment convention
constexpr float SPEAKER_FMT_DIAG_AMPLITUDE_FRACTION = 0.02f;

// --- 'speaker sweep'/'speaker melody'/'speaker chord'/'speaker noise':
// multi-tone speaker bring-up tests for judging the MAX98357A + a real
// speaker load more realistically than a single 440Hz tone -- see
// src/SpeakerTest.cpp's sweepSample()/boundedNoteSequenceSample()/
// noiseSample() (all pre-existing generic engines, reused unchanged here)
// and the SPEAKER_BENCH_MELODY_NOTES/SPEAKER_BENCH_CHORD_NOTES note tables
// in that file. Sweep/melody/chord all use the CURRENT normal speaker
// volume (see 'speaker volume'/'v'/'+'/'-' above, SPEAKER_VOLUME_STEPS_FRACTION),
// read live at the moment each test starts -- not a separate fixed amplitude.
// Noise is the one exception, deliberately capped independent of that
// volume (see SPEAKER_BENCH_NOISE_MAX_AMPLITUDE_FRACTION below).

// 'speaker sweep': same 150Hz->3000Hz range as the existing bare 'sweep'
// command (SPEAKER_SWEEP_START_HZ/END_HZ above, reused directly -- no new
// constants needed for the endpoints), but a longer ~6s duration ("about 6
// seconds", vs the original's fixed 4s) and the file's established 20ms
// short-segment ramp (vs the original's 30ms) -- both deliberately
// independent knobs so the original bare 'sweep' test is unaffected.
constexpr uint32_t SPEAKER_BENCH_SWEEP_DURATION_MS = 6000;
constexpr uint32_t SPEAKER_BENCH_SWEEP_RAMP_MS = 20;

// Per-note fade for 'speaker melody'/'speaker chord' (boundedNoteSequenceSample()
// caps this at 40% of each note's own duration, same convention as the
// existing music1-4 engine's MUSIC_NOTE_RAMP_MS) -- shorter than the
// standalone-tone 20ms convention since some notes here are only 40ms gaps.
constexpr uint32_t SPEAKER_BENCH_NOTE_RAMP_MS = 15;

// 'speaker noise': 2s duration (vs the original bare 'noise' command's 1s),
// same 30ms ramp convention as the original (NOISE_RAMP_MS, reused
// directly). Amplitude is min(current bench volume, this cap) -- "maximum
// 10% regardless of current bench volume" -- computed at call time in
// startSpeakerBenchNoise(), never exceeding this ceiling even if the bench
// volume ladder is set to its 25% maximum, since this test exists
// specifically to reveal hiss/buzz/rattling, not to be a loudness test.
constexpr uint32_t SPEAKER_BENCH_NOISE_DURATION_MS = 2000;
constexpr float SPEAKER_BENCH_NOISE_MAX_AMPLITUDE_FRACTION = 0.10f;

// --- 'speaker voltest'/'speaker volquick'/'speaker volstop'/
// 'speaker volstatus': automatic digital-amplitude ladder diagnostic -- see
// src/SpeakerTest.cpp's VOLTEST_FULL_STEPS/VOLTEST_QUICK_STEPS for the full
// per-level sequence (three sine tones + gaps + a melody excerpt for
// 'voltest'; two tones + gaps for the shorter 'volquick'). Both reuse the
// existing sine/note-sequence engines and startTest() scheduler completely
// unchanged -- no second I2S write path, no new pin/clock/format. Every
// level change waits for the previous level's audio to reach genuine
// digital silence first (see armVolLadderStep()'s own comment) before the
// next amplitude is armed -- never an instant jump mid-tone.
constexpr float VOLTEST_FULL_LEVELS_FRACTION[] = {0.02f, 0.05f, 0.08f, 0.12f, 0.18f,
                                                    0.25f, 0.35f, 0.50f, 0.65f, 0.80f, 1.00f};
constexpr uint8_t VOLTEST_FULL_LEVEL_COUNT =
    sizeof(VOLTEST_FULL_LEVELS_FRACTION) / sizeof(VOLTEST_FULL_LEVELS_FRACTION[0]);
constexpr float VOLTEST_QUICK_LEVELS_FRACTION[] = {0.12f, 0.25f, 0.50f, 0.75f, 1.00f};
constexpr uint8_t VOLTEST_QUICK_LEVEL_COUNT =
    sizeof(VOLTEST_QUICK_LEVELS_FRACTION) / sizeof(VOLTEST_QUICK_LEVELS_FRACTION[0]);
// Fade in/out for every sine step in the ladder -- same 20ms convention this
// file already establishes for short segments (SPEAKER_BENCH_TONE_RAMP_MS/
// SPEAKER_FMT_DIAG_RAMP_MS), named separately so the ladder's own timing can
// be retuned independently of those unrelated tests.
constexpr uint32_t VOLTEST_TONE_RAMP_MS = 20;
// First N notes of SPEAKER_BENCH_MELODY_NOTES (SpeakerTest.cpp) used as the
// ladder's "short segment of the existing diagnostic melody" -- the first 7
// notes total 1840ms, within the requested ~1.5-2s window. Reuses the SAME
// table 'speaker melody' already plays from -- not a new composition.
constexpr uint8_t VOLTEST_MELODY_EXCERPT_NOTE_COUNT = 7;
// Warning-print thresholds (see beginVolLadderLevel() in SpeakerTest.cpp):
// HIGH OUTPUT TEST prints for every level >= this fraction; the 80%/100%
// warnings print only at exactly those two specific levels.
constexpr float VOLTEST_HIGH_OUTPUT_THRESHOLD_FRACTION = 0.50f;
constexpr float VOLTEST_EIGHTY_PERCENT_WARNING_FRACTION = 0.80f;
constexpr float VOLTEST_FULL_SCALE_WARNING_FRACTION = 1.00f;

// ============================================================================
// V1.1 buzz/noise isolation and realistic-content diagnostics (see
// docs/current/SPEAKER.md). All reuse this file's existing generic engines
// (boundedNoteSequenceSample() via the BenchNote table, same as 'speaker
// melody'/'chord') -- no new I2S write path, no pin/clock/format changes.
// ============================================================================

// --- 'speaker silencecheck'/'speaker carriercheck': force continuous
// digital zero (no synthesized tone) and report state. Both are actually
// the SAME underlying signal (feedSpeakerChunk() already writes an
// all-zero buffer whenever phase==SILENCE) -- carriercheck additionally
// prints the live I2S clock/pin-routing diagnostics (i2s_get_clk(),
// GPIO16 routing) so the two states ("clocks running + zeros" vs "actual
// audio content") are explicitly distinguishable in the serial log, not
// just implied. Neither has a fixed duration -- both run until 'speaker
// stop'/'stopmusic'/'k' explicitly ends them (user-controlled period).
constexpr uint32_t SPEAKER_SILENCECHECK_STATUS_INTERVAL_MS = 5000;  // periodic "still active" reminder

// --- 'speaker lowmidhigh': 150/440/1500Hz in sequence, equal duration and
// fades, at the current normal speaker volume -- unlike the original fixed-
// 10%-amplitude 'low'/'mid'/'high' commands (SPEAKER_LMH_* above, preserved
// unchanged), this is the volume-ladder-aware version requested for V1.1.
// Reuses the SAME 150/440/1500Hz frequencies as SPEAKER_LOW/MID/HIGH_FREQUENCY_HZ
// above (no new frequency constants) and the bounded note-sequence engine
// 'speaker melody'/'chord' already use.
// Fades come from the shared engine's own SPEAKER_BENCH_NOTE_RAMP_MS (same
// as every note in 'speaker melody'/'chord') -- no separate ramp constant
// needed since all three notes here go through that one engine.
constexpr uint32_t SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS = 1200;
constexpr uint32_t SPEAKER_LOWMIDHIGH_GAP_MS = 150;

// --- 'speaker speechtest': an ORIGINAL synthetic speech-like diagnostic --
// NOT copyrighted audio, not a recording. This engine has no polyphony/
// formant synthesis (see this file's top-of-file note on why -- one sine
// generator, reused via the existing bounded note-sequence engine), so
// "formant-like" here means an arpeggiated, rapidly-changing approximation:
// short "syllable" notes across the fundamental-voice frequency range
// (~110-260Hz, typical adult speech F0) with syllable-like durations and
// brief gaps, at the current normal speaker volume. ~10s total.
constexpr uint32_t SPEAKER_SPEECHTEST_TARGET_DURATION_MS = 10000;

// --- 'speaker musictest': an ORIGINAL short musical diagnostic -- NOT a
// copyrighted melody. Low/mid/high notes, short gaps ("rests"), and
// transient-like short-attack notes mixed with longer sustained ones, at
// the current normal speaker volume. ~10-15s total.
constexpr uint32_t SPEAKER_MUSICTEST_TARGET_DURATION_MS = 12000;

// --- 'speaker isolate on'/'off'/'status': optional noise-isolation mode --
// motor commanded stopped once (MotorDriver's own motorStop(), the same
// exported API every other behavior layer uses -- see AGENTS.md's
// single-owner table) and LEDs fully muted via Controls.h's existing
// isMuted()/setMuted() (the SAME mechanism MotorPowerGuard's FULL_MUTE
// strategy already uses -- not a new LED-power mechanism), for isolating
// whether residual buzz correlates with motor/LED activity on the shared
// 5V rail (see docs/current/POWER.md). Diagnostic-only; restores the prior
// mute state on 'speaker isolate off'. Never activated automatically by any
// other command. Best-effort, not a hard interlock: it does not add itself
// to isAnyMotorDiagnosticActive() (see ExpressiveMotion.h), so an
// independently-active motor behavior (e.g. MusicMotorController) can still
// re-engage the motor during the isolate window -- see SpeakerTest.cpp's
// speakerIsolateOn() for the full caveat. Adding a true interlock would mean
// changing the motor mutual-exclusion architecture, out of scope for this
// diagnostic-only feature per this sprint's explicit "do not redesign the
// motor... architecture" constraint.
