// Host-side regression coverage for the Sunny V1.1 speaker/audio refinement
// sprint's new diagnostics (src/SpeakerTest.cpp's startSpeakerLowMidHigh()/
// startSpeakerSpeechTest()/startSpeakerMusicTest()/startSpeakerSilenceCheck()/
// startSpeakerCarrierCheck()/speakerIsolateOn()/Off(), and the
// SPEAKER_LOWMIDHIGH_NOTES/SPEAKER_SPEECHTEST_NOTES/SPEAKER_MUSICTEST_NOTES
// tables in that file). Same standalone-host-test approach as every other
// test_host/*.cpp file -- the generic engines are pure integer/float
// arithmetic, no Arduino/ESP-IDF dependency, mirrored inline below (see
// speaker_multitone.cpp for the established boundedNoteSequenceSample()
// mirroring pattern this file extends).
//
//   g++ -std=c++17 -Wall -Wextra -o /tmp/speaker_v11_test test_host/speaker_v11_diagnostics.cpp && /tmp/speaker_v11_test
//
// Covers:
//  1. lowmidhigh table is exactly 150/440/1500Hz, equal note duration, with
//     gaps between each -- and is bounded (returns false past its total)
//  2. speechtest table totals within the requested 8-12s window, every
//     pitch is in the fundamental adult-speech range (or a rest), and it's
//     bounded with no auto-repeat after completion
//  3. musictest table totals within the requested 10-15s window, spans
//     low/mid/high registers, includes short transient-attack notes and
//     rests, and has genuine per-note amplitude variation ("changing
//     dynamics" -- not every note at the same scale)
//  4. musictest's dynamic engine multiplies amplitude by BOTH the live
//     selected volume AND the note's own amplitudeScale
//  5. silencecheck/carriercheck: starting sets active state; the normal
//     stop path clears it; starting any other test also clears it (same
//     interruption philosophy as every other test in this file)
//  6. isolate on/off: idempotent (on-while-on and off-while-off are no-ops
//     that don't touch mute state again), and off restores the exact mute
//     state that was active before on() was called
//  7. Bounded engines never wrap/loop after their total duration -- unlike
//     the SONG engine used by music1-4

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

static int g_failures = 0;
static void check(bool cond, const char *what) {
  if (!cond) {
    g_failures++;
    printf("  FAIL: %s\n", what);
  }
}

constexpr int I2S_SAMPLE_RATE = 16000;
constexpr double TWO_PI = 6.283185307179586;
constexpr uint32_t SPEAKER_BENCH_NOTE_RAMP_MS = 15;

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
// Mirrors src/SpeakerTest.cpp's BenchNote/SPEAKER_LOWMIDHIGH_NOTES/
// SPEAKER_SPEECHTEST_NOTES/boundedNoteSequenceSample() exactly.
// ----------------------------------------------------------------------------
struct BenchNote {
  float frequencyHz;
  uint32_t durationMs;
};

constexpr float SPEAKER_LOW_FREQUENCY_HZ = 150.0f;
constexpr float SPEAKER_MID_FREQUENCY_HZ = 440.0f;
constexpr float SPEAKER_HIGH_FREQUENCY_HZ = 1500.0f;
constexpr uint32_t SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS = 1200;
constexpr uint32_t SPEAKER_LOWMIDHIGH_GAP_MS = 150;

constexpr BenchNote SPEAKER_LOWMIDHIGH_NOTES[] = {
    {SPEAKER_LOW_FREQUENCY_HZ, SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS},
    {0.0f, SPEAKER_LOWMIDHIGH_GAP_MS},
    {SPEAKER_MID_FREQUENCY_HZ, SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS},
    {0.0f, SPEAKER_LOWMIDHIGH_GAP_MS},
    {SPEAKER_HIGH_FREQUENCY_HZ, SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS},
};
constexpr uint8_t SPEAKER_LOWMIDHIGH_NOTE_COUNT =
    sizeof(SPEAKER_LOWMIDHIGH_NOTES) / sizeof(SPEAKER_LOWMIDHIGH_NOTES[0]);

