// Host-side regression coverage for the automatic volume-ladder diagnostic
// ('speaker voltest'/'volquick'/'volstop'/'volstatus', src/SpeakerTest.cpp's
// VOLTEST_FULL_STEPS/VOLTEST_QUICK_STEPS/VOLTEST_FULL_LEVELS_FRACTION/
// VOLTEST_QUICK_LEVELS_FRACTION and the armVolLadderStep()/
// advanceVolLadderStep()/updateVolLadder() state machine). Same standalone-
// host-test approach as every other test_host/*.cpp file -- the step/level
// tables and the state-machine transition logic are pure data/integer
// arithmetic, mirrored inline below, no Arduino/ESP-IDF/Serial/millis()
// dependency.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/speaker_voltest_test test_host/speaker_voltest.cpp && /tmp/speaker_voltest_test
//
// Covers:
//  1. Full ladder: 11 levels, exactly 2/5/8/12/18/25/35/50/65/80/100%
//  2. Quick ladder: 5 levels, exactly 12/25/50/75/100%
//  3. Full ladder's per-level step sequence totals ~4-5s ("roughly 4-5
//     seconds"), quick ladder's totals ~1.9s
//  4. Full ladder total duration ~49s, quick ladder total ~9.5s
//  5. Threshold prints: HIGH OUTPUT TEST for every level >=50%; the 80%/
//     100% warnings fire only at exactly those two levels, nowhere else
//  6. State machine: steps advance within a level, levels advance after
//     their step sequence completes, and the whole ladder completes
//     (without looping) after the last level -- never revisits level 0
//  7. State machine: an abort at any point stops the run immediately and
//     it does not resume/auto-continue afterward
//  8. The melody excerpt (first 7 notes of the existing diagnostic melody)
//     totals 1840ms, within the requested ~1.5-2s window, and is exactly
//     what VOLTEST_MELODY_EXCERPT_NOTE_COUNT expects

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

// ----------------------------------------------------------------------------
// Mirrors include/Config.h's VOLTEST_* constants and
// src/SpeakerTest.cpp's VolLadderStepAction/VolLadderStepDef/
// VOLTEST_FULL_STEPS/VOLTEST_QUICK_STEPS exactly.
// ----------------------------------------------------------------------------
constexpr float VOLTEST_FULL_LEVELS_FRACTION[] = {0.02f, 0.05f, 0.08f, 0.12f, 0.18f,
                                                    0.25f, 0.35f, 0.50f, 0.65f, 0.80f, 1.00f};
constexpr uint8_t VOLTEST_FULL_LEVEL_COUNT =
    sizeof(VOLTEST_FULL_LEVELS_FRACTION) / sizeof(VOLTEST_FULL_LEVELS_FRACTION[0]);
constexpr float VOLTEST_QUICK_LEVELS_FRACTION[] = {0.12f, 0.25f, 0.50f, 0.75f, 1.00f};
constexpr uint8_t VOLTEST_QUICK_LEVEL_COUNT =
    sizeof(VOLTEST_QUICK_LEVELS_FRACTION) / sizeof(VOLTEST_QUICK_LEVELS_FRACTION[0]);
constexpr float VOLTEST_HIGH_OUTPUT_THRESHOLD_FRACTION = 0.50f;
constexpr float VOLTEST_EIGHTY_PERCENT_WARNING_FRACTION = 0.80f;
constexpr float VOLTEST_FULL_SCALE_WARNING_FRACTION = 1.00f;
constexpr uint8_t VOLTEST_MELODY_EXCERPT_NOTE_COUNT = 7;

enum class VolLadderStepAction : uint8_t { TONE, MELODY, GAP };
struct VolLadderStepDef {
  VolLadderStepAction action;
  float frequencyHz;
  uint32_t durationMs;
};

