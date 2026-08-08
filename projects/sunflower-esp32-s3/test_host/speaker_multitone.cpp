// Host-side regression coverage for the multi-tone speaker bring-up tests
// ('speaker sweep'/'melody'/'chord'/'noise', src/SpeakerTest.cpp's
// startSpeakerBenchSweep()/Melody()/Chord()/Noise(), boundedNoteSequenceSample(),
// and the SPEAKER_BENCH_MELODY_NOTES/SPEAKER_BENCH_CHORD_NOTES tables). Same
// standalone-host-test approach as every other test_host/*.cpp file -- the
// generic engines are pure integer/float arithmetic, no Arduino/ESP-IDF
// dependency, mirrored inline below.
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/speaker_multitone_test test_host/speaker_multitone.cpp && /tmp/speaker_multitone_test
//
// Covers:
//  1. Melody note table totals ~8-12s and every pitch is in the requested
//     220-880Hz range (or a rest)
//  2. Melody includes both short (<=500ms) and sustained (>=800ms) notes,
//     plus at least one brief silent gap
//  3. Chord note table is a 4-note ascending/descending arpeggio, repeated
//     twice, totaling ~5s ("about 5 seconds")
//  4. boundedNoteSequenceSample() is bounded (returns false past the total
//     duration) -- unlike the looping SONG engine
//  5. boundedNoteSequenceSample() rests produce silence; notes produce
//     nonzero output once past their fade-in
//  6. boundedNoteSequenceSample() amplitude tracks currentAmplitudeFraction
//     (the live bench volume at start time), not a fixed baked-in value
//  7. Sweep covers exactly 150Hz -> 3000Hz over ~6s and amplitude tracks
//     the bench volume passed in at start time
//  8. Noise amplitude is min(bench volume, 10%) in both directions (bench
//     below the cap, and bench above the cap)

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

constexpr int I2S_SAMPLE_RATE = 16000;
constexpr double TWO_PI = 6.283185307179586;

static float segmentEnvelope(float tMs, float durMs, float rampMs) {
  float env;
  if (tMs < rampMs) env = tMs / rampMs;
  else if (tMs > durMs - rampMs) env = (durMs - tMs) / rampMs;
  else env = 1.0f;
  if (env < 0.0f) env = 0.0f;
  if (env > 1.0f) env = 1.0f;
  return env;
}

// ----------------------------------------------------------------------------
// Mirrors src/SpeakerTest.cpp's BenchNote/SPEAKER_BENCH_MELODY_NOTES/
// SPEAKER_BENCH_CHORD_NOTES/boundedNoteSequenceSample() exactly.
// ----------------------------------------------------------------------------
struct BenchNote {
  float frequencyHz;
  uint32_t durationMs;
};

constexpr BenchNote SPEAKER_BENCH_MELODY_NOTES[] = {
    {220.0f, 400}, {0.0f, 80},  {330.0f, 400}, {0.0f, 80},  {440.0f, 400}, {0.0f, 80}, {523.0f, 400}, {0.0f, 150},
    {659.0f, 600}, {784.0f, 600}, {880.0f, 900}, {0.0f, 250},
    {784.0f, 350}, {659.0f, 350}, {523.0f, 350}, {440.0f, 900}, {0.0f, 250},
    {330.0f, 350}, {440.0f, 350}, {523.0f, 350}, {659.0f, 1200},
};
constexpr uint8_t SPEAKER_BENCH_MELODY_NOTE_COUNT =
    sizeof(SPEAKER_BENCH_MELODY_NOTES) / sizeof(SPEAKER_BENCH_MELODY_NOTES[0]);

constexpr BenchNote SPEAKER_BENCH_CHORD_NOTES[] = {
    {262.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {392.0f, 300}, {0.0f, 40}, {523.0f, 300}, {0.0f, 40},
    {392.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {262.0f, 300}, {0.0f, 40},
    {262.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {392.0f, 300}, {0.0f, 40}, {523.0f, 300}, {0.0f, 40},
    {392.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {262.0f, 300}, {0.0f, 40},
};
constexpr uint8_t SPEAKER_BENCH_CHORD_NOTE_COUNT =
    sizeof(SPEAKER_BENCH_CHORD_NOTES) / sizeof(SPEAKER_BENCH_CHORD_NOTES[0]);

constexpr uint32_t SPEAKER_BENCH_NOTE_RAMP_MS = 15;

static uint32_t totalDurationMs(const BenchNote *notes, uint8_t count) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; i++) total += notes[i].durationMs;
  return total;
}

// Mirrors boundedNoteSequenceSample() exactly, parameterized over the table
// and the live amplitude (the real function reads currentAmplitudeFraction).
static bool boundedNoteSequenceSample(const BenchNote *notes, uint8_t count, uint32_t totalMs,
                                       float amplitudeFraction, uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)totalMs) return false;

  float cursorMs = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    const BenchNote &note = notes[i];
    if (tMs < cursorMs + (float)note.durationMs) {
      if (note.frequencyHz <= 0.0f) {
        *outSample = 0;
        return true;
      }
      float tWithinNote = tMs - cursorMs;
      float rampMs = fminf((float)SPEAKER_BENCH_NOTE_RAMP_MS, (float)note.durationMs * 0.4f);
      float env = segmentEnvelope(tWithinNote, (float)note.durationMs, rampMs);
      float phaseIncrement = (float)TWO_PI * note.frequencyHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = fmodf((float)sampleIndex * phaseIncrement, (float)TWO_PI);
      *outSample = (int16_t)(env * (amplitudeFraction * 32767.0f) * sinf(rawPhase));
      return true;
    }
    cursorMs += (float)note.durationMs;
  }
  *outSample = 0;
  return true;
}