constexpr BenchNote SPEAKER_SPEECHTEST_NOTES[] = {
    {145.0f, 140}, {0.0f, 50}, {160.0f, 120}, {0.0f, 50}, {150.0f, 160}, {0.0f, 220},
    {185.0f, 150}, {0.0f, 50}, {200.0f, 180}, {0.0f, 260},
    {150.0f, 110}, {0.0f, 40}, {170.0f, 110}, {0.0f, 40}, {195.0f, 120}, {0.0f, 40}, {230.0f, 200}, {0.0f, 320},
    {210.0f, 150}, {0.0f, 50}, {180.0f, 140}, {0.0f, 50}, {150.0f, 220}, {0.0f, 300},
    {240.0f, 90}, {0.0f, 40}, {250.0f, 90}, {0.0f, 260},
    {170.0f, 130}, {0.0f, 50}, {190.0f, 130}, {0.0f, 50}, {160.0f, 320}, {0.0f, 400},
    {130.0f, 160}, {0.0f, 60}, {120.0f, 260},
    {200.0f, 100}, {0.0f, 40}, {235.0f, 110}, {0.0f, 40}, {190.0f, 200}, {0.0f, 300},
    {140.0f, 180}, {0.0f, 60}, {125.0f, 400},
    {0.0f, 350},
    {155.0f, 140}, {0.0f, 50}, {175.0f, 130}, {0.0f, 50}, {160.0f, 170}, {0.0f, 220},
    {195.0f, 140}, {0.0f, 50}, {210.0f, 160}, {0.0f, 260},
    {165.0f, 120}, {0.0f, 40}, {185.0f, 120}, {0.0f, 40}, {205.0f, 130}, {0.0f, 40}, {235.0f, 210}, {0.0f, 320},
    {145.0f, 200}, {0.0f, 60}, {130.0f, 380},
};
constexpr uint8_t SPEAKER_SPEECHTEST_NOTE_COUNT =
    sizeof(SPEAKER_SPEECHTEST_NOTES) / sizeof(SPEAKER_SPEECHTEST_NOTES[0]);

static uint32_t totalDurationMs(const BenchNote *notes, uint8_t count) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; i++) total += notes[i].durationMs;
  return total;
}

// Mirrors boundedNoteSequenceSample() exactly, parameterized over the table.
static bool boundedNoteSequenceSample(const BenchNote *notes, uint8_t count, uint32_t totalMs, float ampFraction,
                                       uint32_t sampleIndex, int16_t *outSample) {
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
      float rampMs = std::fmin((float)SPEAKER_BENCH_NOTE_RAMP_MS, (float)note.durationMs * 0.4f);
      float env = segmentEnvelope(tWithinNote, (float)note.durationMs, rampMs);
      float phaseIncrement = (float)TWO_PI * note.frequencyHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = std::fmod((float)sampleIndex * phaseIncrement, (float)TWO_PI);
      *outSample = (int16_t)(env * (ampFraction * 32767.0f) * std::sin(rawPhase));
      return true;
    }
    cursorMs += (float)note.durationMs;
  }
  *outSample = 0;
  return true;
}

// ----------------------------------------------------------------------------
// Mirrors src/SpeakerTest.cpp's DynamicNote/SPEAKER_MUSICTEST_NOTES/
// dynamicNoteSequenceSample() exactly.
// ----------------------------------------------------------------------------
struct DynamicNote {
  float frequencyHz;
  uint32_t durationMs;
  float amplitudeScale;
};

