#include "SpeakerTest.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>

#include "Config.h"
#include "SharedI2S.h"

// ----------------------------------------------------------------------------
// ARCHITECTURE (see include/SharedI2S.h for the full verified-from-headers
// rationale): this module owns no I2S driver configuration. It only calls
// i2s_write()/i2s_zero_dma_buffer() on the SAME I2S_NUM_0 port
// AudioAnalyzer.cpp reads from -- SharedI2S.cpp's initSharedI2S() is the
// sole i2s_driver_install()/i2s_set_pin() owner, configured full-duplex
// MASTER|RX|TX, RIGHT_LEFT (stereo), 32-bit-per-slot. Verified working:
// i2s_write() now reports writtenBytes == requestedBytes in normal
// operation (see the full-duplex refactor report).
//
// Generated samples are 16-bit resolution, left-shifted into the upper 16
// bits of each 32-bit slot (MSB-justified) -- the MAX98357A reads the MSBs
// of whatever slot width it's clocked with. Both L and R slots always get
// the identical sample (see feedSpeakerChunk()).
//
// WRITE PATH: i2s_write() uses a small bounded wait
// (SPEAKER_WRITE_TICKS_TO_WAIT, from pdMS_TO_TICKS(20)) -- never 0, never
// portMAX_DELAY (an isolated portMAX_DELAY diagnostic was tried once, on an
// earlier slave-TX architecture, and froze the entire application -- that
// code has been permanently removed, not just disabled). Sample generation
// is a stateless function of an absolute sample index (sampleForIndex()
// below), so a partial write is handled exactly: toneSampleIndex only
// advances by the number of complete frames i2s_write() actually reports
// as written, so any unwritten remainder is regenerated (not skipped or
// duplicated) on the next call.
//
// TEST SUITE: every start*Test() function below INTERRUPTS whatever is
// currently playing and starts the new test immediately (no "already
// playing" refusal) -- this is a deliberate behavior choice for this
// diagnostic suite (distinct from most of this codebase's other
// mutual-exclusion guards) so a human running through commands quickly
// never has to wait one out or get refused. Emergency stop ('k', via
// stopSpeakerTest()) always wins immediately regardless.
// ----------------------------------------------------------------------------

namespace {

enum class SpeakerPhase { SILENCE, TONE_PLAYING };
// SONG is the procedural music player (see Song/Note below); the others are
// the original diagnostic-tone suite. Adding a future source (WAV/PCM/TTS)
// means adding one more enum value here plus one more case in
// sampleForIndex() -- the scheduler (generateChunk()/feedSpeakerChunk()/the
// write path) never needs to change, since it only ever calls
// sampleForIndex() and knows nothing about what kind of source is active.
enum class TestKind { SINE, SQUARE, SWEEP, MELODY, BEEP, NOISE, SONG };

bool speakerReady = false;
SpeakerPhase phase = SpeakerPhase::SILENCE;

// --- Parameters of the CURRENTLY ARMED test -- set once by startTest(),
// read (never mutated) by sampleForIndex() and its helpers. ---
TestKind currentTestKind = TestKind::SINE;
float currentFrequencyHz = SPEAKER_TONE_FREQUENCY_HZ;
uint32_t currentDurationMs = SPEAKER_TONE_DURATION_MS;
uint32_t currentRampMs = SPEAKER_TONE_RAMP_MS;
float currentAmplitudeFraction = SPEAKER_TONE_AMPLITUDE_FRACTION;
float currentSweepStartHz = SPEAKER_SWEEP_START_HZ;
float currentSweepEndHz = SPEAKER_SWEEP_END_HZ;

// Absolute index of the next NOT-YET-WRITTEN sample within the current
// test. Only ever advanced by the number of complete frames i2s_write()
// actually reports as written (see feedSpeakerChunk()) -- never advanced
// speculatively just because a sample was generated.
uint32_t toneSampleIndex = 0;

unsigned long initCompleteMs = 0;
bool autoDemoToneFired = false;
// initSpeakerTest() runs in setup(), before the pre-existing (unrelated)
// blocking HardwareTest/MicRetest bring-up sequences that follow it --
// those can run for a long time before loop() ever starts. Anchoring the
// auto-demo countdown to initCompleteMs as set in initSpeakerTest() would
// make it fire immediately on the first updateSpeakerTest() call instead
// of ~5s after "Digital silence active" actually printed. Re-latching it
// on the first updateSpeakerTest() call instead ties the countdown to real
// loop() time, regardless of how long setup()'s other blocking work takes.
bool firstUpdateSeen = false;

// --- Write-path bounded wait ---
// pdMS_TO_TICKS(20), clamped to at least 1 tick in case the installed
// FreeRTOS tick rate would otherwise round it to 0. Computed once at
// startup. Never portMAX_DELAY -- see this file's top-of-file comment.
constexpr uint32_t SPEAKER_WRITE_WAIT_MS = 20;
TickType_t computeWriteTicks() {
  TickType_t t = pdMS_TO_TICKS(SPEAKER_WRITE_WAIT_MS);
  return (t > 0) ? t : 1;
}
const TickType_t SPEAKER_WRITE_TICKS_TO_WAIT = computeWriteTicks();

// --- Diagnostics: 4-way write-outcome classification, lifetime counters ---
uint32_t cumulativeSuccessWrites = 0;  // err==ESP_OK && written==requested
uint32_t cumulativeZeroWrites = 0;     // err==ESP_OK && written==0 (requested>0)
uint32_t cumulativePartialWrites = 0;  // err==ESP_OK && 0<written<requested
uint32_t cumulativeWriteErrors = 0;    // err!=ESP_OK
bool firstSuccessLogged = false;       // prints once, the first time a write fully succeeds

int diagCallsRemaining = 0;  // >0 while tracing feedSpeakerChunk() calls for the current trigger
bool diagSamplesPrintedThisRun = false;

// Generic linear fade-in/fade-out envelope for a segment of the given
// duration -- shared by every test type below (single tones, sweep, each
// melody note, each beep) so pop-avoidance is consistent throughout.
float segmentEnvelope(float tMs, float durMs, float rampMs) {
  float env;
  if (tMs < rampMs) env = tMs / rampMs;
  else if (tMs > durMs - rampMs) env = (durMs - tMs) / rampMs;
  else env = 1.0f;
  return constrain(env, 0.0f, 1.0f);
}

// --- Per-test-kind sample generators. Each is a pure function of the
// absolute sample index (no mutable state), returning false once that
// test's own total duration has elapsed. ---

bool sineOrSquareSample(uint32_t sampleIndex, bool square, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)currentDurationMs) return false;

  float env = segmentEnvelope(tMs, (float)currentDurationMs, (float)currentRampMs);
  float phaseIncrement = TWO_PI * currentFrequencyHz / (float)I2S_SAMPLE_RATE;
  float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
  float waveVal = sinf(rawPhase);
  if (square) waveVal = (waveVal >= 0.0f) ? 1.0f : -1.0f;

  *outSample = (int16_t)(env * (currentAmplitudeFraction * 32767.0f) * waveVal);
  return true;
}