constexpr VolLadderStepDef VOLTEST_FULL_STEPS[] = {
    {VolLadderStepAction::TONE, 220.0f, 500},   {VolLadderStepAction::GAP, 0.0f, 100},
    {VolLadderStepAction::TONE, 440.0f, 500},   {VolLadderStepAction::GAP, 0.0f, 100},
    {VolLadderStepAction::TONE, 880.0f, 500},   {VolLadderStepAction::GAP, 0.0f, 150},
    {VolLadderStepAction::MELODY, 0.0f, 0},     {VolLadderStepAction::GAP, 0.0f, 750},
};
constexpr uint8_t VOLTEST_FULL_STEP_COUNT = sizeof(VOLTEST_FULL_STEPS) / sizeof(VOLTEST_FULL_STEPS[0]);

constexpr VolLadderStepDef VOLTEST_QUICK_STEPS[] = {
    {VolLadderStepAction::TONE, 440.0f, 600},
    {VolLadderStepAction::GAP, 0.0f, 200},
    {VolLadderStepAction::TONE, 880.0f, 600},
    {VolLadderStepAction::GAP, 0.0f, 500},
};
constexpr uint8_t VOLTEST_QUICK_STEP_COUNT = sizeof(VOLTEST_QUICK_STEPS) / sizeof(VOLTEST_QUICK_STEPS[0]);

// The melody excerpt is the first VOLTEST_MELODY_EXCERPT_NOTE_COUNT entries
// of SpeakerTest.cpp's SPEAKER_BENCH_MELODY_NOTES -- mirrored here (just the
// prefix actually used) rather than the full table, since only its total
// duration matters for this diagnostic.
constexpr uint32_t MELODY_EXCERPT_NOTE_DURATIONS_MS[] = {400, 80, 400, 80, 400, 80, 400};
static uint32_t melodyExcerptTotalMs() {
  uint32_t total = 0;
  for (uint32_t d : MELODY_EXCERPT_NOTE_DURATIONS_MS) total += d;
  return total;
}

static uint32_t stepTableTotalMs(const VolLadderStepDef *steps, uint8_t count) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; i++) {
    total += (steps[i].action == VolLadderStepAction::MELODY) ? melodyExcerptTotalMs() : steps[i].durationMs;
  }
  return total;
}

// ----------------------------------------------------------------------------
// Mirrors the armVolLadderStep()/advanceVolLadderStep()/updateVolLadder()
// state machine -- simplified to "which (level, step) is active" plus
// "active/aborted/completed", since real hardware timing/Serial/I2S are out
// of scope for a host test. tick() models one event: either "the current
// step finished" (TONE/MELODY reaching phase==SILENCE, or a GAP's deadline
// passing) -- the real code's two distinct wait conditions collapse to the
// same "step finished" event here, since both just call
// advanceVolLadderStep().
// ----------------------------------------------------------------------------
struct VolLadderSim {
  bool active = false;
  bool completed = false;
  bool aborted = false;
  bool abortedSafety = false;
  uint8_t levelIndex = 0;
  uint8_t stepIndex = 0;
  uint8_t lastCompletedLevel = 0;
  bool isFull = true;

  const float *levels() const { return isFull ? VOLTEST_FULL_LEVELS_FRACTION : VOLTEST_QUICK_LEVELS_FRACTION; }
  uint8_t levelCount() const { return isFull ? VOLTEST_FULL_LEVEL_COUNT : VOLTEST_QUICK_LEVEL_COUNT; }
  uint8_t stepCount() const { return isFull ? VOLTEST_FULL_STEP_COUNT : VOLTEST_QUICK_STEP_COUNT; }

  void start(bool full) {
    active = true;
    completed = false;
    aborted = false;
    abortedSafety = false;
    isFull = full;
    levelIndex = 0;
    stepIndex = 0;
    lastCompletedLevel = 0;
  }

  void abort(bool safety) {
    if (!active) return;
    active = false;
    aborted = true;
    abortedSafety = safety;
  }

  // Mirrors advanceVolLadderStep() -- called when the current step finishes.
  void tick() {
    if (!active) return;
    stepIndex++;
    if (stepIndex >= stepCount()) {
      lastCompletedLevel = levelIndex + 1;
      levelIndex++;
      if (levelIndex >= levelCount()) {
        active = false;
        completed = true;
        return;
      }
      stepIndex = 0;
      return;
    }
  }
};