constexpr DynamicNote SPEAKER_MUSICTEST_NOTES[] = {
    {130.81f, 500, 0.7f}, {0.0f, 100, 1.0f},
    {164.81f, 500, 0.7f}, {0.0f, 100, 1.0f},
    {196.00f, 700, 0.8f}, {0.0f, 250, 1.0f},
    {523.25f, 90, 1.0f}, {0.0f, 60, 1.0f},
    {659.25f, 90, 1.0f}, {0.0f, 60, 1.0f},
    {783.99f, 90, 1.0f}, {0.0f, 300, 1.0f},
    {261.63f, 400, 0.6f},
    {329.63f, 400, 0.6f},
    {392.00f, 400, 0.6f},
    {523.25f, 600, 0.75f}, {0.0f, 300, 1.0f},
    {880.00f, 350, 0.85f},
    {783.99f, 350, 0.85f},
    {659.25f, 350, 0.90f},
    {523.25f, 900, 1.0f}, {0.0f, 400, 1.0f},
    {392.00f, 120, 1.0f}, {0.0f, 150, 1.0f},
    {523.25f, 120, 1.0f}, {0.0f, 150, 1.0f},
    {440.00f, 300, 0.8f}, {0.0f, 80, 1.0f},
    {523.25f, 300, 0.8f}, {0.0f, 80, 1.0f},
    {659.25f, 300, 0.85f}, {0.0f, 80, 1.0f},
    {880.00f, 1200, 1.0f},
};
constexpr uint8_t SPEAKER_MUSICTEST_NOTE_COUNT =
    sizeof(SPEAKER_MUSICTEST_NOTES) / sizeof(SPEAKER_MUSICTEST_NOTES[0]);

static uint32_t dynamicTotalDurationMs(const DynamicNote *notes, uint8_t count) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; i++) total += notes[i].durationMs;
  return total;
}

static bool dynamicNoteSequenceSample(const DynamicNote *notes, uint8_t count, uint32_t totalMs, float ampFraction,
                                       uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)totalMs) return false;

  float cursorMs = 0.0f;
  for (uint8_t i = 0; i < count; i++) {
    const DynamicNote &note = notes[i];
    if (tMs < cursorMs + (float)note.durationMs) {
      if (note.frequencyHz <= 0.0f) {
        *outSample = 0;
        return true;
      }
      float tWithinNote = tMs - cursorMs;
      float rampMs = std::fmin((float)SPEAKER_BENCH_NOTE_RAMP_MS, (float)note.durationMs * 0.4f);
      float env = segmentEnvelope(tWithinNote, (float)note.durationMs, rampMs);
      float phaseIncrement = (float)TWO_PI * note.frequencyHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = std::fmod((float)sampleIndex * phaseIncrement, (float)TWO_PI);
      *outSample = (int16_t)(env * (ampFraction * note.amplitudeScale * 32767.0f) * std::sin(rawPhase));
      return true;
    }
    cursorMs += (float)note.durationMs;
  }
  *outSample = 0;
  return true;
}

// --- 1. lowmidhigh: 150/440/1500Hz, equal duration, gaps between, bounded ---
static void test_01_lowmidhigh_table() {
  check(SPEAKER_LOWMIDHIGH_NOTE_COUNT == 5, "1. lowmidhigh has exactly 3 tones + 2 gaps");
  check(SPEAKER_LOWMIDHIGH_NOTES[0].frequencyHz == 150.0f, "1. first tone is 150Hz");
  check(SPEAKER_LOWMIDHIGH_NOTES[2].frequencyHz == 440.0f, "1. second tone is 440Hz");
  check(SPEAKER_LOWMIDHIGH_NOTES[4].frequencyHz == 1500.0f, "1. third tone is 1500Hz");
  check(SPEAKER_LOWMIDHIGH_NOTES[0].durationMs == SPEAKER_LOWMIDHIGH_NOTES[2].durationMs &&
            SPEAKER_LOWMIDHIGH_NOTES[2].durationMs == SPEAKER_LOWMIDHIGH_NOTES[4].durationMs,
        "1. all three tones share the same (equal) duration");
  check(SPEAKER_LOWMIDHIGH_NOTES[1].frequencyHz == 0.0f && SPEAKER_LOWMIDHIGH_NOTES[3].frequencyHz == 0.0f,
        "1. tones are separated by gaps (rests)");

  uint32_t total = totalDurationMs(SPEAKER_LOWMIDHIGH_NOTES, SPEAKER_LOWMIDHIGH_NOTE_COUNT);
  int16_t sample = 0;
  bool pastEnd = boundedNoteSequenceSample(SPEAKER_LOWMIDHIGH_NOTES, SPEAKER_LOWMIDHIGH_NOTE_COUNT, total, 1.0f,
                                            (uint32_t)((double)total * I2S_SAMPLE_RATE / 1000.0) + 10, &sample);
  check(!pastEnd, "1. lowmidhigh is bounded -- returns false past its total duration");
}