// Logarithmic (exponential) chirp: instantaneous frequency
// f(t) = f0 * (f1/f0)^(t/T). The closed-form phase integral is
// phase(t) = 2*pi*f0*T/ln(f1/f0) * ((f1/f0)^(t/T) - 1) -- using this
// directly (rather than naively plugging f(t) into sin(2*pi*f(t)*t)) is
// what makes the sweep's instantaneous frequency actually correct and the
// waveform continuous.
bool sweepSample(uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)currentDurationMs) return false;

  float tSec = tMs / 1000.0f;
  float durSec = (float)currentDurationMs / 1000.0f;
  float k = currentSweepEndHz / currentSweepStartHz;
  float phase = TWO_PI * currentSweepStartHz * durSec / logf(k) * (powf(k, tSec / durSec) - 1.0f);
  phase = fmodf(phase, TWO_PI);

  float env = segmentEnvelope(tMs, (float)currentDurationMs, (float)currentRampMs);
  *outSample = (int16_t)(env * (currentAmplitudeFraction * 32767.0f) * sinf(phase));
  return true;
}

struct MelodyNote {
  float freqHz;
  uint32_t startMs;
};
constexpr MelodyNote MELODY_NOTES[] = {
    {MELODY_NOTE_C5_HZ, 0 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_E5_HZ, 1 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_G5_HZ, 2 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_C6_HZ, 3 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_C5_HZ, MELODY_REPEAT_SPAN_MS + 0 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_E5_HZ, MELODY_REPEAT_SPAN_MS + 1 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_G5_HZ, MELODY_REPEAT_SPAN_MS + 2 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
    {MELODY_NOTE_C6_HZ, MELODY_REPEAT_SPAN_MS + 3 * (MELODY_NOTE_DURATION_MS + MELODY_GAP_MS)},
};
constexpr uint8_t MELODY_NOTE_COUNT = sizeof(MELODY_NOTES) / sizeof(MELODY_NOTES[0]);

bool melodySample(uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)MELODY_TOTAL_DURATION_MS) return false;

  for (uint8_t i = 0; i < MELODY_NOTE_COUNT; i++) {
    float noteStart = (float)MELODY_NOTES[i].startMs;
    float noteEnd = noteStart + (float)MELODY_NOTE_DURATION_MS;
    if (tMs >= noteStart && tMs < noteEnd) {
      float tWithinNote = tMs - noteStart;
      float env = segmentEnvelope(tWithinNote, (float)MELODY_NOTE_DURATION_MS, (float)MELODY_NOTE_RAMP_MS);
      float phaseIncrement = TWO_PI * MELODY_NOTES[i].freqHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
      *outSample = (int16_t)(env * (MELODY_AMPLITUDE_FRACTION * 32767.0f) * sinf(rawPhase));
      return true;
    }
  }
  *outSample = 0;  // the 100ms gap between notes -- silence, but still within the overall test
  return true;
}

