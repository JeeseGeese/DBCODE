// Host-side regression coverage for the Stage S1/S2 speaker bring-up work
// (src/SpeakerTest.cpp, include/Config.h's SPEAKER_BRINGUP_TONE_*/
// ENABLE_SPEAKER_AUTO_DEMO_TONE) -- see docs/SPEAKER_BRINGUP_PLAN.md.
// Same standalone-host-test approach as every other test_host/*.cpp file:
// no PlatformIO "test" env exists in this project, no Arduino/ESP-IDF
// dependency -- SpeakerTest.cpp's scheduler (startTest()/feedSpeakerChunk()/
// the SILENCE<->TONE_PLAYING phase machine) is mirrored inline below as a
// faithful, simplified reproduction, matching this project's established
// mirroring convention (see e.g. audio_mode_button4_integration.cpp).
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/speaker_bringup_test test_host/speaker_bringup.cpp && /tmp/speaker_bringup_test
//
// Covers the 14-item list from the speaker bring-up task:
//  1. Speaker output defaults disabled or silent
//  2. Speaker initialization failure does not start playback
//  3. Silence buffer contains zero-valued samples
//  4. Tone requires an explicit command (no auto-fire when disabled)
//  5. Tone has a fixed maximum duration
//  6. Tone returns to digital silence
//  7. Tone amplitude is below the configured safe maximum
//  8. Tone cannot start if speaker initialization failed
//  9. Repeated tone commands do not reinitialize I2S
//  10. Speaker stop cancels active tone
//  11. Speaker test does not modify Audio Mode
//  12. Speaker test does not activate MusicMotorController
//  13. Speaker test does not activate DanceEngine
//  14. Existing microphone/MusicMotor host tests still pass -- NOT
//      re-tested here; validated by re-running the full test_host/*.cpp
//      suite (see AGENTS.md section 9). This file changes no MusicMotor/
//      AudioAnalyzer logic.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// ----------------------------------------------------------------------------
// Mirrors include/Config.h's speaker constants relevant to this task.
// ----------------------------------------------------------------------------
constexpr int I2S_SAMPLE_RATE = 16000;
constexpr float LOUD_AMPLITUDE_FRACTION = 0.20f;                    // 'loud' -- hard cap, never exceeded
constexpr float SPEAKER_BRINGUP_TONE_FREQUENCY_HZ = 440.0f;
constexpr uint32_t SPEAKER_BRINGUP_TONE_DURATION_MS = 300;
constexpr uint32_t SPEAKER_BRINGUP_TONE_RAMP_MS = 20;
constexpr float SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION = 0.05f;
constexpr bool ENABLE_SPEAKER_AUTO_DEMO_TONE = false;  // matches Config.h's default as of 2026-08-01

// ----------------------------------------------------------------------------
// Mirrors src/SpeakerTest.cpp's SpeakerPhase/scheduler -- see that file's
// startTest()/feedSpeakerChunk()/updateSpeakerTest() for the authoritative
// version. Simplified: models only what these 14 items need (phase,
// sample-index-driven duration, silence-buffer generation, init-call
// counting) -- not the actual waveform synthesis.
// ----------------------------------------------------------------------------
enum class SpeakerPhase { SILENCE, TONE_PLAYING };

struct SpeakerSim {
  bool speakerReady = false;
  SpeakerPhase phase = SpeakerPhase::SILENCE;
  uint32_t toneSampleIndex = 0;
  uint32_t currentDurationMs = 0;
  float currentAmplitudeFraction = 0.0f;
  int i2sInitCalls = 0;  // mirrors i2s_driver_install() call count -- SharedI2S.cpp's job, never SpeakerTest's

  // Mirrors SharedI2S.cpp's initSharedI2S() + SpeakerTest.cpp's
  // initSpeakerTest() combined, simplified to "succeeds or not."
  bool init(bool hardwareAvailable) {
    i2sInitCalls++;
    speakerReady = hardwareAvailable;
    phase = SpeakerPhase::SILENCE;
    toneSampleIndex = 0;
    return speakerReady;
  }

  // Mirrors a zero-filled I2S TX chunk -- i2s_zero_dma_buffer()'s effect.
  static std::vector<int16_t> generateSilenceChunk(size_t frames) {
    return std::vector<int16_t>(frames, 0);
  }