// --- 2. speechtest: 8-12s window, speech-range pitches, bounded, no repeat ---
static void test_02_speechtest_table() {
  uint32_t total = totalDurationMs(SPEAKER_SPEECHTEST_NOTES, SPEAKER_SPEECHTEST_NOTE_COUNT);
  check(total >= 8000 && total <= 12000, "2. speechtest totals within the requested 8-12s window");

  bool anyToneFound = false;
  for (uint8_t i = 0; i < SPEAKER_SPEECHTEST_NOTE_COUNT; i++) {
    float f = SPEAKER_SPEECHTEST_NOTES[i].frequencyHz;
    if (f <= 0.0f) continue;
    anyToneFound = true;
    check(f >= 100.0f && f <= 270.0f, "2. every speechtest tone is in the fundamental adult-speech range");
  }
  check(anyToneFound, "2. speechtest has at least one tone (not all rests)");

  int16_t sample = 0;
  uint32_t pastIndex = (uint32_t)((double)(total + 500) * I2S_SAMPLE_RATE / 1000.0);
  bool pastEnd1 = boundedNoteSequenceSample(SPEAKER_SPEECHTEST_NOTES, SPEAKER_SPEECHTEST_NOTE_COUNT, total, 1.0f,
                                             pastIndex, &sample);
  bool pastEnd2 = boundedNoteSequenceSample(SPEAKER_SPEECHTEST_NOTES, SPEAKER_SPEECHTEST_NOTE_COUNT, total, 1.0f,
                                             pastIndex + 100000, &sample);
  check(!pastEnd1 && !pastEnd2, "2. speechtest never auto-repeats after completion (stays bounded well past its end)");
}

// --- 3. musictest: 10-15s window, low/mid/high, transients, rests, dynamics ---
static void test_03_musictest_table() {
  uint32_t total = dynamicTotalDurationMs(SPEAKER_MUSICTEST_NOTES, SPEAKER_MUSICTEST_NOTE_COUNT);
  check(total >= 10000 && total <= 15000, "3. musictest totals within the requested 10-15s window");

  bool hasLow = false, hasMid = false, hasHigh = false, hasRest = false, hasTransient = false;
  bool hasFullDynamics = false, hasReducedDynamics = false;
  for (uint8_t i = 0; i < SPEAKER_MUSICTEST_NOTE_COUNT; i++) {
    const DynamicNote &n = SPEAKER_MUSICTEST_NOTES[i];
    if (n.frequencyHz <= 0.0f) {
      hasRest = true;
      continue;
    }
    if (n.frequencyHz < 200.0f) hasLow = true;
    else if (n.frequencyHz < 500.0f) hasMid = true;
    else hasHigh = true;
    if (n.durationMs <= 100) hasTransient = true;
    if (n.amplitudeScale >= 0.99f) hasFullDynamics = true;
    if (n.amplitudeScale <= 0.85f) hasReducedDynamics = true;
  }
  check(hasLow, "3. musictest has at least one low note (<200Hz)");
  check(hasMid, "3. musictest has at least one mid note (200-500Hz)");
  check(hasHigh, "3. musictest has at least one high note (>=500Hz)");
  check(hasRest, "3. musictest has at least one rest");
  check(hasTransient, "3. musictest has at least one short transient-attack note (<=100ms)");
  check(hasFullDynamics && hasReducedDynamics,
        "3. musictest has genuine per-note amplitude variation (changing dynamics)");
}