bool beepSample(uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)BEEP_TOTAL_DURATION_MS) return false;

  uint32_t cycleMs = BEEP_ON_MS + BEEP_OFF_MS;
  // fmodf, not an integer cast+modulo -- preserves sub-millisecond
  // precision. An earlier version truncated tMs to whole milliseconds
  // before the modulo, which quantized the first ~1ms of every ramp into
  // a single repeated envelope value (a "staircase" instead of a smooth
  // per-sample ramp) -- found via a boot-time sample trace showing the
  // first several generated beep samples as exactly 0 in a row.
  float posInCycleMs = fmodf(tMs, (float)cycleMs);
  if (posInCycleMs >= (float)BEEP_ON_MS) {
    *outSample = 0;  // OFF portion
    return true;
  }

  float tWithinOn = posInCycleMs;
  float env = segmentEnvelope(tWithinOn, (float)BEEP_ON_MS, (float)BEEP_RAMP_MS);
  float phaseIncrement = TWO_PI * BEEP_FREQUENCY_HZ / (float)I2S_SAMPLE_RATE;
  float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
  *outSample = (int16_t)(env * (BEEP_AMPLITUDE_FRACTION * 32767.0f) * sinf(rawPhase));
  return true;
}

bool noiseSample(uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)NOISE_DURATION_MS) return false;

  float env = segmentEnvelope(tMs, (float)NOISE_DURATION_MS, (float)NOISE_RAMP_MS);
  // White noise has no continuity requirement (unlike a tone's phase), so
  // a fresh random() call per sample -- not deterministic in sampleIndex --
  // is fine even under the partial-write-retry model: regenerating "the
  // same" index range with new random values is indistinguishable from the
  // listener's perspective.
  long r = random(-32767, 32768);
  *outSample = (int16_t)(env * NOISE_AMPLITUDE_FRACTION * (float)r);
  return true;
}

// ----------------------------------------------------------------------------
// PROCEDURAL MUSIC ENGINE ('music1'-'music4', see songSample() below).
//
// A reusable Note{frequency, length, amplitude} + Song{notes, count, bpm,
// name} data model, played by ONE generic function (songSample()) that
// walks the note table and works identically for any Song -- nothing here
// is hardcoded per tune. Adding a 5th song means adding a new Note[] table
// and a Song struct pointing at it; the playback code itself never changes.
//
// Like every other test in this file, songSample() is a pure function of
// the absolute sample index: given currentSong (set once when the song
// starts) and sampleIndex, it deterministically computes which note is
// active and what sample to emit -- no running "current note" cursor to
// keep in sync. Looping is just sampleIndex mod (song's total duration),
// so a song plays forever until an explicit command changes currentTestKind
// (any other test, stopmusic, or 'k') -- unlike every fixed-duration test
// above, songSample() never returns false.
// ----------------------------------------------------------------------------

enum class NoteLength : uint8_t { EIGHTH, QUARTER, HALF };

struct Note {
  float frequencyHz;  // 0 = rest (silence for this note's duration)
  NoteLength length;
  float amplitudeFraction;
};

struct Song {
  const Note *notes;
  uint8_t noteCount;
  uint16_t bpm;
  const char *name;  // printed as "[SPEAKER] Playing: <name>"
};

uint32_t noteLengthToMs(NoteLength length, uint16_t bpm) {
  uint32_t quarterMs = 60000UL / bpm;
  switch (length) {
    case NoteLength::EIGHTH: return quarterMs / 2;
    case NoteLength::HALF: return quarterMs * 2;
    case NoteLength::QUARTER:
    default: return quarterMs;
  }
}

uint32_t computeSongDurationMs(const Song &song) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < song.noteCount; i++) total += noteLengthToMs(song.notes[i].length, song.bpm);
  return total;
}

// Standard equal-temperament frequencies (A4 = 440Hz), the octave 4-5 range
// needed by the songs below. Indexed by NoteName -- see N() -- purely for
// readability when authoring note tables (e.g. `N(NoteName::C4)` instead of
// a bare magic-number frequency).
enum class NoteName : uint8_t { REST, C4, D4, E4, F4, G4, A4, B4, C5, D5, E5, F5, G5 };
constexpr float NOTE_FREQ_HZ[] = {
    0.0f,     // REST
    261.63f,  // C4
    293.66f,  // D4
    329.63f,  // E4
    349.23f,  // F4
    392.00f,  // G4
    440.00f,  // A4
    493.88f,  // B4
    523.25f,  // C5
    587.33f,  // D5
    659.25f,  // E5
    698.46f,  // F5
    783.99f,  // G5
};
constexpr float N(NoteName n) { return NOTE_FREQ_HZ[(uint8_t)n]; }

constexpr float MA = MUSIC_AMPLITUDE_FRACTION;  // short alias, keeps the note tables below one line per note
using NL = NoteLength;

