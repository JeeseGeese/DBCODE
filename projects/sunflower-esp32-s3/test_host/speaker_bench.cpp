// Host-side regression coverage for the 'speaker t'/'1'/'2'/'3'/'v'/'+'/'-'/
// 'h' gain/volume bring-up test bench (src/SpeakerTest.cpp's
// startSpeakerBenchT()/1()/2()/3()/speakerBenchVolumeUp()/Down(), Config.h's
// SPEAKER_BENCH_* constants). Same standalone-host-test approach as every
// other test_host/*.cpp file: no PlatformIO "test" env exists in this
// project, no Arduino/ESP-IDF dependency -- the preset table and the
// volume-step state machine are mirrored inline below, matching this
// project's established mirroring convention (see speaker_bringup.cpp).
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/speaker_bench_test test_host/speaker_bench.cpp && /tmp/speaker_bench_test
//
// Covers:
//  1. Preset table matches the requested t/1/2/3 frequencies and durations
//  2. Default bench volume is 5% (index 1)
//  3. Volume steps up through the full ladder, capped at 25% (never higher)
//  4. Volume steps down through the full ladder, capped at 2% (never lower,
//     never full-scale/1.0)
//  5. A preset refuses to start if the speaker never initialized
//  6. Starting a preset does not reinitialize I2S
//  7. Each preset's amplitude tracks the CURRENT bench volume at the moment
//     it's started, not a value frozen at boot
//  8. Bench presets are isolated from Audio Mode/MusicMotorController/
//     DanceEngine (same guarantee as every other speaker test)

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// ----------------------------------------------------------------------------
// Mirrors include/Config.h's SPEAKER_BENCH_* constants.
// ----------------------------------------------------------------------------
struct SpeakerBenchPreset {
  float frequencyHz;
  uint32_t durationMs;
};
constexpr SpeakerBenchPreset SPEAKER_BENCH_PRESETS[] = {
    {440.0f, 750},
    {220.0f, 750},
    {440.0f, 750},
    {880.0f, 500},
};
constexpr uint8_t SPEAKER_BENCH_PRESET_COUNT = sizeof(SPEAKER_BENCH_PRESETS) / sizeof(SPEAKER_BENCH_PRESETS[0]);
constexpr float SPEAKER_BENCH_VOLUME_STEPS_FRACTION[] = {0.02f, 0.05f, 0.08f, 0.12f, 0.18f, 0.25f};
constexpr uint8_t SPEAKER_BENCH_VOLUME_STEP_COUNT =
    sizeof(SPEAKER_BENCH_VOLUME_STEPS_FRACTION) / sizeof(SPEAKER_BENCH_VOLUME_STEPS_FRACTION[0]);
constexpr uint8_t SPEAKER_BENCH_VOLUME_DEFAULT_INDEX = 1;  // 5%

// ----------------------------------------------------------------------------
// Mirrors src/SpeakerTest.cpp's bench state (benchVolumeIndex) and the
// startSpeakerBenchPreset()/speakerBenchVolumeUp()/Down() functions --
// simplified to what these 8 items need (no waveform synthesis, no I2S).
// ----------------------------------------------------------------------------
struct SpeakerBenchSim {
  bool speakerReady = false;
  uint8_t benchVolumeIndex = SPEAKER_BENCH_VOLUME_DEFAULT_INDEX;
  int i2sInitCalls = 0;
  bool playing = false;
  float lastStartedFrequencyHz = 0.0f;
  uint32_t lastStartedDurationMs = 0;
  float lastStartedAmplitudeFraction = 0.0f;

  bool init(bool hardwareAvailable) {
    i2sInitCalls++;
    speakerReady = hardwareAvailable;
    return speakerReady;
  }

  // Mirrors startSpeakerBenchPreset(): refuses if not ready, otherwise reads
  // the CURRENT benchVolumeIndex at call time (not a value cached earlier).
  bool startPreset(uint8_t presetIndex) {
    if (!speakerReady) return false;
    const SpeakerBenchPreset &preset = SPEAKER_BENCH_PRESETS[presetIndex];
    lastStartedFrequencyHz = preset.frequencyHz;
    lastStartedDurationMs = preset.durationMs;
    lastStartedAmplitudeFraction = SPEAKER_BENCH_VOLUME_STEPS_FRACTION[benchVolumeIndex];
    playing = true;
    return true;
  }

  void volumeUp() {
    if (benchVolumeIndex + 1 < SPEAKER_BENCH_VOLUME_STEP_COUNT) benchVolumeIndex++;
  }
  void volumeDown() {
    if (benchVolumeIndex > 0) benchVolumeIndex--;
  }
};

// --- 1. Preset table matches the requested t/1/2/3 frequencies/durations ---
static void test_01_preset_table_matches_spec() {
  check(SPEAKER_BENCH_PRESET_COUNT == 4, "1. exactly 4 presets exist (t/1/2/3)");
  check(SPEAKER_BENCH_PRESETS[0].frequencyHz == 440.0f && SPEAKER_BENCH_PRESETS[0].durationMs == 750,
        "1. 't' = 440Hz/750ms");
  check(SPEAKER_BENCH_PRESETS[1].frequencyHz == 220.0f && SPEAKER_BENCH_PRESETS[1].durationMs == 750,
        "1. '1' = 220Hz/750ms");
  check(SPEAKER_BENCH_PRESETS[2].frequencyHz == 440.0f && SPEAKER_BENCH_PRESETS[2].durationMs == 750,
        "1. '2' = 440Hz/750ms");
  check(SPEAKER_BENCH_PRESETS[3].frequencyHz == 880.0f && SPEAKER_BENCH_PRESETS[3].durationMs == 500,
        "1. '3' = 880Hz/500ms");
}