// --- 1. Full ladder: 11 levels, exactly 2/5/8/12/18/25/35/50/65/80/100% ---
static void test_01_full_levels_exact() {
  check(VOLTEST_FULL_LEVEL_COUNT == 11, "1. full ladder has exactly 11 levels");
  float expected[] = {0.02f, 0.05f, 0.08f, 0.12f, 0.18f, 0.25f, 0.35f, 0.50f, 0.65f, 0.80f, 1.00f};
  for (uint8_t i = 0; i < VOLTEST_FULL_LEVEL_COUNT; i++) {
    check(std::fabs(VOLTEST_FULL_LEVELS_FRACTION[i] - expected[i]) < 0.0001f, "1. full ladder level value matches spec");
  }
}

// --- 2. Quick ladder: 5 levels, exactly 12/25/50/75/100% ---
static void test_02_quick_levels_exact() {
  check(VOLTEST_QUICK_LEVEL_COUNT == 5, "2. quick ladder has exactly 5 levels");
  float expected[] = {0.12f, 0.25f, 0.50f, 0.75f, 1.00f};
  for (uint8_t i = 0; i < VOLTEST_QUICK_LEVEL_COUNT; i++) {
    check(std::fabs(VOLTEST_QUICK_LEVELS_FRACTION[i] - expected[i]) < 0.0001f,
          "2. quick ladder level value matches spec");
  }
}

// --- 3. Per-level duration windows ---
static void test_03_per_level_duration_windows() {
  uint32_t fullLevelMs = stepTableTotalMs(VOLTEST_FULL_STEPS, VOLTEST_FULL_STEP_COUNT);
  check(fullLevelMs >= 4000 && fullLevelMs <= 5000, "3. full ladder per-level duration is roughly 4-5s");
  uint32_t quickLevelMs = stepTableTotalMs(VOLTEST_QUICK_STEPS, VOLTEST_QUICK_STEP_COUNT);
  check(quickLevelMs >= 1500 && quickLevelMs <= 2500, "3. quick ladder per-level duration is a few seconds");
}

// --- 4. Total ladder durations ---
static void test_04_total_ladder_durations() {
  uint32_t fullLevelMs = stepTableTotalMs(VOLTEST_FULL_STEPS, VOLTEST_FULL_STEP_COUNT);
  uint32_t fullTotalMs = fullLevelMs * VOLTEST_FULL_LEVEL_COUNT;
  check(fullTotalMs >= 40000 && fullTotalMs <= 60000, "4. full ladder total duration is roughly 40-60s (~49s)");
  uint32_t quickLevelMs = stepTableTotalMs(VOLTEST_QUICK_STEPS, VOLTEST_QUICK_STEP_COUNT);
  uint32_t quickTotalMs = quickLevelMs * VOLTEST_QUICK_LEVEL_COUNT;
  check(quickTotalMs >= 7000 && quickTotalMs <= 12000, "4. quick ladder total duration is roughly 7-12s (~9.5s)");
}

// --- 5. Threshold prints fire at exactly the right levels ---
static void test_05_threshold_prints() {
  bool expectedHighOutput[] = {false, false, false, false, false, false, false, true, true, true, true};
  bool expectedEighty[] = {false, false, false, false, false, false, false, false, false, true, false};
  bool expectedFullScale[] = {false, false, false, false, false, false, false, false, false, false, true};
  for (uint8_t i = 0; i < VOLTEST_FULL_LEVEL_COUNT; i++) {
    float level = VOLTEST_FULL_LEVELS_FRACTION[i];
    bool highOutput = level >= VOLTEST_HIGH_OUTPUT_THRESHOLD_FRACTION;
    bool eighty = std::fabs(level - VOLTEST_EIGHTY_PERCENT_WARNING_FRACTION) < 0.001f;
    bool fullScale = std::fabs(level - VOLTEST_FULL_SCALE_WARNING_FRACTION) < 0.001f;
    check(highOutput == expectedHighOutput[i], "5. HIGH OUTPUT TEST fires exactly for levels >=50%");
    check(eighty == expectedEighty[i], "5. 80% warning fires only at exactly the 80% level");
    check(fullScale == expectedFullScale[i], "5. FULL-SCALE warning fires only at exactly the 100% level");
  }
}