// "Twinkle Twinkle Little Star" -- full traditional melody, C major, 120bpm.
constexpr Note TWINKLE_NOTES[] = {
    {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA},
    {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::A4), NL::QUARTER, MA}, {N(NoteName::A4), NL::QUARTER, MA},
    {N(NoteName::G4), NL::HALF, MA},
    {N(NoteName::F4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA},
    {N(NoteName::C4), NL::HALF, MA},
    {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA},
    {N(NoteName::F4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::D4), NL::HALF, MA},
    {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA},
    {N(NoteName::F4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::D4), NL::HALF, MA},
    {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA},
    {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::A4), NL::QUARTER, MA}, {N(NoteName::A4), NL::QUARTER, MA},
    {N(NoteName::G4), NL::HALF, MA},
    {N(NoteName::F4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA},
    {N(NoteName::C4), NL::HALF, MA},
};
constexpr Song TWINKLE_SONG = {TWINKLE_NOTES, sizeof(TWINKLE_NOTES) / sizeof(TWINKLE_NOTES[0]), 120,
                                "Twinkle Twinkle"};

// "Mary Had a Little Lamb" -- traditional melody, C major, 120bpm.
constexpr Note MARY_NOTES[] = {
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA},
    {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::HALF, MA},
    {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::D4), NL::HALF, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::G4), NL::HALF, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA},
    {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA},
    {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA},
    {N(NoteName::C4), NL::HALF, MA},
};
constexpr Song MARY_SONG = {MARY_NOTES, sizeof(MARY_NOTES) / sizeof(MARY_NOTES[0]), 120, "Mary Had a Little Lamb"};

// "Ode to Joy" (Beethoven, Symphony No. 9 -- the melody itself is a public-
// domain 19th-century theme) -- first two phrases, C major, 120bpm.
constexpr Note ODE_NOTES[] = {
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA},
    {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA},
    {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::D4), NL::HALF, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA},
    {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::F4), NL::QUARTER, MA},
    {N(NoteName::E4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA},
    {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::E4), NL::QUARTER, MA},
    {N(NoteName::D4), NL::QUARTER, MA}, {N(NoteName::C4), NL::QUARTER, MA}, {N(NoteName::C4), NL::HALF, MA},
};
constexpr Song ODE_SONG = {ODE_NOTES, sizeof(ODE_NOTES) / sizeof(ODE_NOTES[0]), 120, "Ode to Joy"};

// Super Mario Bros. (NES, 1985, Koji Kondo) overworld theme -- ONLY the
// opening flourish (the first ~2s, instantly recognizable on its own), NOT
// the full copyrighted melody. Faster tempo (200bpm) to match the game's
// actual sprightly pace.
constexpr Note MARIO_NOTES[] = {
    {N(NoteName::E5), NL::EIGHTH, MA}, {N(NoteName::REST), NL::EIGHTH, MA}, {N(NoteName::E5), NL::EIGHTH, MA},
    {N(NoteName::REST), NL::EIGHTH, MA}, {N(NoteName::C5), NL::EIGHTH, MA}, {N(NoteName::E5), NL::EIGHTH, MA},
    {N(NoteName::REST), NL::EIGHTH, MA}, {N(NoteName::G4), NL::QUARTER, MA}, {N(NoteName::REST), NL::QUARTER, MA},
    {N(NoteName::G4), NL::QUARTER, MA},
};
constexpr Song MARIO_SONG = {MARIO_NOTES, sizeof(MARIO_NOTES) / sizeof(MARIO_NOTES[0]), 200,
                             "Super Mario Bros. (opening flourish)"};

// --- Music engine state -- currentSong/songTotalDurationMs are set once
// by startSongPlayback() and read (never mutated) by songSample(), same
// discipline as this file's other "currentX" test parameters.
// lastPrintedMusicLoop is the one exception: mutated by feedSpeakerChunk()
// only, purely to decide when to print "[SPEAKER] Loop N" -- it never
// affects playback itself. ---
const Song *currentSong = nullptr;
uint32_t songTotalDurationMs = 0;
uint32_t lastPrintedMusicLoop = 0;

bool songSample(uint32_t sampleIndex, int16_t *outSample) {
  if (currentSong == nullptr || songTotalDurationMs == 0) {
    *outSample = 0;
    return true;
  }
  const Song &song = *currentSong;
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  float loopRelativeMs = fmodf(tMs, (float)songTotalDurationMs);  // sampleIndex only ever grows -- this is the loop

  float cursorMs = 0.0f;
  for (uint8_t i = 0; i < song.noteCount; i++) {
    const Note &note = song.notes[i];
    uint32_t noteDurMs = noteLengthToMs(note.length, song.bpm);
    if (loopRelativeMs < cursorMs + (float)noteDurMs) {
      if (note.frequencyHz <= 0.0f) {
        *outSample = 0;  // rest
        return true;
      }
      float tWithinNote = loopRelativeMs - cursorMs;
      // Ramp capped at 40% of the note's own duration so short eighth
      // notes still have an audible sustained portion, not an all-ramp blip.
      float rampMs = fminf((float)MUSIC_NOTE_RAMP_MS, (float)noteDurMs * 0.4f);
      float env = segmentEnvelope(tWithinNote, (float)noteDurMs, rampMs);
      float phaseIncrement = TWO_PI * note.frequencyHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
      *outSample = (int16_t)(env * (note.amplitudeFraction * 32767.0f) * sinf(rawPhase));
      return true;
    }
    cursorMs += (float)noteDurMs;
  }
  *outSample = 0;  // unreachable given the fmodf bound above; safe fallback
  return true;
}