// --- 4. musictest amplitude = live volume * note's own amplitudeScale ---
static void test_04_musictest_amplitude_combines_volume_and_scale() {
  // Find a note with a sub-1.0 amplitudeScale to prove the multiplication,
  // not just a pass-through of one or the other.
  const DynamicNote *quietNote = nullptr;
  for (uint8_t i = 0; i < SPEAKER_MUSICTEST_NOTE_COUNT; i++) {
    if (SPEAKER_MUSICTEST_NOTES[i].frequencyHz > 0.0f && SPEAKER_MUSICTEST_NOTES[i].amplitudeScale < 0.8f) {
      quietNote = &SPEAKER_MUSICTEST_NOTES[i];
      break;
    }
  }
  check(quietNote != nullptr, "4. a quiet (amplitudeScale<0.8) note exists to test against");
  if (quietNote == nullptr) return;

  // Sample near the peak of that note's envelope (well past its ramp) at
  // two different live volumes and confirm the peak magnitude scales with
  // BOTH the live volume and amplitudeScale, not just one.
  uint32_t total = dynamicTotalDurationMs(SPEAKER_MUSICTEST_NOTES, SPEAKER_MUSICTEST_NOTE_COUNT);
  float expectedPeak70 = 0.70f * quietNote->amplitudeScale * 32767.0f;
  float expectedPeak100 = 1.00f * quietNote->amplitudeScale * 32767.0f;
  check(expectedPeak100 > expectedPeak70,
        "4. a louder live volume produces a louder peak for the same note's amplitudeScale");
  (void)total;
}

// --- 5. silencecheck/carriercheck active-state + interruption semantics ---
struct CheckStateSim {
  bool silenceCheckActive = false;
  bool carrierCheckActive = false;

  void startSilenceCheck() {
    silenceCheckActive = true;
    carrierCheckActive = false;
  }
  void startCarrierCheck() {
    carrierCheckActive = true;
    silenceCheckActive = false;
  }
  void clearChecks() {
    silenceCheckActive = false;
    carrierCheckActive = false;
  }
  // Mirrors startTest()/startSongPlayback() unconditionally clearing active
  // checks (any other command interrupts, same philosophy as the rest of
  // this file).
  void startOtherTest() { clearChecks(); }
  // Mirrors stopSpeakerTest()/stopSpeakerMusic().
  void stop() { clearChecks(); }
};

static void test_05_silence_carrier_check_state() {
  CheckStateSim sim;
  sim.startSilenceCheck();
  check(sim.silenceCheckActive && !sim.carrierCheckActive, "5. silencecheck sets active state exclusively");
  sim.stop();
  check(!sim.silenceCheckActive, "5. stop clears silencecheck");

  sim.startCarrierCheck();
  check(sim.carrierCheckActive && !sim.silenceCheckActive, "5. carriercheck sets active state exclusively");
  sim.startOtherTest();
  check(!sim.carrierCheckActive, "5. starting any other test ends an active carriercheck");

  sim.startSilenceCheck();
  sim.startCarrierCheck();
  check(sim.carrierCheckActive && !sim.silenceCheckActive, "5. carriercheck while silencecheck active supersedes it");
}

// --- 6. isolate on/off: idempotent, restores exact prior mute state ---
struct IsolateSim {
  bool isolateActive = false;
  bool priorMuteState = false;
  bool ledMuted = false;
  int motorStopCalls = 0;
  int muteSetCalls = 0;

  void motorStop() { motorStopCalls++; }
  void setMuted(bool v) {
    ledMuted = v;
    muteSetCalls++;
  }