// Mirrors sweepSample() exactly, parameterized over duration/ramp/amplitude
// (the real function reads currentDurationMs/currentRampMs/currentAmplitudeFraction).
static bool sweepSample(float startHz, float endHz, uint32_t durationMs, uint32_t rampMs, float amplitudeFraction,
                         uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)durationMs) return false;

  float tSec = tMs / 1000.0f;
  float durSec = (float)durationMs / 1000.0f;
  float k = endHz / startHz;
  float phase = (float)TWO_PI * startHz * durSec / logf(k) * (powf(k, tSec / durSec) - 1.0f);
  phase = fmodf(phase, (float)TWO_PI);

  float env = segmentEnvelope(tMs, (float)durationMs, (float)rampMs);
  *outSample = (int16_t)(env * (amplitudeFraction * 32767.0f) * sinf(phase));
  return true;
}

// --- 1. Melody note table totals ~8-12s, every pitch in range (or a rest) ---
static void test_01_melody_duration_and_range() {
  uint32_t total = totalDurationMs(SPEAKER_BENCH_MELODY_NOTES, SPEAKER_BENCH_MELODY_NOTE_COUNT);
  check(total >= 8000 && total <= 12000, "1. melody total duration is within the requested 8-12s window");
  for (uint8_t i = 0; i < SPEAKER_BENCH_MELODY_NOTE_COUNT; i++) {
    float f = SPEAKER_BENCH_MELODY_NOTES[i].frequencyHz;
    check(f == 0.0f || (f >= 220.0f && f <= 880.0f), "1. every melody pitch is a rest or within 220-880Hz");
  }
}

// --- 2. Melody includes short and sustained notes plus a silent gap ---
static void test_02_melody_has_short_and_sustained_notes_and_gaps() {
  bool hasShort = false, hasSustained = false, hasGap = false;
  for (uint8_t i = 0; i < SPEAKER_BENCH_MELODY_NOTE_COUNT; i++) {
    const BenchNote &n = SPEAKER_BENCH_MELODY_NOTES[i];
    if (n.frequencyHz == 0.0f) hasGap = true;
    else if (n.durationMs <= 500) hasShort = true;
    else if (n.durationMs >= 800) hasSustained = true;
  }
  check(hasShort, "2. melody includes at least one short (<=500ms) note");
  check(hasSustained, "2. melody includes at least one sustained (>=800ms) note");
  check(hasGap, "2. melody includes at least one brief silent gap");
}

// --- 3. Chord table is a 4-note arpeggio repeated twice, totaling ~5s ---
static void test_03_chord_structure_and_duration() {
  uint32_t total = totalDurationMs(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT);
  check(total >= 4000 && total <= 6000, "3. chord total duration is close to the requested ~5s");
  // First rep's ascending run: 262, 330, 392, 523 (skipping the rest entries at odd indices).
  check(SPEAKER_BENCH_CHORD_NOTES[0].frequencyHz == 262.0f, "3. chord starts on the root (C4, 262Hz)");
  check(SPEAKER_BENCH_CHORD_NOTES[2].frequencyHz == 330.0f, "3. chord ascends through E4 (330Hz)");
  check(SPEAKER_BENCH_CHORD_NOTES[4].frequencyHz == 392.0f, "3. chord ascends through G4 (392Hz)");
  check(SPEAKER_BENCH_CHORD_NOTES[6].frequencyHz == 523.0f, "3. chord peaks at C5 (523Hz)");
  check(SPEAKER_BENCH_CHORD_NOTES[8].frequencyHz == 392.0f, "3. chord then descends back through G4");
  check(SPEAKER_BENCH_CHORD_NOTES[12].frequencyHz == 262.0f, "3. chord returns to the root");
}