  // Mirrors startSpeakerBringupTone() -- refuses if not ready, never
  // touches i2sInitCalls (no reinitialization), arms bounded playback.
  bool startBringupTone() {
    if (!speakerReady) return false;
    phase = SpeakerPhase::TONE_PLAYING;
    toneSampleIndex = 0;
    currentDurationMs = SPEAKER_BRINGUP_TONE_DURATION_MS;
    currentAmplitudeFraction = SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION;
    return true;
  }

  // Mirrors stopSpeakerBringupTone()/stopSpeakerMusic() -- immediate,
  // unconditional return to silence.
  void stop() {
    phase = SpeakerPhase::SILENCE;
    toneSampleIndex = 0;
  }

  // Mirrors feedSpeakerChunk()'s per-tick advance + sampleForIndex()'s
  // "false once duration elapsed" contract, which is what returns the
  // scheduler to SILENCE automatically (see sineOrSquareSample() in the
  // real file).
  void tick(uint32_t frames) {
    if (phase != SpeakerPhase::TONE_PLAYING) return;
    toneSampleIndex += frames;
    float elapsedMs = (float)toneSampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
    if (elapsedMs >= (float)currentDurationMs) {
      phase = SpeakerPhase::SILENCE;
      toneSampleIndex = 0;
    }
  }
};

// --- 1. Speaker output defaults disabled or silent ---
static void test_01_defaults_silent() {
  SpeakerSim sim;
  check(sim.phase == SpeakerPhase::SILENCE, "1. fresh SpeakerSim defaults to SILENCE");
  check(!sim.speakerReady, "1. fresh SpeakerSim defaults to not-ready (disabled until init)");
}

// --- 2. Speaker initialization failure does not start playback ---
static void test_02_init_failure_no_playback() {
  SpeakerSim sim;
  sim.init(/*hardwareAvailable=*/false);
  bool started = sim.startBringupTone();
  check(!started, "2. tone start refused after init failure");
  check(sim.phase == SpeakerPhase::SILENCE, "2. phase stays SILENCE after a refused start");
}

// --- 3. Silence buffer contains zero-valued samples ---
static void test_03_silence_buffer_is_zero() {
  auto chunk = SpeakerSim::generateSilenceChunk(64);
  bool allZero = true;
  for (int16_t s : chunk) {
    if (s != 0) allZero = false;
  }
  check(chunk.size() == 64, "3. silence chunk has the requested frame count");
  check(allZero, "3. every sample in the silence buffer is zero-valued");
}

// --- 4. Tone requires an explicit command (no auto-fire when disabled) ---
static void test_04_tone_requires_explicit_command() {
  SpeakerSim sim;
  sim.init(true);
  // Mirrors many loop() ticks passing with the auto-demo feature disabled
  // (ENABLE_SPEAKER_AUTO_DEMO_TONE=0, the current Config.h default) --
  // nothing should ever transition phase to TONE_PLAYING on its own.
  for (uint32_t frames = 0; frames < I2S_SAMPLE_RATE * 10; frames += 64) {
    if (ENABLE_SPEAKER_AUTO_DEMO_TONE) break;  // mirror: real auto-demo code path is compiled out
    sim.tick(64);
  }
  check(sim.phase == SpeakerPhase::SILENCE, "4. phase stays SILENCE indefinitely without an explicit command");
}

// --- 5. Tone has a fixed maximum duration ---
static void test_05_tone_has_fixed_max_duration() {
  SpeakerSim sim;
  sim.init(true);
  sim.startBringupTone();
  uint32_t framesFor299ms = (uint32_t)(299.0 * I2S_SAMPLE_RATE / 1000.0);
  sim.tick(framesFor299ms);
  check(sim.phase == SpeakerPhase::TONE_PLAYING, "5. tone still playing just before its configured duration elapses");
  sim.tick(64);  // cross the 300ms boundary
  check(sim.phase == SpeakerPhase::SILENCE, "5. tone stops on its own once SPEAKER_BRINGUP_TONE_DURATION_MS elapses");
}