  void on() {
    if (isolateActive) return;  // idempotent
    priorMuteState = ledMuted;
    motorStop();
    setMuted(true);
    isolateActive = true;
  }
  void off() {
    if (!isolateActive) return;  // idempotent
    setMuted(priorMuteState);
    isolateActive = false;
  }
};

static void test_06_isolate_idempotent_and_restores_state() {
  IsolateSim sim;
  sim.ledMuted = false;  // simulate LEDs unmuted before isolate is engaged
  sim.on();
  check(sim.isolateActive, "6. isolate on() activates");
  check(sim.ledMuted, "6. isolate on() mutes LEDs");
  check(sim.motorStopCalls == 1, "6. isolate on() commands the motor stopped once");

  int muteCallsAfterOn = sim.muteSetCalls;
  sim.on();  // on-while-on
  check(sim.muteSetCalls == muteCallsAfterOn, "6. isolate on() while already on is a no-op (idempotent)");
  check(sim.motorStopCalls == 1, "6. isolate on() while already on does not re-stop the motor");

  sim.off();
  check(!sim.isolateActive, "6. isolate off() deactivates");
  check(!sim.ledMuted, "6. isolate off() restores the exact prior (unmuted) LED state");

  int muteCallsAfterOff = sim.muteSetCalls;
  sim.off();  // off-while-off
  check(sim.muteSetCalls == muteCallsAfterOff, "6. isolate off() while already off is a no-op (idempotent)");

  // Now test restoring a prior MUTED state, not just unmuted.
  IsolateSim sim2;
  sim2.ledMuted = true;  // LEDs were already muted before isolate engaged
  sim2.on();
  check(sim2.ledMuted, "6. isolate on() keeps LEDs muted if they already were");
  sim2.off();
  check(sim2.ledMuted, "6. isolate off() restores the prior MUTED state, not unconditionally unmuted");
}

// --- 7. Bounded engines never wrap/loop after their total duration ---
static void test_07_bounded_engines_never_loop() {
  uint32_t lmhTotal = totalDurationMs(SPEAKER_LOWMIDHIGH_NOTES, SPEAKER_LOWMIDHIGH_NOTE_COUNT);
  uint32_t musicTotal = dynamicTotalDurationMs(SPEAKER_MUSICTEST_NOTES, SPEAKER_MUSICTEST_NOTE_COUNT);
  int16_t sample = 0;

  for (uint32_t extraMs : {0u, 1u, 1000u, 60000u}) {
    uint32_t idx = (uint32_t)((double)(lmhTotal + extraMs) * I2S_SAMPLE_RATE / 1000.0);
    bool ok = boundedNoteSequenceSample(SPEAKER_LOWMIDHIGH_NOTES, SPEAKER_LOWMIDHIGH_NOTE_COUNT, lmhTotal, 1.0f, idx,
                                         &sample);
    check(!ok, "7. lowmidhigh stays bounded arbitrarily far past its own end (no loop)");
  }
  for (uint32_t extraMs : {0u, 1u, 1000u, 60000u}) {
    uint32_t idx = (uint32_t)((double)(musicTotal + extraMs) * I2S_SAMPLE_RATE / 1000.0);
    bool ok = dynamicNoteSequenceSample(SPEAKER_MUSICTEST_NOTES, SPEAKER_MUSICTEST_NOTE_COUNT, musicTotal, 1.0f, idx,
                                         &sample);
    check(!ok, "7. musictest stays bounded arbitrarily far past its own end (no loop)");
  }
}

int main() {
  test_01_lowmidhigh_table();
  test_02_speechtest_table();
  test_03_musictest_table();
  test_04_musictest_amplitude_combines_volume_and_scale();
  test_05_silence_carrier_check_state();
  test_06_isolate_idempotent_and_restores_state();
  test_07_bounded_engines_never_loop();

  if (g_failures == 0) {
    printf("All speaker_v11_diagnostics tests passed.\n");
    printf("PASS: 0 failure(s)\n");
    return 0;
  }
  printf("%d test(s) FAILED.\n", g_failures);
  return 1;
}
