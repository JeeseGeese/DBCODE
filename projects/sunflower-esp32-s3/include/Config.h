#pragma once

// Centralized tunables for the sunflower LED/audio firmware. Grouped by
// subsystem; each nontrivial constant documents what raising/lowering it
// does so tuning doesn't require reading the implementation.

#include <Arduino.h>

// ============================================================================
// Hardware pin map (verified)
// ============================================================================
#define LED_PIN 4
#define NUM_LEDS 58

// ============================================================================
// 42-LED row assembly metadata
// ============================================================================
// A 42-LED WS2812 assembly (3 daisy-chained rows: 10 + 10 + 22) has been
// physically connected on the same GPIO4/strip object as the existing
// NUM_LEDS=58 strip above. It has been observed working correctly with the
// current firmware as-is -- existing modes, effects, brightness, and mute
// behavior all display properly on it with no code changes, no indexing
// failures, and no corrupted output. NUM_LEDS is therefore left at 58
// (unchanged) rather than being replaced with 42: whether the physical
// strip is actually 42, 58, or something else connected downstream has not
// been independently confirmed from the repository, and driving more
// pixels than are physically present is standard, harmless WS2812 behavior
// (the extra data simply has no LED left in the chain to land on). These
// constants are metadata only, describing the first 42 logical pixels of
// the existing strip -- they do not change NUM_LEDS, do not create a
// second NeoPixel object, and do not alter any effect's output by
// themselves.
struct LedRegion {
  uint16_t start;
  uint16_t count;
};

constexpr LedRegion LED_ROW_1{0, 10};
constexpr LedRegion LED_ROW_2{10, 10};
constexpr LedRegion LED_ROW_3{20, 22};
constexpr uint16_t PHYSICAL_LED_COUNT = 42;

static_assert(LED_ROW_1.start + LED_ROW_1.count == LED_ROW_2.start, "Row 1 must end exactly where Row 2 begins");
static_assert(LED_ROW_2.start + LED_ROW_2.count == LED_ROW_3.start, "Row 2 must end exactly where Row 3 begins");
static_assert(LED_ROW_3.start + LED_ROW_3.count == PHYSICAL_LED_COUNT, "Row 3 must end exactly at PHYSICAL_LED_COUNT");
static_assert(PHYSICAL_LED_COUNT <= NUM_LEDS,
              "PHYSICAL_LED_COUNT must fit within NUM_LEDS -- row indices address the existing strip object");

#define BUTTON_MODE_PIN 10
#define BUTTON_MUTE_PIN 11
#define BUTTON_BRIGHTNESS_PIN 17
#define BUTTON4_PIN 5

#define I2S_PORT I2S_NUM_0
#define I2S_BCLK_PIN 6
#define I2S_WS_PIN 7
#define I2S_DIN_PIN 15
#define I2S_SAMPLE_RATE 16000

// ============================================================================
// Button timing
// ============================================================================
#define DEBOUNCE_DELAY_MS 40

// Button4-specific debounce, separate from DEBOUNCE_DELAY_MS (which the
// other three buttons still use). Raised from 40ms after physical testing
// suggested Button4 needed more settling margin; scoped to this one
// button so Mode/Mute/Brightness timing is untouched.
//
// Button4 is now a plain immediate toggle (audio overlay ON/OFF) on the
// debounced press edge -- no click-count/hold timing constant needed here
// anymore; the single/double-click window this used to gate has been
// removed along with that architecture.
constexpr uint32_t BUTTON4_DEBOUNCE_MS = 75;

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
// disabled by default (ExpressiveMotionMode::DISABLED at boot). All
// timings deliberately conservative for initial physical validation; not
// yet tuned against the actual mechanism/belt.
// ============================================================================

// --- IDLE_ALIVE: gentle randomized idle behavior ---
constexpr uint32_t MOTION_PULSE_MIN_MS = 100;         // one short movement, lower bound
constexpr uint32_t MOTION_PULSE_MAX_MS = 220;         // one short movement, upper bound -- well under the 2000ms safeguard
constexpr uint32_t MOTION_REST_MIN_MS = 900;          // minimum rest between movements
constexpr uint32_t MOTION_REST_MAX_MS = 3000;         // typical maximum rest between movements
constexpr uint32_t MOTION_LONG_REST_MIN_MS = 4000;    // occasional longer pause, lower bound
constexpr uint32_t MOTION_LONG_REST_MAX_MS = 7000;    // occasional longer pause, upper bound
constexpr float MOTION_LONG_REST_CHANCE = 0.15f;      // fraction of idle cycles that use the long-rest range instead
constexpr float MOTION_CURIOUS_CHANCE = 0.20f;        // fraction of idle cycles that do a "curious" second pulse
constexpr uint32_t MOTION_INTRA_PULSE_STOP_MS = 200;  // brief full stop between a curious double-movement's two pulses
constexpr uint8_t MOTION_MAX_CONSECUTIVE_SAME_DIR = 2; // never more than this many same-direction pulses in a row

// --- AUDIO_REACTIVE: activity bands (hysteresis) + cooldowns ---
// Based on AudioFeatures.envelope (already attack/release-smoothed, 0..1)
// -- never raw per-sample audio. Enter/exit thresholds differ so the band
// doesn't chatter right at a boundary.
constexpr float MOTION_AUDIO_ACTIVE_ENTER = 0.20f;
constexpr float MOTION_AUDIO_ACTIVE_EXIT = 0.12f;
constexpr float MOTION_AUDIO_STRONG_ENTER = 0.55f;
constexpr float MOTION_AUDIO_STRONG_EXIT = 0.40f;
constexpr uint32_t MOTION_AUDIO_ACTIVE_COOLDOWN_MS = 700;  // min gap between ordinary audio-triggered movements
constexpr uint32_t MOTION_AUDIO_STRONG_COOLDOWN_MS = 1400; // min gap between STRONG (two-pulse) reactions

// --- Motion-motion LED brightness: reuses MotorPowerGuard's existing
// DIM_DURING_MOTION test-level selection (see MotorPowerGuard.h) rather
// than a separate constant -- "begin conservatively at the already
// validated selected motion brightness" per the development plan.