bool sampleForIndex(uint32_t sampleIndex, int16_t *outSample) {
  switch (currentTestKind) {
    case TestKind::SINE: return sineOrSquareSample(sampleIndex, false, outSample);
    case TestKind::SQUARE: return sineOrSquareSample(sampleIndex, true, outSample);
    case TestKind::SWEEP: return sweepSample(sampleIndex, outSample);
    case TestKind::MELODY: return melodySample(sampleIndex, outSample);
    case TestKind::BEEP: return beepSample(sampleIndex, outSample);
    case TestKind::NOISE: return noiseSample(sampleIndex, outSample);
    case TestKind::SONG: return songSample(sampleIndex, outSample);
  }
  return false;
}

void printGpio16Routing(const char *when) {
  uint32_t sel = GPIO.func_out_sel_cfg[I2S_SPEAKER_DOUT_PIN].func_sel;
  bool correct = (sel == I2S0O_SD_OUT_IDX);
  Serial.printf("[SPEAKER DIAG] GPIO%d routing (%s): func_out_sel=%lu expected=I2S0O_SD_OUT_IDX(%d) match=%d\n",
                I2S_SPEAKER_DOUT_PIN, when, (unsigned long)sel, (int)I2S0O_SD_OUT_IDX, correct ? 1 : 0);
}

// Shared entry point for every test command -- see this file's top-of-file
// comment on why this INTERRUPTS rather than refuses when something is
// already playing.
void startTest(TestKind kind, float freqHz, uint32_t durationMs, uint32_t rampMs, float amplitudeFraction,
               const char *label) {
  currentTestKind = kind;
  currentFrequencyHz = freqHz;
  currentDurationMs = durationMs;
  currentRampMs = rampMs;
  currentAmplitudeFraction = amplitudeFraction;
  toneSampleIndex = 0;
  phase = SpeakerPhase::TONE_PLAYING;
  diagCallsRemaining = SPEAKER_DIAG_CALL_COUNT;
  diagSamplesPrintedThisRun = false;

  Serial.printf("[SPEAKER] Playing %s\n", label);
  Serial.printf("[SPEAKER DIAG] i2s_write wait: %lu ticks (~%lums)\n", (unsigned long)SPEAKER_WRITE_TICKS_TO_WAIT,
                (unsigned long)SPEAKER_WRITE_WAIT_MS);
  Serial.printf("[SPEAKER DIAG] i2s_get_clk(I2S_NUM_0)=%.1f Hz\n", i2s_get_clk(I2S_PORT));
  printGpio16Routing("at test trigger");
}

// Music-engine entry point -- like startTest() above (INTERRUPTS whatever
// is currently playing), but for TestKind::SONG: prints "Playing: <name>"
// + "Loop 1" instead of a frequency label, and arms songTotalDurationMs/
// lastPrintedMusicLoop so feedSpeakerChunk() can detect subsequent loops.
void startSongPlayback(const Song &song) {
  currentSong = &song;
  songTotalDurationMs = computeSongDurationMs(song);
  currentTestKind = TestKind::SONG;
  toneSampleIndex = 0;
  phase = SpeakerPhase::TONE_PLAYING;
  diagCallsRemaining = SPEAKER_DIAG_CALL_COUNT;
  diagSamplesPrintedThisRun = false;
  lastPrintedMusicLoop = 1;

  Serial.printf("[SPEAKER] Playing: %s\n", song.name);
  Serial.println(F("[SPEAKER] Loop 1"));
  Serial.printf("[SPEAKER DIAG] i2s_write wait: %lu ticks (~%lums)\n", (unsigned long)SPEAKER_WRITE_TICKS_TO_WAIT,
                (unsigned long)SPEAKER_WRITE_WAIT_MS);
  Serial.printf("[SPEAKER DIAG] i2s_get_clk(I2S_NUM_0)=%.1f Hz\n", i2s_get_clk(I2S_PORT));
  printGpio16Routing("at music trigger");
}