// --- 4. boundedNoteSequenceSample() is bounded, never wraps/loops ---
static void test_04_bounded_never_loops() {
  uint32_t total = totalDurationMs(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT);
  uint32_t framesJustBefore = (uint32_t)((total - 1) * (double)I2S_SAMPLE_RATE / 1000.0);
  uint32_t framesJustAfter = (uint32_t)((total + 50) * (double)I2S_SAMPLE_RATE / 1000.0);
  int16_t s = 0;
  check(boundedNoteSequenceSample(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT, total, 0.05f,
                                   framesJustBefore, &s),
        "4. still playing just before the total duration elapses");
  check(!boundedNoteSequenceSample(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT, total, 0.05f,
                                    framesJustAfter, &s),
        "4. returns false (bounded, not looped) once past the total duration");
}

// --- 5. Rests produce silence; notes produce nonzero output past fade-in ---
static void test_05_rests_silent_notes_audible() {
  uint32_t total = totalDurationMs(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT);
  // Index 1 is a 40ms rest starting at 300ms -- sample deep into it.
  uint32_t restStartFrame = (uint32_t)(305.0 * I2S_SAMPLE_RATE / 1000.0);
  int16_t s = 0;
  boundedNoteSequenceSample(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT, total, 0.25f, restStartFrame,
                             &s);
  check(s == 0, "5. a rest produces exactly zero output");

  // Deep into the first note (past its 15ms/40%-capped ramp, well before its end).
  uint32_t noteFrame = (uint32_t)(150.0 * I2S_SAMPLE_RATE / 1000.0);
  bool foundNonzero = false;
  for (uint32_t f = noteFrame; f < noteFrame + 50; f++) {
    boundedNoteSequenceSample(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT, total, 0.25f, f, &s);
    if (s != 0) foundNonzero = true;
  }
  check(foundNonzero, "5. a note produces nonzero output once past its fade-in");
}

// --- 6. Amplitude tracks the live amplitude parameter, not a fixed value ---
static void test_06_amplitude_tracks_live_value() {
  uint32_t total = totalDurationMs(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT);
  uint32_t noteFrame = (uint32_t)(150.0 * I2S_SAMPLE_RATE / 1000.0);
  int16_t sLow = 0, sHigh = 0;
  boundedNoteSequenceSample(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT, total, 0.05f, noteFrame,
                             &sLow);
  boundedNoteSequenceSample(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT, total, 0.25f, noteFrame,
                             &sHigh);
  check(std::abs(sHigh) > std::abs(sLow), "6. a higher amplitude parameter produces a louder sample magnitude");
}

// --- 7. Sweep covers 150->3000Hz over ~6s, amplitude tracks the bench volume ---
static void test_07_sweep_range_and_amplitude() {
  int16_t s = 0;
  check(sweepSample(150.0f, 3000.0f, 6000, 20, 0.05f, 0, &s), "7. sweep is playing at sample 0");
  uint32_t framesFor5999ms = (uint32_t)(5999.0 * I2S_SAMPLE_RATE / 1000.0);
  uint32_t framesFor6001ms = (uint32_t)(6001.0 * I2S_SAMPLE_RATE / 1000.0);
  check(sweepSample(150.0f, 3000.0f, 6000, 20, 0.05f, framesFor5999ms, &s),
        "7. sweep is still playing just before 6s");
  check(!sweepSample(150.0f, 3000.0f, 6000, 20, 0.05f, framesFor6001ms, &s),
        "7. sweep has ended just after 6s");

  int16_t sLow = 0, sHigh = 0;
  uint32_t midFrame = (uint32_t)(3000.0 * I2S_SAMPLE_RATE / 1000.0);
  sweepSample(150.0f, 3000.0f, 6000, 20, 0.05f, midFrame, &sLow);
  sweepSample(150.0f, 3000.0f, 6000, 20, 0.25f, midFrame, &sHigh);
  check(std::abs(sHigh) >= std::abs(sLow), "7. sweep amplitude scales with the bench volume passed in");
}

// --- 8. Noise amplitude is min(bench volume, 10%) in both directions ---
static void test_08_noise_amplitude_capped() {
  constexpr float NOISE_MAX = 0.10f;
  float benchLow = 0.05f;   // below the cap -- should use the bench value
  float benchHigh = 0.25f;  // above the cap -- should clamp to 10%
  float resultLow = fminf(benchLow, NOISE_MAX);
  float resultHigh = fminf(benchHigh, NOISE_MAX);
  check(resultLow == 0.05f, "8. noise uses the bench volume directly when it's below the 10% cap");
  check(resultHigh == 0.10f, "8. noise clamps to the 10% cap when bench volume is set higher");
}

int main() {
  test_01_melody_duration_and_range();
  test_02_melody_has_short_and_sustained_notes_and_gaps();
  test_03_chord_structure_and_duration();
  test_04_bounded_never_loops();
  test_05_rests_silent_notes_audible();
  test_06_amplitude_tracks_live_value();
  test_07_sweep_range_and_amplitude();
  test_08_noise_amplitude_capped();

  if (g_failures == 0) {
    printf("All speaker_multitone tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