// --- 6. State machine: steps advance, levels advance, ladder completes without looping ---
static void test_06_state_machine_progression_and_completion() {
  VolLadderSim sim;
  sim.start(true);
  check(sim.active && sim.levelIndex == 0 && sim.stepIndex == 0, "6. starts at level 0, step 0");

  // Advance through every step of every level.
  uint32_t totalTicks = (uint32_t)VOLTEST_FULL_STEP_COUNT * VOLTEST_FULL_LEVEL_COUNT;
  uint8_t seenMaxLevel = 0;
  for (uint32_t t = 0; t < totalTicks; t++) {
    if (sim.active) seenMaxLevel = sim.levelIndex > seenMaxLevel ? sim.levelIndex : seenMaxLevel;
    sim.tick();
    if (!sim.active) break;
  }
  check(sim.completed, "6. ladder reaches COMPLETE after the last level's last step");
  check(!sim.aborted, "6. a normal completion is not recorded as an abort");
  check(sim.lastCompletedLevel == VOLTEST_FULL_LEVEL_COUNT, "6. last completed level is the final level (11)");
  check(seenMaxLevel == VOLTEST_FULL_LEVEL_COUNT - 1, "6. every level from 0 up to the last was visited in order");

  // Ticking further after completion must not resume/loop.
  uint8_t levelBefore = sim.levelIndex;
  sim.tick();
  check(!sim.active && sim.levelIndex == levelBefore, "6. ticking after completion does not resume or loop");
}

// --- 7. Abort stops immediately and does not auto-resume ---
static void test_07_abort_stops_and_does_not_resume() {
  VolLadderSim sim;
  sim.start(true);
  for (int i = 0; i < 5; i++) sim.tick();  // partway through level 0
  check(sim.active, "7. still active partway through the run");
  sim.abort(false);
  check(!sim.active, "7. abort() immediately deactivates the run");
  check(sim.aborted && !sim.abortedSafety, "7. recorded as a manual abort");
  check(!sim.completed, "7. an aborted run is not also recorded as completed");

  uint8_t stepBefore = sim.stepIndex;
  sim.tick();
  check(sim.stepIndex == stepBefore, "7. ticking after abort does not advance or resume");

  // Safety-triggered abort is distinguished from manual.
  VolLadderSim sim2;
  sim2.start(false);
  sim2.abort(true);
  check(sim2.aborted && sim2.abortedSafety, "7. a safety-triggered abort is recorded distinctly from manual");
}

// --- 8. Melody excerpt totals ~1.84s, within the requested ~1.5-2s window ---
static void test_08_melody_excerpt_duration() {
  check(sizeof(MELODY_EXCERPT_NOTE_DURATIONS_MS) / sizeof(MELODY_EXCERPT_NOTE_DURATIONS_MS[0]) ==
            VOLTEST_MELODY_EXCERPT_NOTE_COUNT,
        "8. excerpt note count matches VOLTEST_MELODY_EXCERPT_NOTE_COUNT");
  uint32_t total = melodyExcerptTotalMs();
  check(total >= 1500 && total <= 2000, "8. melody excerpt duration is within the requested ~1.5-2s window");
}

int main() {
  test_01_full_levels_exact();
  test_02_quick_levels_exact();
  test_03_per_level_duration_windows();
  test_04_total_ladder_durations();
  test_05_threshold_prints();
  test_06_state_machine_progression_and_completion();
  test_07_abort_stops_and_does_not_resume();
  test_08_melody_excerpt_duration();

  if (g_failures == 0) {
    printf("All speaker_voltest tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