// Generates up to SPEAKER_CHUNK_FRAMES speculative frames starting at
// `startIndex` into `buf` (interleaved L,R, 32-bit-per-slot, MSB-justified
// 16-bit audio -- see this file's top-of-file comment). Returns the number
// of frames actually generated (less than SPEAKER_CHUNK_FRAMES if the
// test's duration ends partway through this chunk); *hitEnd is set true in
// that case. `traceFirst10` requests capturing the first 10 raw int16
// samples into `outFirst10` for diagnostics (only meaningful when
// returned frame count >= 10).
int generateChunk(uint32_t startIndex, int32_t *buf, bool *hitEnd, bool traceFirst10, int16_t *outFirst10) {
  *hitEnd = false;
  int framesGenerated = 0;
  for (int i = 0; i < SPEAKER_CHUNK_FRAMES; i++) {
    int16_t raw = 0;
    if (!sampleForIndex(startIndex + (uint32_t)i, &raw)) {
      *hitEnd = true;
      break;
    }
    if (traceFirst10 && i < 10) outFirst10[i] = raw;
    int32_t s32 = ((int32_t)raw) << 16;  // MSB-justify 16-bit audio into the 32-bit slot
    buf[2 * i] = s32;
    buf[2 * i + 1] = s32;
    framesGenerated++;
  }
  return framesGenerated;
}

// Writes one small chunk to the shared I2S TX DMA buffer with a bounded,
// nonzero timeout (see SPEAKER_WRITE_TICKS_TO_WAIT above), then commits
// exactly as many frames as were actually accepted -- see this file's
// top-of-file WRITE PATH comment. Handles both the TONE_PLAYING and
// SILENCE cases; silence has no per-sample state to preserve on a partial
// write (every sample is 0 regardless of position), so it always requests
// a full SPEAKER_CHUNK_FRAMES chunk.
void feedSpeakerChunk() {
  bool tracing = diagCallsRemaining > 0;
  bool playingTone = (phase == SpeakerPhase::TONE_PLAYING);

  int32_t buf[SPEAKER_CHUNK_FRAMES * 2];
  int16_t first10[10] = {0};
  int framesGenerated;
  bool hitEnd = false;

  if (playingTone) {
    framesGenerated = generateChunk(toneSampleIndex, buf, &hitEnd, tracing && !diagSamplesPrintedThisRun, first10);
  } else {
    framesGenerated = SPEAKER_CHUNK_FRAMES;
    for (int i = 0; i < SPEAKER_CHUNK_FRAMES; i++) {
      buf[2 * i] = 0;
      buf[2 * i + 1] = 0;
    }
  }

  if (tracing && playingTone && !diagSamplesPrintedThisRun && framesGenerated >= 10) {
    diagSamplesPrintedThisRun = true;
    Serial.print(F("[SPEAKER DIAG] first 10 generated samples (int16):"));
    for (int i = 0; i < 10; i++) Serial.printf(" %d", first10[i]);
    Serial.println();

    Serial.println(F("[SPEAKER DIAG] first 5 packed stereo frames (hex, L then R, 32-bit-per-slot):"));
    for (int i = 0; i < 5; i++) {
      Serial.printf("  frame %d: L=0x%08lX R=0x%08lX\n", i, (unsigned long)buf[2 * i], (unsigned long)buf[2 * i + 1]);
    }
  }

  size_t requested = (size_t)framesGenerated * 2 * sizeof(int32_t);
  size_t written = 0;
  unsigned long writeStartUs = micros();
  esp_err_t err = i2s_write(I2S_PORT, buf, requested, &written, SPEAKER_WRITE_TICKS_TO_WAIT);
  unsigned long writeElapsedUs = micros() - writeStartUs;

  size_t frameBytes = 2 * sizeof(int32_t);
  size_t framesWritten = written / frameBytes;

  const char *outcome;
  if (err != ESP_OK) {
    cumulativeWriteErrors++;
    outcome = "ERROR";
  } else if (written == 0 && requested > 0) {
    cumulativeZeroWrites++;
    outcome = "ZERO";
  } else if (written < requested) {
    cumulativePartialWrites++;
    outcome = "PARTIAL";
  } else {
    cumulativeSuccessWrites++;
    outcome = "SUCCESS";
    if (!firstSuccessLogged) {
      firstSuccessLogged = true;
      Serial.printf(
          "[SPEAKER DIAG] FIRST SUCCESSFUL WRITE: requestedBytes=%u writtenBytes=%u waitTicks=%lu elapsedUs=%lu\n",
          (unsigned)requested, (unsigned)written, (unsigned long)SPEAKER_WRITE_TICKS_TO_WAIT, writeElapsedUs);
    }
  }

  if (playingTone) {
    // Only advance past frames the DMA actually accepted -- any unwritten
    // remainder is regenerated (not skipped) on the next call.
    toneSampleIndex += (uint32_t)framesWritten;
    if (hitEnd && framesWritten == (size_t)framesGenerated) {
      phase = SpeakerPhase::SILENCE;
      Serial.println(F("[SPEAKER] Done"));
    }
    // Songs never hitEnd (songSample() always returns true, wrapping via
    // fmodf) -- instead, detect and announce each completed loop here,
    // the one place that already knows how many frames were genuinely
    // accepted (not just generated) by the DMA.
    if (currentTestKind == TestKind::SONG && songTotalDurationMs > 0) {
      float tMs = (float)toneSampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
      uint32_t loopNumber = (uint32_t)(tMs / (float)songTotalDurationMs) + 1;
      if (loopNumber != lastPrintedMusicLoop) {
        lastPrintedMusicLoop = loopNumber;
        Serial.printf("[SPEAKER] Loop %lu\n", (unsigned long)loopNumber);
      }
    }
  }

  if (tracing) {
    Serial.printf(
        "[SPEAKER DIAG] feed call trace=%d/%d requestedBytes=%u writtenBytes=%u waitTicks=%lu elapsedUs=%lu "
        "outcome=%s phase=%s toneSampleIndex=%lu | cumulative success=%lu zero=%lu partial=%lu error=%lu\n",
        SPEAKER_DIAG_CALL_COUNT - diagCallsRemaining + 1, SPEAKER_DIAG_CALL_COUNT, (unsigned)requested,
        (unsigned)written, (unsigned long)SPEAKER_WRITE_TICKS_TO_WAIT, writeElapsedUs, outcome,
        phase == SpeakerPhase::TONE_PLAYING ? "TONE_PLAYING" : "SILENCE", (unsigned long)toneSampleIndex,
        (unsigned long)cumulativeSuccessWrites, (unsigned long)cumulativeZeroWrites,
        (unsigned long)cumulativePartialWrites, (unsigned long)cumulativeWriteErrors);
    diagCallsRemaining--;
  }
}

}  // namespace