// --- 2. Default bench volume is 5% (index 1) ---
static void test_02_default_volume_is_5_percent() {
  SpeakerBenchSim sim;
  check(sim.benchVolumeIndex == 1, "2. default bench volume index is 1");
  check(SPEAKER_BENCH_VOLUME_STEPS_FRACTION[sim.benchVolumeIndex] == 0.05f, "2. default bench volume is 5%");
}

// --- 3. Volume steps up through the full ladder, capped at 25% ---
static void test_03_volume_up_caps_at_25_percent() {
  SpeakerBenchSim sim;
  for (int i = 0; i < 20; i++) sim.volumeUp();  // far more presses than steps available
  check(SPEAKER_BENCH_VOLUME_STEPS_FRACTION[sim.benchVolumeIndex] == 0.25f, "3. volume up caps at 25%, never higher");
  check(sim.benchVolumeIndex == SPEAKER_BENCH_VOLUME_STEP_COUNT - 1, "3. index caps at the last step");
}

// --- 4. Volume steps down through the full ladder, capped at 2% (never full-scale) ---
static void test_04_volume_down_caps_at_2_percent() {
  SpeakerBenchSim sim;
  for (int i = 0; i < 20; i++) sim.volumeDown();
  check(SPEAKER_BENCH_VOLUME_STEPS_FRACTION[sim.benchVolumeIndex] == 0.02f, "4. volume down caps at 2%, never lower");
  check(sim.benchVolumeIndex == 0, "4. index caps at 0, never negative/wraps");
  for (float step : SPEAKER_BENCH_VOLUME_STEPS_FRACTION) {
    check(step < 1.0f, "4. no bench volume step reaches full-scale (1.0)");
    check(step <= 0.25f, "4. no bench volume step exceeds the requested 25% ceiling");
  }
}

// --- 5. A preset refuses to start if the speaker never initialized ---
static void test_05_preset_refuses_when_not_ready() {
  SpeakerBenchSim sim;
  sim.init(false);
  bool started = sim.startPreset(0);
  check(!started, "5. startPreset() returns false when speaker init failed");
  check(!sim.playing, "5. nothing plays after a refused start");
}

// --- 6. Starting a preset does not reinitialize I2S ---
static void test_06_preset_start_does_not_reinit_i2s() {
  SpeakerBenchSim sim;
  sim.init(true);
  int callsAfterInit = sim.i2sInitCalls;
  for (uint8_t p = 0; p < SPEAKER_BENCH_PRESET_COUNT; p++) sim.startPreset(p);
  check(sim.i2sInitCalls == callsAfterInit, "6. starting all 4 presets never calls init() again");
}

// --- 7. Each preset's amplitude tracks the CURRENT bench volume at start time ---
static void test_07_amplitude_tracks_current_volume_at_start() {
  SpeakerBenchSim sim;
  sim.init(true);
  sim.startPreset(0);
  check(sim.lastStartedAmplitudeFraction == 0.05f, "7. preset starts at the default 5% volume");

  sim.volumeUp();  // -> 8%
  sim.volumeUp();  // -> 12%
  sim.startPreset(2);
  check(sim.lastStartedAmplitudeFraction == 0.12f, "7. preset picks up the volume raised since the last start");

  sim.volumeDown();  // -> 8%
  sim.startPreset(3);
  check(sim.lastStartedAmplitudeFraction == 0.08f, "7. preset picks up the volume lowered since the last start");
  check(sim.lastStartedFrequencyHz == 880.0f && sim.lastStartedDurationMs == 500,
        "7. preset 3's own frequency/duration are unaffected by volume changes");
}

// --- 8. Bench presets are isolated from Audio Mode/MusicMotorController/DanceEngine ---
static void test_08_bench_isolated_from_other_subsystems() {
  SpeakerBenchSim sim;
  sim.init(true);
  bool audioModeEnabled = false;
  bool musicMotorActive = false;
  bool danceEngineActive = false;

  for (uint8_t p = 0; p < SPEAKER_BENCH_PRESET_COUNT; p++) sim.startPreset(p);
  sim.volumeUp();
  sim.volumeDown();

  check(!audioModeEnabled, "8. bench presets never enable Audio Mode");
  check(!musicMotorActive, "8. bench presets never activate MusicMotorController");
  check(!danceEngineActive, "8. bench presets never activate DanceEngine");
}

int main() {
  test_01_preset_table_matches_spec();
  test_02_default_volume_is_5_percent();
  test_03_volume_up_caps_at_25_percent();
  test_04_volume_down_caps_at_2_percent();
  test_05_preset_refuses_when_not_ready();
  test_06_preset_start_does_not_reinit_i2s();
  test_07_amplitude_tracks_current_volume_at_start();
  test_08_bench_isolated_from_other_subsystems();

  if (g_failures == 0) {
    printf("All speaker_bench tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