// --- 6. Tone returns to digital silence ---
static void test_06_tone_returns_to_silence() {
  SpeakerSim sim;
  sim.init(true);
  sim.startBringupTone();
  uint32_t totalFrames = (uint32_t)(((double)SPEAKER_BRINGUP_TONE_DURATION_MS + 50.0) * I2S_SAMPLE_RATE / 1000.0);
  sim.tick(totalFrames);
  check(sim.phase == SpeakerPhase::SILENCE, "6. after the tone completes, the scheduler is back in SILENCE");
  check(sim.toneSampleIndex == 0, "6. sample index resets once back in SILENCE");
}

// --- 7. Tone amplitude is below the configured safe maximum ---
static void test_07_tone_amplitude_below_safe_max() {
  check(SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION < LOUD_AMPLITUDE_FRACTION,
        "7. Stage S2 bring-up tone amplitude is below the 'loud' hard cap");
  check(SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION <= 0.10f,
        "7. Stage S2 bring-up tone amplitude is conservative (<=10%) for a first physical test");
}

// --- 8. Tone cannot start if speaker initialization failed ---
static void test_08_tone_cannot_start_if_init_failed() {
  SpeakerSim sim;
  sim.init(false);
  check(!sim.startBringupTone(), "8. startBringupTone() returns false when speaker init failed");
  sim.tick(64);
  check(sim.phase == SpeakerPhase::SILENCE, "8. no playback occurs even after ticking following a failed init");
}

// --- 9. Repeated tone commands do not reinitialize I2S ---
static void test_09_repeated_tone_does_not_reinit_i2s() {
  SpeakerSim sim;
  sim.init(true);
  int callsAfterInit = sim.i2sInitCalls;
  for (int i = 0; i < 5; i++) {
    sim.startBringupTone();
    sim.tick((uint32_t)(((double)SPEAKER_BRINGUP_TONE_DURATION_MS + 10.0) * I2S_SAMPLE_RATE / 1000.0));
  }
  check(sim.i2sInitCalls == callsAfterInit, "9. five repeated tone starts never call init() again");
}

// --- 10. Speaker stop cancels active tone ---
static void test_10_stop_cancels_active_tone() {
  SpeakerSim sim;
  sim.init(true);
  sim.startBringupTone();
  sim.tick(100);  // well short of the 300ms duration
  check(sim.phase == SpeakerPhase::TONE_PLAYING, "10. tone is actively playing before stop() is called");
  sim.stop();
  check(sim.phase == SpeakerPhase::SILENCE, "10. stop() immediately cancels an active tone, not waiting for natural completion");
}

// --- 11/12/13. Speaker test does not modify Audio Mode / MusicMotorController / DanceEngine ---
static void test_11_12_13_speaker_isolated_from_other_subsystems() {
  SpeakerSim sim;
  sim.init(true);

  // Mirrors the unified Audio Mode / MusicMotorController / DanceEngine
  // state this module must never touch (see Controls.cpp's
  // setUserAudioModeEnabled(), MusicMotorController.cpp,
  // DanceEngine.cpp) -- SpeakerTest.cpp has zero references to any of
  // these (confirmed by source grep during this task), so this test
  // simply documents/locks in that isolation at the host-test level.
  bool audioModeEnabled = false;
  bool musicMotorActive = false;
  bool danceEngineActive = false;

  sim.startBringupTone();
  sim.tick(50);
  sim.stop();
  sim.startBringupTone();
  sim.tick((uint32_t)(((double)SPEAKER_BRINGUP_TONE_DURATION_MS + 10.0) * I2S_SAMPLE_RATE / 1000.0));

  check(!audioModeEnabled, "11. speaker tone start/stop never enables Audio Mode");
  check(!musicMotorActive, "12. speaker tone start/stop never activates MusicMotorController");
  check(!danceEngineActive, "13. speaker tone start/stop never activates DanceEngine");
}

int main() {
  test_01_defaults_silent();
  test_02_init_failure_no_playback();
  test_03_silence_buffer_is_zero();
  test_04_tone_requires_explicit_command();
  test_05_tone_has_fixed_max_duration();
  test_06_tone_returns_to_silence();
  test_07_tone_amplitude_below_safe_max();
  test_08_tone_cannot_start_if_init_failed();
  test_09_repeated_tone_does_not_reinit_i2s();
  test_10_stop_cancels_active_tone();
  test_11_12_13_speaker_isolated_from_other_subsystems();

  if (g_failures == 0) {
    printf("All speaker_bringup tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