// Does NOT call i2s_driver_install()/i2s_set_pin() -- SharedI2S.cpp's
// initSharedI2S() is the sole owner of I2S_NUM_0's configuration (see
// include/SharedI2S.h). This only verifies the shared bus is ready and
// primes TX with real silence; call it after initSharedI2S() (and after
// initAudioAnalyzer(), by convention) has already succeeded.
void initSpeakerTest() {
  Serial.println(F("[SPEAKER] Initializing MAX98357A output (shared I2S_NUM_0 bus)"));
  Serial.printf("[SPEAKER] BCLK=%d LRC=%d DIN=%d\n", I2S_BCLK_PIN, I2S_WS_PIN, I2S_SPEAKER_DOUT_PIN);

  if (!isSharedI2SReady()) {
    Serial.println(F("[SPEAKER] I2S TX initialization: FAILED (shared I2S bus not ready)"));
    speakerReady = false;
    return;
  }

  phase = SpeakerPhase::SILENCE;
  toneSampleIndex = 0;
  autoDemoToneFired = false;

  // Immediately (re-)confirm digital silence is being transmitted -- before
  // telling the user it's safe to enable the amplifier. SharedI2S.cpp's
  // initSharedI2S() already primed this once; repeating it here is a
  // harmless, idempotent defense-in-depth, not a second driver owner (it
  // only pushes zero samples into the existing TX buffer).
  i2s_zero_dma_buffer(I2S_PORT);

  Serial.println(F("[SPEAKER] I2S TX initialization: SUCCESS"));
  Serial.println(F("[SPEAKER] Digital silence active"));
  Serial.println(F("[SPEAKER] Connect MAX98357A SD to 3.3V now"));
  printGpio16Routing("immediately after initSpeakerTest()");

  speakerReady = true;
  // initCompleteMs is deliberately NOT set here -- see firstUpdateSeen's
  // comment above; it's latched on the first updateSpeakerTest() call
  // instead, so the auto-demo countdown reflects real loop() time.
}

void updateSpeakerTest(unsigned long now) {
  if (!speakerReady) return;

  if (!firstUpdateSeen) {
    firstUpdateSeen = true;
    initCompleteMs = now;
  }

  // One-shot automatic demonstration tone (same as 't') -- never repeats
  // on its own afterward (autoDemoToneFired latches permanently; only an
  // explicit command can play another).
  if (!autoDemoToneFired && (now - initCompleteMs) >= SPEAKER_AUTO_DEMO_DELAY_MS) {
    autoDemoToneFired = true;
    if (phase == SpeakerPhase::SILENCE) {
      Serial.println(F("[SPEAKER] Automatic demonstration tone"));
      startTest(TestKind::SINE, SPEAKER_TONE_FREQUENCY_HZ, SPEAKER_TONE_DURATION_MS, SPEAKER_TONE_RAMP_MS,
                SPEAKER_TONE_AMPLITUDE_FRACTION, "440 Hz");
    }
  }

  feedSpeakerChunk();
}

void stopSpeakerTest() {
  bool wasPlaying = (phase == SpeakerPhase::TONE_PLAYING);
  phase = SpeakerPhase::SILENCE;
  toneSampleIndex = 0;
  diagCallsRemaining = 0;
  currentSong = nullptr;  // defensive tidiness -- harmless either way (static data, never dangling)
  songTotalDurationMs = 0;
  if (wasPlaying) Serial.println(F("[SPEAKER] Emergency stop -- digital silence active"));
}

bool startSpeakerTestTone() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::SINE, SPEAKER_TONE_FREQUENCY_HZ, SPEAKER_TONE_DURATION_MS, SPEAKER_TONE_RAMP_MS,
            SPEAKER_TONE_AMPLITUDE_FRACTION, "440 Hz");
  return true;
}

bool startSpeakerSquareWaveTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::SQUARE, SPEAKER_TONE_FREQUENCY_HZ, SPEAKER_TONE_DURATION_MS, SPEAKER_TONE_RAMP_MS,
            SPEAKER_SQUARE_AMPLITUDE_FRACTION, "440 Hz square wave");
  return true;
}

bool startSpeakerLowTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::SINE, SPEAKER_LOW_FREQUENCY_HZ, SPEAKER_LMH_DURATION_MS, SPEAKER_LMH_RAMP_MS,
            SPEAKER_LMH_AMPLITUDE_FRACTION, "150 Hz");
  return true;
}

bool startSpeakerMidTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::SINE, SPEAKER_MID_FREQUENCY_HZ, SPEAKER_LMH_DURATION_MS, SPEAKER_LMH_RAMP_MS,
            SPEAKER_LMH_AMPLITUDE_FRACTION, "440 Hz");
  return true;
}

bool startSpeakerHighTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::SINE, SPEAKER_HIGH_FREQUENCY_HZ, SPEAKER_LMH_DURATION_MS, SPEAKER_LMH_RAMP_MS,
            SPEAKER_LMH_AMPLITUDE_FRACTION, "1500 Hz");
  return true;
}

bool startSpeakerSweepTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  currentSweepStartHz = SPEAKER_SWEEP_START_HZ;
  currentSweepEndHz = SPEAKER_SWEEP_END_HZ;
  startTest(TestKind::SWEEP, 0.0f, SPEAKER_SWEEP_DURATION_MS, SPEAKER_SWEEP_RAMP_MS, SPEAKER_SWEEP_AMPLITUDE_FRACTION,
            "sweep");
  return true;
}

bool startSpeakerMelodyTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::MELODY, 0.0f, MELODY_TOTAL_DURATION_MS, 0, MELODY_AMPLITUDE_FRACTION, "melody");
  return true;
}

bool startSpeakerBeepTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::BEEP, BEEP_FREQUENCY_HZ, BEEP_TOTAL_DURATION_MS, 0, BEEP_AMPLITUDE_FRACTION, "beep");
  return true;
}

bool startSpeakerNoiseTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startTest(TestKind::NOISE, 0.0f, NOISE_DURATION_MS, NOISE_RAMP_MS, NOISE_AMPLITUDE_FRACTION, "white noise");
  return true;
}

bool startSpeakerLoudTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  Serial.println(F("[SPEAKER] WARNING: Temporary loud diagnostic"));
  // Hard cap, never to be exceeded -- see LOUD_AMPLITUDE_FRACTION's own
  // comment in Config.h. Reusing the constant directly (rather than
  // letting a caller pass an amplitude in) makes this the single place
  // that value can come from.
  startTest(TestKind::SINE, LOUD_FREQUENCY_HZ, LOUD_DURATION_MS, LOUD_RAMP_MS, LOUD_AMPLITUDE_FRACTION, "1000 Hz");
  return true;
}

bool startSpeakerMusic1() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startSongPlayback(TWINKLE_SONG);
  return true;
}

bool startSpeakerMusic2() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startSongPlayback(MARY_SONG);
  return true;
}

bool startSpeakerMusic3() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startSongPlayback(ODE_SONG);
  return true;
}

bool startSpeakerMusic4() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  startSongPlayback(MARIO_SONG);
  return true;
}

bool stopSpeakerMusic() {
  phase = SpeakerPhase::SILENCE;
  toneSampleIndex = 0;
  diagCallsRemaining = 0;
  currentSong = nullptr;
  songTotalDurationMs = 0;
  Serial.println(F("[SPEAKER] Stopped"));
  return true;
}

bool isSpeakerReady() { return speakerReady; }

void printSpeakerTestStatus() {
  Serial.printf(
      "[SPEAKER] ready=%d phase=%s toneSampleIndex=%lu autoDemoFired=%d waitTicks=%lu | "
      "cumulative success=%lu zero=%lu partial=%lu error=%lu\n",
      speakerReady ? 1 : 0, phase == SpeakerPhase::TONE_PLAYING ? "TONE_PLAYING" : "SILENCE",
      (unsigned long)toneSampleIndex, autoDemoToneFired ? 1 : 0, (unsigned long)SPEAKER_WRITE_TICKS_TO_WAIT,
      (unsigned long)cumulativeSuccessWrites, (unsigned long)cumulativeZeroWrites,
      (unsigned long)cumulativePartialWrites, (unsigned long)cumulativeWriteErrors);
  if (currentTestKind == TestKind::SONG && currentSong != nullptr) {
    Serial.printf("[SPEAKER] music: song=\"%s\" loop=%lu songTotalDurationMs=%lu\n", currentSong->name,
                  (unsigned long)lastPrintedMusicLoop, (unsigned long)songTotalDurationMs);
  }
}
