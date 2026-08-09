#include "SpeakerTest.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>

#include "Config.h"
#include "Controls.h"
#include "MotorDriver.h"
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
// FMT_DIAG is the one exception -- see fmtDiagSample()'s own comment for
// why it bypasses sampleForIndex() entirely. BENCH_NOTES is 'speaker
// melody'/'speaker chord'/'speaker lowmidhigh'/'speaker speechtest' -- a
// bounded (non-looping) note sequence, sharing one generic engine
// (boundedNoteSequenceSample() below) the same way SONG shares songSample()
// across music1-4 -- see that function's own comment. MUSICTEST_NOTES is
// 'speaker musictest' -- the one diagnostic in this file that needs
// per-note AMPLITUDE as well as frequency/duration ("changing dynamics"),
// so it gets its own small table/engine (dynamicNoteSequenceSample())
// rather than overloading BenchNote's fixed-amplitude shape.
enum class TestKind { SINE, SQUARE, SWEEP, MELODY, BEEP, NOISE, SONG, FMT_DIAG, BENCH_NOTES, MUSICTEST_NOTES };

// Stage S3 buzz/distortion diagnostic ('speaker fmt1'/'fmt2'/'fmt3') -- which
// bit depth/justification to pack the sine sample as within each 32-bit I2S
// slot. See fmtDiagSample() for the exact per-format bit layout.
enum class SpeakerFmtDiagFormat : uint8_t { FMT1_16IN32, FMT2_24IN32, FMT3_32DIRECT };

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
// Set once by startSpeakerFmt1()/2()/3(), read (never mutated) by
// fmtDiagSample() -- same discipline as the other currentX parameters above.
SpeakerFmtDiagFormat currentFmtDiagFormat = SpeakerFmtDiagFormat::FMT1_16IN32;
// Last format a fmt1/2/3 command actually started, independent of
// currentFmtDiagFormat's "next" value -- purely for 'speaker fmtstatus' to
// report what was last heard, even after a different test has since played.
SpeakerFmtDiagFormat lastStartedFmtDiagFormat = SpeakerFmtDiagFormat::FMT1_16IN32;
bool fmtDiagEverStarted = false;

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

// --- Write-error edge detection (see feedSpeakerChunk()) -- prints once per
// contiguous failure streak, not once per call, so a sustained fault
// doesn't flood the serial monitor at loop() rate. Escalates to an explicit
// "repeated write failures" warning once a streak crosses
// SPEAKER_WRITE_REPEATED_FAILURE_THRESHOLD.
bool writeErrorStreakActive = false;
uint32_t writeErrorStreakLength = 0;
bool writeErrorRepeatedWarningPrinted = false;

// True only for the write feedSpeakerChunk() just performed THIS call --
// ERROR, ZERO, or PARTIAL, never SUCCESS. Read by updateVolLadder() (see
// below this namespace) to immediately abort an active 'speaker voltest'/
// 'volquick' run on any bad write outcome, per that diagnostic's own
// stricter safety requirement -- not just the escalated "repeated failures"
// threshold every other test here tolerates via the cumulative counters.
bool lastWriteHadFault = false;

// --- V1.1 normal-use volume ('speaker t'/'1'/'2'/'3'/'volume'/'v'/'+'/'-') --
// see SPEAKER_VOLUME_* in Config.h. Persists across presets/tests (not
// across reboot -- no settings system exists) so the chosen volume survives
// interruption by another command.
uint8_t speakerVolumeIndex = SPEAKER_VOLUME_DEFAULT_INDEX;

// --- V1.1 silence/carrier check state ('speaker silencecheck'/
// 'carriercheck') -- see startSpeakerSilenceCheck()/startSpeakerCarrierCheck()
// below. Both just hold phase==SILENCE (the same continuous digital zero
// the speaker already transmits between tests) and print a periodic
// reminder so a human knows the check is still running; neither has a
// fixed duration, both end via the normal stop paths. ---
bool silenceCheckActive = false;
bool carrierCheckActive = false;
unsigned long silenceCheckStartMs = 0;
unsigned long lastSilenceCheckStatusMs = 0;

void clearActiveChecks() {
  silenceCheckActive = false;
  carrierCheckActive = false;
}

// ----------------------------------------------------------------------------
// Automatic volume-ladder diagnostic ('speaker voltest'/'volquick'/'volstop'/
// 'volstatus') -- DATA only (types, step tables, module state), declared
// here (rather than nearer the bottom of the namespace) because startTest()/
// startSongPlayback() below need to reference volActive/
// volLadderArmingInProgress -- C++ requires the declaration to precede use
// at namespace scope. The orchestration LOGIC (armVolLadderStep()/
// advanceVolLadderStep()/updateVolLadder()/the public start*()/stop*()/
// print*() functions) lives after this namespace closes, alongside every
// other "public" test-command function in this file, so it can freely call
// startTest()/armBenchNotes()/stopSpeakerMusic() -- the same file-wide-
// visibility pattern this file already relies on for currentSong/phase/etc.
//
// Each level runs a short, fixed step sequence (three sine tones + gaps +
// a melody excerpt for the full ladder; two tones + gaps for the quick
// one) entirely through this file's EXISTING engines -- SINE via
// startTest(), the melody excerpt via the SAME BENCH_NOTES/
// boundedNoteSequenceSample() engine 'speaker melody' uses (first 7 notes
// of SPEAKER_BENCH_MELODY_NOTES, ~1.84s). No new I2S write path.
// ----------------------------------------------------------------------------

enum class VolLadderStepAction : uint8_t { TONE, MELODY, GAP };
struct VolLadderStepDef {
  VolLadderStepAction action;
  float frequencyHz;    // unused for MELODY/GAP
  uint32_t durationMs;  // TONE: tone duration; GAP: wait duration; MELODY: unused (real duration comes
                         // from the armed note table's own total, computed once by armBenchNotes())
};

// Per-level sequence for 'speaker voltest': 220Hz/500ms, 100ms gap,
// 440Hz/500ms, 100ms gap, 880Hz/500ms, 150ms gap, melody excerpt (~1.84s),
// 750ms inter-level silence -- sums to ~4.44s/level, matching the requested
// "roughly 4-5 seconds" per level.
constexpr VolLadderStepDef VOLTEST_FULL_STEPS[] = {
    {VolLadderStepAction::TONE, 220.0f, 500},   {VolLadderStepAction::GAP, 0.0f, 100},
    {VolLadderStepAction::TONE, 440.0f, 500},   {VolLadderStepAction::GAP, 0.0f, 100},
    {VolLadderStepAction::TONE, 880.0f, 500},   {VolLadderStepAction::GAP, 0.0f, 150},
    {VolLadderStepAction::MELODY, 0.0f, 0},     {VolLadderStepAction::GAP, 0.0f, 750},
};
constexpr uint8_t VOLTEST_FULL_STEP_COUNT = sizeof(VOLTEST_FULL_STEPS) / sizeof(VOLTEST_FULL_STEPS[0]);

// Per-level sequence for 'speaker volquick': 440Hz/600ms, 200ms gap,
// 880Hz/600ms, 500ms gap -- ~1.9s/level, for "quickly repeating the test
// later".
constexpr VolLadderStepDef VOLTEST_QUICK_STEPS[] = {
    {VolLadderStepAction::TONE, 440.0f, 600},
    {VolLadderStepAction::GAP, 0.0f, 200},
    {VolLadderStepAction::TONE, 880.0f, 600},
    {VolLadderStepAction::GAP, 0.0f, 500},
};
constexpr uint8_t VOLTEST_QUICK_STEP_COUNT = sizeof(VOLTEST_QUICK_STEPS) / sizeof(VOLTEST_QUICK_STEPS[0]);

enum class VolLadderKind : uint8_t { NONE, FULL, QUICK };

// --- Ladder engine state -- set/read only by the orchestration functions
// after this namespace closes (armVolLadderStep()/advanceVolLadderStep()/
// updateVolLadder()/abortVolLadderIfActive()/the public start/stop/print
// functions), plus the two abort-hook checks added to startTest()/
// startSongPlayback() just below. volLadderArmingInProgress guards the
// ladder's OWN internal startTest() calls so they don't self-trigger that
// "any other command interrupts and aborts the ladder" hook. ---
bool volActive = false;
VolLadderKind volKind = VolLadderKind::NONE;
uint8_t volLevelIndex = 0;               // 0-based index into the active level table
uint8_t volStepIndex = 0;                // 0-based index into the active step table
bool volWaitingForPhaseSilence = false;  // true while a TONE/MELODY sub-step is armed and playing
unsigned long volGapDeadlineMs = 0;      // valid only while the current step is a GAP
uint8_t volLastCompletedLevel = 0;       // 1-based; 0 = no level completed yet this run
bool volLastRunAborted = false;          // true if the most recent run ended via abort, not COMPLETE
bool volLastAbortWasSafety = false;      // true if that abort was the write-fault safety trip, not manual
bool volLadderArmingInProgress = false;

// Idempotent (safe to call when no ladder is running -- just returns).
// Called from: startTest()/startSongPlayback() below (any other test
// command interrupts and aborts an active ladder run, same "every
// start*Test() interrupts" philosophy as the rest of this file), plus
// stopSpeakerTest()/stopSpeakerMusic() and the ladder's own safety-trip and
// manual-'speaker volstop' paths (defined after this namespace closes).
// Does NOT itself touch phase/toneSampleIndex -- callers that need audio to
// actually stop immediately (as opposed to just marking the ladder's own
// bookkeeping inactive) call stopSpeakerTest()/stopSpeakerMusic() too,
// which already force phase back to SILENCE unconditionally.
void abortVolLadderIfActive(bool safetyTriggered) {
  if (!volActive) return;
  volActive = false;
  volKind = VolLadderKind::NONE;
  volLastRunAborted = true;
  volLastAbortWasSafety = safetyTriggered;
  Serial.println(safetyTriggered ? F("[SPEAKER VOLTEST] ABORTED -- I2S write fault detected")
                                  : F("[SPEAKER VOLTEST] Aborted"));
}

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

// Reads currentDurationMs/currentRampMs/currentAmplitudeFraction (set by
// startTest(), same discipline as sineOrSquareSample()/sweepSample() above)
// rather than the fixed NOISE_* constants directly -- found during the
// 'speaker noise' extension: this was the one generator in the file NOT
// already following that pattern, which would have blocked reuse for a
// second noise test with its own duration/amplitude. startSpeakerNoiseTest()
// (the original bare 'noise' command) is unaffected -- it already passes
// NOISE_DURATION_MS/NOISE_RAMP_MS/NOISE_AMPLITUDE_FRACTION into startTest()
// today, so this is a behavior-preserving fix, not a behavior change.
bool noiseSample(uint32_t sampleIndex, int16_t *outSample) {
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)currentDurationMs) return false;

  float env = segmentEnvelope(tMs, (float)currentDurationMs, (float)currentRampMs);
  // White noise has no continuity requirement (unlike a tone's phase), so
  // a fresh random() call per sample -- not deterministic in sampleIndex --
  // is fine even under the partial-write-retry model: regenerating "the
  // same" index range with new random values is indistinguishable from the
  // listener's perspective.
  long r = random(-32767, 32768);
  *outSample = (int16_t)(env * currentAmplitudeFraction * (float)r);
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

// ----------------------------------------------------------------------------
// Bounded (non-looping) note sequence engine -- 'speaker melody'/'speaker
// chord'. Deliberately separate from the SONG/Song/songSample() engine
// above: SONG loops forever (fmodf-wrapped, by design, for music1-4's
// "plays until stopped" behavior), whereas these two bench diagnostics are
// each a single bounded pass that returns to silence on its own, like every
// other test in this file (SINE/SWEEP/MELODY/BEEP/NOISE). One generic
// engine, two tables (currentBenchNotes swapped by startSpeakerMelody()/
// startSpeakerChord()) -- same "one engine, many tables" pattern as
// SONG/Song, and amplitude is read live from currentAmplitudeFraction (the
// current bench volume at the moment the test was started via startTest()),
// not baked into the table -- unlike Song's per-note amplitudeFraction,
// these two are always played at one uniform, bench-volume-controlled level.
// ----------------------------------------------------------------------------
struct BenchNote {
  float frequencyHz;    // 0 = rest (silence for this note's duration)
  uint32_t durationMs;
};

// --- 'speaker melody': an original ~8.8s diagnostic phrase, not a copy or
// encoding of any existing song -- spans the requested ~220-880Hz range,
// mixes short (350-400ms) and sustained (900-1200ms) notes, with several
// brief (80-250ms) silent gaps. ---
constexpr BenchNote SPEAKER_BENCH_MELODY_NOTES[] = {
    {220.0f, 400}, {0.0f, 80},  {330.0f, 400}, {0.0f, 80},  {440.0f, 400}, {0.0f, 80}, {523.0f, 400}, {0.0f, 150},
    {659.0f, 600}, {784.0f, 600}, {880.0f, 900}, {0.0f, 250},
    {784.0f, 350}, {659.0f, 350}, {523.0f, 350}, {440.0f, 900}, {0.0f, 250},
    {330.0f, 350}, {440.0f, 350}, {523.0f, 350}, {659.0f, 1200},
};
constexpr uint8_t SPEAKER_BENCH_MELODY_NOTE_COUNT =
    sizeof(SPEAKER_BENCH_MELODY_NOTES) / sizeof(SPEAKER_BENCH_MELODY_NOTES[0]);

// --- 'speaker chord': a monophonic arpeggio standing in for simultaneous
// polyphony (this engine generates one sine at a time -- see this file's
// top-of-file comment) -- ascending C4-E4-G4-C5, then descending back down
// to C4 (omitting the repeated top note), played twice (~4.76s total, "about
// 5 seconds"). ---
constexpr BenchNote SPEAKER_BENCH_CHORD_NOTES[] = {
    {262.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {392.0f, 300}, {0.0f, 40}, {523.0f, 300}, {0.0f, 40},
    {392.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {262.0f, 300}, {0.0f, 40},
    {262.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {392.0f, 300}, {0.0f, 40}, {523.0f, 300}, {0.0f, 40},
    {392.0f, 300}, {0.0f, 40}, {330.0f, 300}, {0.0f, 40}, {262.0f, 300}, {0.0f, 40},
};
constexpr uint8_t SPEAKER_BENCH_CHORD_NOTE_COUNT =
    sizeof(SPEAKER_BENCH_CHORD_NOTES) / sizeof(SPEAKER_BENCH_CHORD_NOTES[0]);

// --- 'speaker lowmidhigh': 150/440/1500Hz in sequence, equal duration and
// gaps -- the volume-ladder-aware counterpart to the original fixed-10%
// 'low'/'mid'/'high' commands (SPEAKER_LOW/MID/HIGH_FREQUENCY_HZ above,
// unchanged). Reuses the SAME frequency constants (no new ones) and this
// same bounded note-sequence engine 'speaker melody'/'chord' use. ---
constexpr BenchNote SPEAKER_LOWMIDHIGH_NOTES[] = {
    {SPEAKER_LOW_FREQUENCY_HZ, SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS},
    {0.0f, SPEAKER_LOWMIDHIGH_GAP_MS},
    {SPEAKER_MID_FREQUENCY_HZ, SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS},
    {0.0f, SPEAKER_LOWMIDHIGH_GAP_MS},
    {SPEAKER_HIGH_FREQUENCY_HZ, SPEAKER_LOWMIDHIGH_NOTE_DURATION_MS},
};
constexpr uint8_t SPEAKER_LOWMIDHIGH_NOTE_COUNT =
    sizeof(SPEAKER_LOWMIDHIGH_NOTES) / sizeof(SPEAKER_LOWMIDHIGH_NOTES[0]);

// --- 'speaker speechtest': an ORIGINAL synthetic speech-like diagnostic --
// NOT copyrighted audio, not a recording, not real speech. This engine has
// no polyphony/formant synthesis (one sine generator -- see this file's
// top-of-file comment), so "formant-like" here is an arpeggiated, rapidly-
// changing approximation: short "syllable" notes across the fundamental
// adult-voice range (~110-250Hz, spanning typical male/female F0), grouped
// into "word"-like runs (short intra-word gaps, longer inter-word/sentence
// gaps) with rising and falling pitch contours (question-like vs
// statement-like), ~10s total. Each note's own ramp-in/out (via this
// engine's existing segmentEnvelope() call) is the "amplitude envelope
// resembling syllables". ---
constexpr BenchNote SPEAKER_SPEECHTEST_NOTES[] = {
    // "word" 1 -- 3 short syllables, mid-low pitch
    {145.0f, 140}, {0.0f, 50}, {160.0f, 120}, {0.0f, 50}, {150.0f, 160}, {0.0f, 220},
    // "word" 2 -- 2 syllables, slightly higher
    {185.0f, 150}, {0.0f, 50}, {200.0f, 180}, {0.0f, 260},
    // "word" 3 -- 4 syllables, rising pitch (question-like contour)
    {150.0f, 110}, {0.0f, 40}, {170.0f, 110}, {0.0f, 40}, {195.0f, 120}, {0.0f, 40}, {230.0f, 200}, {0.0f, 320},
    // "word" 4 -- 3 syllables, falling pitch (statement contour)
    {210.0f, 150}, {0.0f, 50}, {180.0f, 140}, {0.0f, 50}, {150.0f, 220}, {0.0f, 300},
    // "word" 5 -- 2 short clipped syllables, higher (female-range) pitch
    {240.0f, 90}, {0.0f, 40}, {250.0f, 90}, {0.0f, 260},
    // "word" 6 -- 3 syllables, mid pitch, longer final syllable (sentence-final lengthening)
    {170.0f, 130}, {0.0f, 50}, {190.0f, 130}, {0.0f, 50}, {160.0f, 320}, {0.0f, 400},
    // "word" 7 -- lower register close, 2 syllables
    {130.0f, 160}, {0.0f, 60}, {120.0f, 260},
    // "word" 8 -- rising then falling (emphatic), higher pitch
    {200.0f, 100}, {0.0f, 40}, {235.0f, 110}, {0.0f, 40}, {190.0f, 200}, {0.0f, 300},
    // "word" 9 -- closing word, low, longer sustained (sentence-final)
    {140.0f, 180}, {0.0f, 60}, {125.0f, 400},
    {0.0f, 350},  // sentence-boundary pause, longer than a word-boundary gap
    // Second sentence -- similar contour, shifted register slightly
    {155.0f, 140}, {0.0f, 50}, {175.0f, 130}, {0.0f, 50}, {160.0f, 170}, {0.0f, 220},
    {195.0f, 140}, {0.0f, 50}, {210.0f, 160}, {0.0f, 260},
    {165.0f, 120}, {0.0f, 40}, {185.0f, 120}, {0.0f, 40}, {205.0f, 130}, {0.0f, 40}, {235.0f, 210}, {0.0f, 320},
    {145.0f, 200}, {0.0f, 60}, {130.0f, 380},
};
constexpr uint8_t SPEAKER_SPEECHTEST_NOTE_COUNT =
    sizeof(SPEAKER_SPEECHTEST_NOTES) / sizeof(SPEAKER_SPEECHTEST_NOTES[0]);

// --- Engine state -- currentBenchNotes/currentBenchNotesTotalMs are set
// once by startSpeakerMelody()/startSpeakerChord() (via armBenchNotes()
// below) and read (never mutated) by boundedNoteSequenceSample(), same
// discipline as this file's other "currentX" test parameters. ---
const BenchNote *currentBenchNotes = nullptr;
uint8_t currentBenchNoteCount = 0;
uint32_t currentBenchNotesTotalMs = 0;

bool boundedNoteSequenceSample(uint32_t sampleIndex, int16_t *outSample) {
  if (currentBenchNotes == nullptr || currentBenchNotesTotalMs == 0) {
    *outSample = 0;
    return false;
  }
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)currentBenchNotesTotalMs) return false;  // bounded -- unlike SONG, never wraps

  float cursorMs = 0.0f;
  for (uint8_t i = 0; i < currentBenchNoteCount; i++) {
    const BenchNote &note = currentBenchNotes[i];
    if (tMs < cursorMs + (float)note.durationMs) {
      if (note.frequencyHz <= 0.0f) {
        *outSample = 0;  // rest
        return true;
      }
      float tWithinNote = tMs - cursorMs;
      // Ramp capped at 40% of the note's own duration, same convention as
      // songSample() above, so even the shortest (40ms gap-adjacent) notes
      // keep an audible sustained portion.
      float rampMs = fminf((float)SPEAKER_BENCH_NOTE_RAMP_MS, (float)note.durationMs * 0.4f);
      float env = segmentEnvelope(tWithinNote, (float)note.durationMs, rampMs);
      float phaseIncrement = TWO_PI * note.frequencyHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
      *outSample = (int16_t)(env * (currentAmplitudeFraction * 32767.0f) * sinf(rawPhase));
      return true;
    }
    cursorMs += (float)note.durationMs;
  }
  *outSample = 0;  // unreachable given the tMs bound above; safe fallback
  return true;
}

// ----------------------------------------------------------------------------
// 'speaker musictest' engine -- like BenchNote/boundedNoteSequenceSample()
// above (bounded, non-looping, one generic engine + one table), but each
// note also carries its own amplitudeScale, multiplied against the current
// normal speaker volume ("changing dynamics" -- forte/mezzo-forte/piano
// relative to whatever volume the user has selected). This is the one
// diagnostic in this file that needs per-note amplitude, which is why it
// gets its own small struct/table/engine instead of extending BenchNote
// (extending BenchNote would mean touching every existing melody/chord
// table entry, an unrelated risk for an unrelated feature). ---
// ----------------------------------------------------------------------------
struct DynamicNote {
  float frequencyHz;      // 0 = rest (silence for this note's duration)
  uint32_t durationMs;
  float amplitudeScale;   // multiplies currentAmplitudeFraction (the live selected speaker volume)
};

// --- 'speaker musictest': an ORIGINAL short musical diagnostic, NOT a
// copyrighted melody -- low notes (sustained, moderate dynamics), a run of
// short transient-attack high notes (full dynamics), a flowing mid section
// (softer dynamics), a bright high section (building dynamics), and a
// closing transient-plus-sustained-finish phrase, ~11s total. ---
constexpr DynamicNote SPEAKER_MUSICTEST_NOTES[] = {
    // Low section -- sustained, moderate dynamics
    {130.81f, 500, 0.7f}, {0.0f, 100, 1.0f},   // C3
    {164.81f, 500, 0.7f}, {0.0f, 100, 1.0f},   // E3
    {196.00f, 700, 0.8f}, {0.0f, 250, 1.0f},   // G3
    // Transient attacks -- short, punchy, full dynamics
    {523.25f, 90, 1.0f}, {0.0f, 60, 1.0f},     // C5
    {659.25f, 90, 1.0f}, {0.0f, 60, 1.0f},     // E5
    {783.99f, 90, 1.0f}, {0.0f, 300, 1.0f},    // G5
    // Mid section -- flowing, softer dynamics
    {261.63f, 400, 0.6f},                       // C4
    {329.63f, 400, 0.6f},                       // E4
    {392.00f, 400, 0.6f},                       // G4
    {523.25f, 600, 0.75f}, {0.0f, 300, 1.0f},  // C5
    // High section -- bright, building dynamics
    {880.00f, 350, 0.85f},                      // A5
    {783.99f, 350, 0.85f},                      // G5
    {659.25f, 350, 0.90f},                      // E5
    {523.25f, 900, 1.0f}, {0.0f, 400, 1.0f},   // C5, full dynamics finish
    // Closing transient punches with rests (dynamic contrast) then a
    // sustained full-dynamics finish
    {392.00f, 120, 1.0f}, {0.0f, 150, 1.0f},   // G4
    {523.25f, 120, 1.0f}, {0.0f, 150, 1.0f},   // C5
    {440.00f, 300, 0.8f}, {0.0f, 80, 1.0f},    // A4
    {523.25f, 300, 0.8f}, {0.0f, 80, 1.0f},    // C5
    {659.25f, 300, 0.85f}, {0.0f, 80, 1.0f},   // E5
    {880.00f, 1200, 1.0f},                      // A5, sustained finish
};
constexpr uint8_t SPEAKER_MUSICTEST_NOTE_COUNT =
    sizeof(SPEAKER_MUSICTEST_NOTES) / sizeof(SPEAKER_MUSICTEST_NOTES[0]);

// --- Engine state -- currentMusicTestNotes/currentMusicTestTotalMs are set
// once by startSpeakerMusicTest() (via armMusicTestNotes() below) and read
// (never mutated) by dynamicNoteSequenceSample(), same discipline as this
// file's other "currentX" test parameters. ---
const DynamicNote *currentMusicTestNotes = nullptr;
uint8_t currentMusicTestNoteCount = 0;
uint32_t currentMusicTestTotalMs = 0;

bool dynamicNoteSequenceSample(uint32_t sampleIndex, int16_t *outSample) {
  if (currentMusicTestNotes == nullptr || currentMusicTestTotalMs == 0) {
    *outSample = 0;
    return false;
  }
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)currentMusicTestTotalMs) return false;  // bounded -- never wraps

  float cursorMs = 0.0f;
  for (uint8_t i = 0; i < currentMusicTestNoteCount; i++) {
    const DynamicNote &note = currentMusicTestNotes[i];
    if (tMs < cursorMs + (float)note.durationMs) {
      if (note.frequencyHz <= 0.0f) {
        *outSample = 0;  // rest
        return true;
      }
      float tWithinNote = tMs - cursorMs;
      // Same ramp convention as boundedNoteSequenceSample() above (capped at
      // 40% of the note's own duration) -- the shortest transient-attack
      // notes here (90ms) still get an audible sustained portion.
      float rampMs = fminf((float)SPEAKER_BENCH_NOTE_RAMP_MS, (float)note.durationMs * 0.4f);
      float env = segmentEnvelope(tWithinNote, (float)note.durationMs, rampMs);
      float phaseIncrement = TWO_PI * note.frequencyHz / (float)I2S_SAMPLE_RATE;
      float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
      *outSample = (int16_t)(env * (currentAmplitudeFraction * note.amplitudeScale * 32767.0f) * sinf(rawPhase));
      return true;
    }
    cursorMs += (float)note.durationMs;
  }
  *outSample = 0;  // unreachable given the tMs bound above; safe fallback
  return true;
}

// ----------------------------------------------------------------------------
// Stage S3 buzz/distortion diagnostic ('speaker fmt1'/'fmt2'/'fmt3') -- see
// docs for the full task rationale. The shared bus (SharedI2S.cpp) is fixed
// at 32 bits-per-slot for BOTH directions (a hard full-duplex constraint --
// changing it would also change the microphone's RX format, explicitly out
// of scope here). What's actually in question is which PORTION of each
// 32-bit slot the speaker's own generated sample occupies:
//   fmt1 = 16-bit signed sample, MSB-justified into bits[31:16], bits[15:0]=0
//          (this is what every other test in this file already produces --
//          see generateChunk()'s existing int16 "raw" path above).
//   fmt2 = 24-bit signed sample, left-justified into bits[31:8], bits[7:0]=0
//          (the conventional "24-bit audio in a 32-bit container" packing).
//   fmt3 = full 32-bit signed sample occupying the entire slot, no padding
//          -- the only one of the three that uses every bit the hardware is
//          actually clocking out, since the shared bus is hard-configured
//          at 32 bits/slot regardless of which format is selected here.
// All three duplicate the identical value into both L and R slots (mono),
// exactly like every other test in this file -- see this file's top-of-file
// comment on why L/R order is a non-issue under mono duplication.
// Bypasses sampleForIndex()'s normal int16-only pipeline (see
// generateChunk()'s FMT_DIAG branch) because fmt2/fmt3 need more than
// int16_t's 16 bits of resolution -- sampleForIndex()'s signature can't
// carry that without changing every other generator function in this file.
// ----------------------------------------------------------------------------
bool fmtDiagSample(uint32_t sampleIndex, int32_t *outL, int32_t *outR) {
  // Reads the shared "currently armed test" parameters (currentFrequencyHz/
  // currentDurationMs/currentRampMs/currentAmplitudeFraction), same as
  // sineOrSquareSample() and every other generator above -- startSpeakerFmt1()/
  // 2()/3() arm these via startTest() with the SPEAKER_FMT_DIAG_* constants
  // (Config.h) before playback begins, so all three formats get the same
  // 440Hz/750ms/20ms-ramp/2% values without duplicating them here.
  float tMs = (float)sampleIndex * 1000.0f / (float)I2S_SAMPLE_RATE;
  if (tMs >= (float)currentDurationMs) return false;

  float env = segmentEnvelope(tMs, (float)currentDurationMs, (float)currentRampMs);
  float phaseIncrement = TWO_PI * currentFrequencyHz / (float)I2S_SAMPLE_RATE;
  float rawPhase = fmodf((float)sampleIndex * phaseIncrement, TWO_PI);
  // [-1, 1] -- scaled to each format's own full-scale value below, so all
  // three formats represent the same 2% relative analog level, not just the
  // same raw integer magnitude in different bit positions.
  float unitValue = env * currentAmplitudeFraction * sinf(rawPhase);

  int32_t sample32;
  switch (currentFmtDiagFormat) {
    case SpeakerFmtDiagFormat::FMT1_16IN32: {
      int16_t s16 = (int16_t)(unitValue * 32767.0f);
      // Unsigned shift-then-reinterpret -- see generateChunk()'s own comment
      // on why this avoids left-shift-of-negative-value undefined behavior.
      sample32 = (int32_t)((uint32_t)(uint16_t)s16 << 16);
      break;
    }
    case SpeakerFmtDiagFormat::FMT2_24IN32: {
      int32_t s24 = (int32_t)(unitValue * 8388607.0f);
      s24 = constrain(s24, -8388608, 8388607);
      sample32 = (int32_t)((uint32_t)s24 << 8);
      break;
    }
    case SpeakerFmtDiagFormat::FMT3_32DIRECT:
    default: {
      double s32d = (double)unitValue * 2147483647.0;
      if (s32d > 2147483647.0) s32d = 2147483647.0;
      if (s32d < -2147483648.0) s32d = -2147483648.0;
      sample32 = (int32_t)s32d;
      break;
    }
  }
  *outL = sample32;
  *outR = sample32;
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
    case TestKind::BENCH_NOTES: return boundedNoteSequenceSample(sampleIndex, outSample);
    case TestKind::MUSICTEST_NOTES: return dynamicNoteSequenceSample(sampleIndex, outSample);
    // FMT_DIAG never reaches here -- generateChunk() special-cases it to
    // fmtDiagSample() before calling sampleForIndex() at all (see that
    // function's own comment). This case exists only to keep the switch
    // exhaustive; it is unreachable in practice.
    case TestKind::FMT_DIAG:
      *outSample = 0;
      return false;
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
  // An active 'speaker voltest'/'volquick' ladder run is itself just a
  // sequence of startTest() calls (see armVolLadderStep()) -- guard against
  // self-aborting those with volLadderArmingInProgress, while still
  // treating any OTHER command's startTest() call (a human typing 't'/'s'/
  // 'sweep'/etc. mid-ladder) as an interruption that cancels the run, same
  // "every start*Test() interrupts whatever's playing" philosophy this file
  // already applies to every other test.
  if (volActive && !volLadderArmingInProgress) abortVolLadderIfActive(false);
  clearActiveChecks();  // any test start ends an active silencecheck/carriercheck, same interruption philosophy

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
  // music1-4 never run while the ladder is active by design, so this is
  // never guarded by volLadderArmingInProgress -- any music1-4 command
  // always interrupts and aborts an active ladder run.
  if (volActive) abortVolLadderIfActive(false);
  clearActiveChecks();
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

  // Stage S3 format diagnostic ('speaker fmt1'/'fmt2'/'fmt3') -- a separate
  // packing path, not routed through sampleForIndex()/the int16 "raw"
  // pipeline below, since each format needs its own native bit depth
  // (16/24/32) rather than always generating at int16 resolution and
  // shifting. See fmtDiagSample() for the per-format packing and its own
  // header comment for why this exists.
  if (currentTestKind == TestKind::FMT_DIAG) {
    for (int i = 0; i < SPEAKER_CHUNK_FRAMES; i++) {
      int32_t l = 0, r = 0;
      if (!fmtDiagSample(startIndex + (uint32_t)i, &l, &r)) {
        *hitEnd = true;
        break;
      }
      if (traceFirst10 && i < 10) outFirst10[i] = (int16_t)(l >> 16);  // top 16 bits, for the shared trace print
      buf[2 * i] = l;
      buf[2 * i + 1] = r;
      framesGenerated++;
    }
    return framesGenerated;
  }

  for (int i = 0; i < SPEAKER_CHUNK_FRAMES; i++) {
    int16_t raw = 0;
    if (!sampleForIndex(startIndex + (uint32_t)i, &raw)) {
      *hitEnd = true;
      break;
    }
    if (traceFirst10 && i < 10) outFirst10[i] = raw;
    // MSB-justify 16-bit audio into the 32-bit slot. Shifts through
    // uint32_t rather than shifting the signed int32_t directly -- left-
    // shifting a negative signed value is undefined behavior pre-C++20;
    // this codebase builds under the Arduino toolchain's gnu++11 default.
    // GCC/xtensa's actual (implementation-defined) behavior for the old
    // form was already the intended bit pattern, so this is a defensive
    // correctness fix found during the format audit, not a behavior
    // change -- bit-for-bit identical output on this toolchain.
    int32_t s32 = (int32_t)((uint32_t)(uint16_t)raw << 16);
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
    lastWriteHadFault = true;
    // Edge-triggered: print the first failure of a streak immediately, then
    // stay quiet until either recovery or the repeated-failure threshold --
    // see writeErrorStreakActive's own comment above.
    if (!writeErrorStreakActive) {
      writeErrorStreakActive = true;
      writeErrorRepeatedWarningPrinted = false;
      Serial.printf("[SPEAKER] I2S write error: err=%d requestedBytes=%u writtenBytes=%u\n", (int)err,
                    (unsigned)requested, (unsigned)written);
    }
    writeErrorStreakLength++;
    if (writeErrorStreakLength >= SPEAKER_WRITE_REPEATED_FAILURE_THRESHOLD && !writeErrorRepeatedWarningPrinted) {
      writeErrorRepeatedWarningPrinted = true;
      Serial.printf("[SPEAKER] WARNING: %lu consecutive I2S write failures\n", (unsigned long)writeErrorStreakLength);
    }
  } else if (written == 0 && requested > 0) {
    cumulativeZeroWrites++;
    outcome = "ZERO";
    lastWriteHadFault = true;
  } else if (written < requested) {
    cumulativePartialWrites++;
    outcome = "PARTIAL";
    lastWriteHadFault = true;
  } else {
    cumulativeSuccessWrites++;
    outcome = "SUCCESS";
    lastWriteHadFault = false;
    if (writeErrorStreakActive) {
      Serial.printf("[SPEAKER] I2S write recovered after %lu consecutive failure(s)\n",
                    (unsigned long)writeErrorStreakLength);
      writeErrorStreakActive = false;
      writeErrorStreakLength = 0;
    }
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
      Serial.println(F("[SPEAKER] Silence"));
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

// Shared by startSpeakerBenchMelody()/startSpeakerBenchChord()/the volume-
// ladder's MELODY step below -- arms the bounded note-sequence engine's
// table/count/total-duration state, exactly once, so no caller has to
// recompute the total duration by hand. Placed here (rather than nearer
// startSpeakerBenchMelody()) so armVolLadderStep() below can call it.
static void armBenchNotes(const BenchNote *notes, uint8_t count) {
  currentBenchNotes = notes;
  currentBenchNoteCount = count;
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; i++) total += notes[i].durationMs;
  currentBenchNotesTotalMs = total;
}

// ----------------------------------------------------------------------------
// Automatic volume-ladder diagnostic ORCHESTRATION ('speaker voltest'/
// 'volquick'/'volstop'/'volstatus') -- see the DATA block (VolLadderStepDef/
// VOLTEST_FULL_STEPS/VOLTEST_QUICK_STEPS/volActive/etc., declared inside the
// namespace above, near speakerVolumeIndex) for the full design rationale.
// Placed here, before initSpeakerTest()/updateSpeakerTest(), so
// updateVolLadder() is defined before updateSpeakerTest() calls it.
// ----------------------------------------------------------------------------

static const float *volLevelsTable() {
  return (volKind == VolLadderKind::FULL) ? VOLTEST_FULL_LEVELS_FRACTION : VOLTEST_QUICK_LEVELS_FRACTION;
}
static uint8_t volLevelCount() {
  return (volKind == VolLadderKind::FULL) ? VOLTEST_FULL_LEVEL_COUNT : VOLTEST_QUICK_LEVEL_COUNT;
}
static const VolLadderStepDef *volStepsTable() {
  return (volKind == VolLadderKind::FULL) ? VOLTEST_FULL_STEPS : VOLTEST_QUICK_STEPS;
}
static uint8_t volStepCount() {
  return (volKind == VolLadderKind::FULL) ? VOLTEST_FULL_STEP_COUNT : VOLTEST_QUICK_STEP_COUNT;
}

// Arms the CURRENT step (volStepIndex) -- either starts a bounded
// tone/melody sub-test and waits for phase to return to SILENCE (TONE/
// MELODY), or arms a plain millis()-based wait (GAP). Never both at once.
// volLadderArmingInProgress brackets the startTest() calls here so they
// don't self-trigger startTest()'s own "any other command aborts an active
// ladder" hook (see that function's comment).
static void armVolLadderStep(unsigned long now) {
  const VolLadderStepDef &step = volStepsTable()[volStepIndex];
  float amplitude = volLevelsTable()[volLevelIndex];

  switch (step.action) {
    case VolLadderStepAction::TONE:
      volWaitingForPhaseSilence = true;
      volLadderArmingInProgress = true;
      startTest(TestKind::SINE, step.frequencyHz, step.durationMs, VOLTEST_TONE_RAMP_MS, amplitude, "voltest tone");
      volLadderArmingInProgress = false;
      break;
    case VolLadderStepAction::MELODY:
      volWaitingForPhaseSilence = true;
      armBenchNotes(SPEAKER_BENCH_MELODY_NOTES, VOLTEST_MELODY_EXCERPT_NOTE_COUNT);
      volLadderArmingInProgress = true;
      startTest(TestKind::BENCH_NOTES, 0.0f, currentBenchNotesTotalMs, 0, amplitude, "voltest melody excerpt");
      volLadderArmingInProgress = false;
      break;
    case VolLadderStepAction::GAP:
      volWaitingForPhaseSilence = false;
      volGapDeadlineMs = now + step.durationMs;
      break;
  }
}

// Prints the "Level N/Total: X%" announcement plus any warning lines for
// the level currently pointed to by volLevelIndex, then arms its first step.
static void beginVolLadderLevel(unsigned long now) {
  float levelFraction = volLevelsTable()[volLevelIndex];
  uint8_t count = volLevelCount();
  Serial.printf("[SPEAKER VOLTEST] Level %d/%d: %.0f%%\n", volLevelIndex + 1, count,
                (double)(levelFraction * 100.0f));
  if (levelFraction >= VOLTEST_HIGH_OUTPUT_THRESHOLD_FRACTION) {
    Serial.println(F("[SPEAKER VOLTEST] HIGH OUTPUT TEST"));
  }
  if (fabsf(levelFraction - VOLTEST_EIGHTY_PERCENT_WARNING_FRACTION) < 0.001f) {
    Serial.println(F("[SPEAKER VOLTEST] WARNING: 80% digital amplitude"));
  }
  if (fabsf(levelFraction - VOLTEST_FULL_SCALE_WARNING_FRACTION) < 0.001f) {
    Serial.println(F("[SPEAKER VOLTEST] WARNING: FULL-SCALE DIGITAL TEST"));
  }
  volStepIndex = 0;
  armVolLadderStep(now);
}

static void completeVolLadder() {
  volActive = false;
  volKind = VolLadderKind::NONE;
  Serial.println(F("[SPEAKER VOLTEST] COMPLETE"));
  Serial.println(F("[SPEAKER] Silence"));
}

// Called once the CURRENT step has genuinely finished (phase returned to
// SILENCE for a TONE/MELODY step, or a GAP's deadline passed) -- moves to
// the next step, the next level, or completes the whole ladder. This is the
// ONE place that changes volLevelIndex/volStepIndex, and it always does so
// only after the just-finished step has already reached digital silence --
// see this diagnostic's own safety requirement on never jumping amplitude
// mid-tone (a GAP step, by construction, only ever follows a step that has
// already faded out and gone silent).
static void advanceVolLadderStep(unsigned long now) {
  volStepIndex++;
  if (volStepIndex >= volStepCount()) {
    volLastCompletedLevel = volLevelIndex + 1;
    volLevelIndex++;
    if (volLevelIndex >= volLevelCount()) {
      completeVolLadder();
      return;
    }
    beginVolLadderLevel(now);
    return;
  }
  armVolLadderStep(now);
}

// Called every updateSpeakerTest() tick (i.e. every loop() iteration) --
// a no-op unless a ladder is active. Never blocks; every transition is
// driven by either phase==SILENCE (a bounded sub-test finished) or a
// millis() deadline (a GAP step finished), both checked non-blockingly.
void updateVolLadder(unsigned long now) {
  if (!volActive) return;

  // Safety: abort immediately on ANY bad I2S write outcome (error, zero, or
  // partial) observed on the write feedSpeakerChunk() just performed --
  // see lastWriteHadFault's own comment. Checked first, every tick,
  // regardless of which step is currently active. Deliberately stricter
  // than every other test in this file (which only escalate after several
  // consecutive failures) -- this diagnostic explicitly asked for immediate
  // abort on a single bad write, not just a sustained failure streak.
  if (lastWriteHadFault) {
    abortVolLadderIfActive(true);
    stopSpeakerTest();  // forces phase back to SILENCE immediately, same as 'k'
    return;
  }

  if (volWaitingForPhaseSilence) {
    if (phase == SpeakerPhase::SILENCE) {
      volWaitingForPhaseSilence = false;
      advanceVolLadderStep(now);
    }
    return;
  }

  // In a GAP step -- (long) cast handles millis() wraparound correctly,
  // the same idiom used throughout this codebase's other timing checks.
  if ((long)(now - volGapDeadlineMs) >= 0) {
    advanceVolLadderStep(now);
  }
}

bool startSpeakerVolTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  volActive = true;
  volKind = VolLadderKind::FULL;
  volLevelIndex = 0;
  volLastCompletedLevel = 0;
  volLastRunAborted = false;
  volLastAbortWasSafety = false;
  beginVolLadderLevel(millis());
  return true;
}

bool startSpeakerVolQuick() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  volActive = true;
  volKind = VolLadderKind::QUICK;
  volLevelIndex = 0;
  volLastCompletedLevel = 0;
  volLastRunAborted = false;
  volLastAbortWasSafety = false;
  beginVolLadderLevel(millis());
  return true;
}

bool stopSpeakerVolTest() {
  if (!volActive) {
    Serial.println(F("[SPEAKER VOLTEST] Not active"));
    return false;
  }
  // stopSpeakerMusic() both aborts the ladder's own bookkeeping (it calls
  // abortVolLadderIfActive() internally) and forces audio back to silence
  // immediately, regardless of which sub-step was mid-flight -- same
  // immediate (not wait-for-completion) semantics as 'k'/'speaker stop' for
  // every other test in this file.
  stopSpeakerMusic();
  return true;
}

void printSpeakerVolStatus() {
  Serial.printf("[SPEAKER VOLTEST] active=%d\n", volActive ? 1 : 0);
  if (volActive) {
    const char *typeName = (volKind == VolLadderKind::FULL) ? "FULL" : "QUICK";
    float amplitude = volLevelsTable()[volLevelIndex];
    const VolLadderStepDef &step = volStepsTable()[volStepIndex];
    const char *stageName;
    switch (step.action) {
      case VolLadderStepAction::TONE: stageName = "TONE"; break;
      case VolLadderStepAction::MELODY: stageName = "MELODY"; break;
      case VolLadderStepAction::GAP: stageName = "GAP"; break;
      default: stageName = "?"; break;
    }
    Serial.printf("[SPEAKER VOLTEST]   type=%s level=%d/%d amplitude=%.0f%% stage=%s", typeName, volLevelIndex + 1,
                  volLevelCount(), (double)(amplitude * 100.0f), stageName);
    if (step.action == VolLadderStepAction::TONE) Serial.printf(" (%.0fHz)", (double)step.frequencyHz);
    Serial.println();
  } else {
    Serial.println(F("[SPEAKER VOLTEST]   type=NONE"));
  }
  Serial.printf("[SPEAKER VOLTEST]   lastCompletedLevel=%d abortedManually=%d abortedSafety=%d\n",
                volLastCompletedLevel, (volLastRunAborted && !volLastAbortWasSafety) ? 1 : 0,
                (volLastRunAborted && volLastAbortWasSafety) ? 1 : 0);
}

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
  // Bench-harness diagnostic lines, exact requested format -- additive to
  // (not a replacement for) the pin line printed above, which predates this
  // bench work and is kept for backward compatibility with existing logs.
  Serial.printf("[SPEAKER] Pins: BCLK=%d WS=%d DOUT=%d\n", I2S_BCLK_PIN, I2S_WS_PIN, I2S_SPEAKER_DOUT_PIN);
  Serial.printf("[SPEAKER] Sample rate: %d Hz\n", I2S_SAMPLE_RATE);
  Serial.println(F("[SPEAKER] Digital silence active"));
  Serial.println(F("[SPEAKER] Connect MAX98357A SD to 3.3V now"));
  printGpio16Routing("immediately after initSpeakerTest()");

  speakerReady = true;
  printSpeakerVolume();  // V1.1 boot default -- "[SPEAKER] Volume: 70%" (not persisted across reboot)
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
  // explicit command can play another). Disabled by default as of the
  // Stage S1 speaker bring-up work -- see ENABLE_SPEAKER_AUTO_DEMO_TONE's
  // comment in Config.h. Sunny's default boot must stay speaker-silent
  // until a human issues an explicit tone command.
#if ENABLE_SPEAKER_AUTO_DEMO_TONE
  if (!autoDemoToneFired && (now - initCompleteMs) >= SPEAKER_AUTO_DEMO_DELAY_MS) {
    autoDemoToneFired = true;
    if (phase == SpeakerPhase::SILENCE) {
      Serial.println(F("[SPEAKER] Automatic demonstration tone"));
      startTest(TestKind::SINE, SPEAKER_TONE_FREQUENCY_HZ, SPEAKER_TONE_DURATION_MS, SPEAKER_TONE_RAMP_MS,
                SPEAKER_TONE_AMPLITUDE_FRACTION, "440 Hz");
    }
  }
#endif

  feedSpeakerChunk();
  updateVolLadder(now);  // no-op unless 'speaker voltest'/'volquick' is active -- see its own comment

  // Periodic "still active" reminder for 'speaker silencecheck'/
  // 'carriercheck' -- neither has a fixed duration, so this is the only
  // ongoing serial evidence that the check is still running (vs. having
  // silently ended) while a human listens.
  if ((silenceCheckActive || carrierCheckActive) &&
      (now - lastSilenceCheckStatusMs) >= SPEAKER_SILENCECHECK_STATUS_INTERVAL_MS) {
    lastSilenceCheckStatusMs = now;
    unsigned long elapsedS = (now - silenceCheckStartMs) / 1000;
    Serial.printf("[SPEAKER] %s still active (%lus elapsed) -- 'speaker stop' to end\n",
                  carrierCheckActive ? "Carrier check" : "Silence check", elapsedS);
  }
}

void stopSpeakerTest() {
  // 'k'/emergency stop must cancel an active volume-ladder run too, not
  // just whatever single tone happens to be mid-flight -- otherwise the
  // ladder would silently resume advancing once phase next reaches
  // SILENCE. Idempotent (no-op if no ladder is running).
  abortVolLadderIfActive(false);
  bool wasPlaying = (phase == SpeakerPhase::TONE_PLAYING);
  bool wasChecking = silenceCheckActive || carrierCheckActive;
  clearActiveChecks();
  phase = SpeakerPhase::SILENCE;
  toneSampleIndex = 0;
  diagCallsRemaining = 0;
  currentSong = nullptr;  // defensive tidiness -- harmless either way (static data, never dangling)
  songTotalDurationMs = 0;
  if (wasPlaying) Serial.println(F("[SPEAKER] Emergency stop -- digital silence active"));
  else if (wasChecking) Serial.println(F("[SPEAKER] Emergency stop -- silence/carrier check ended"));
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

// Stage S2 bring-up tone -- see include/SpeakerTest.h and
// SPEAKER_BRINGUP_TONE_* in Config.h for the full rationale. Deliberately
// does not reuse startSpeakerTestTone()'s ('t') parameters or the 'loud'
// path -- its own conservative constants, its own explicit confirmation
// line naming every parameter a first-time physical test needs to know.
bool startSpeakerBringupTone() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  Serial.printf(
      "[SPEAKER] Stage S2 bring-up tone: frequency=%.0fHz amplitude=%.0f%% sampleRate=%dHz duration=%lums\n",
      (double)SPEAKER_BRINGUP_TONE_FREQUENCY_HZ, (double)(SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION * 100.0f),
      I2S_SAMPLE_RATE, (unsigned long)SPEAKER_BRINGUP_TONE_DURATION_MS);
  startTest(TestKind::SINE, SPEAKER_BRINGUP_TONE_FREQUENCY_HZ, SPEAKER_BRINGUP_TONE_DURATION_MS,
            SPEAKER_BRINGUP_TONE_RAMP_MS, SPEAKER_BRINGUP_TONE_AMPLITUDE_FRACTION, "440 Hz (Stage S2 bring-up)");
  return true;
}

// Same underlying stop path as stopSpeakerMusic() (see that function) --
// a differently-named entry point for the 'speaker stop' command, not a
// second implementation.
bool stopSpeakerBringupTone() { return stopSpeakerMusic(); }

// ----------------------------------------------------------------------------
// Bring-up test bench ('speaker t'/'1'/'2'/'3'/'volume'/'v'/'+'/'-'/'h') --
// see the SPEAKER_BENCH_*/SPEAKER_VOLUME_* constants in Config.h and this
// file's own header comment for why these live under the 'speaker' word
// prefix rather than bare single-char tokens. Shares startTest()'s
// scheduler/ramp/silence machinery with every test above -- no second I2S
// write path.
// ----------------------------------------------------------------------------

// Common entry point for 't'/'1'/'2'/'3' -- looks up the preset table,
// prints the exact requested confirmation line, then hands off to the
// existing startTest() scheduler at the current normal speaker volume.
static bool startSpeakerBenchPreset(uint8_t presetIndex) {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  const SpeakerBenchPreset &preset = SPEAKER_BENCH_PRESETS[presetIndex];
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Tone: %.0f Hz | amplitude=%.0f%% | duration=%lu ms\n", (double)preset.frequencyHz,
                (double)(amplitudeFraction * 100.0f), (unsigned long)preset.durationMs);
  char label[16];
  snprintf(label, sizeof(label), "%.0f Hz", (double)preset.frequencyHz);
  startTest(TestKind::SINE, preset.frequencyHz, preset.durationMs, SPEAKER_BENCH_TONE_RAMP_MS, amplitudeFraction,
            label);
  return true;
}

bool startSpeakerBenchT() { return startSpeakerBenchPreset(0); }
bool startSpeakerBench1() { return startSpeakerBenchPreset(1); }
bool startSpeakerBench2() { return startSpeakerBenchPreset(2); }
bool startSpeakerBench3() { return startSpeakerBenchPreset(3); }

// Loudness label for the exact format requested for V1.1: "[SPEAKER]
// Volume: 70%" at 35-70%, then "(LOUD)"/"(HIGH)"/"(MAX)" at 80/90/100%
// specifically -- not a general >=threshold rule, so a future ladder change
// doesn't silently start labeling an unintended step.
static const char *speakerVolumeLabel(float fraction) {
  if (fabsf(fraction - 1.00f) < 0.001f) return " (MAX)";
  if (fabsf(fraction - 0.90f) < 0.001f) return " (HIGH)";
  if (fabsf(fraction - 0.80f) < 0.001f) return " (LOUD)";
  return "";
}

void printSpeakerVolume() {
  float fraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Volume: %.0f%%%s\n", (double)(fraction * 100.0f), speakerVolumeLabel(fraction));
}

void speakerVolumeUp() {
  if (speakerVolumeIndex + 1 < SPEAKER_VOLUME_STEP_COUNT) speakerVolumeIndex++;
  else Serial.println(F("[SPEAKER] Volume already at maximum (100%)"));
  printSpeakerVolume();
}

void speakerVolumeDown() {
  if (speakerVolumeIndex > 0) speakerVolumeIndex--;
  else Serial.println(F("[SPEAKER] Volume already at minimum (35%)"));
  printSpeakerVolume();
}

bool setSpeakerVolumePercent(uint8_t percent) {
  for (uint8_t i = 0; i < SPEAKER_VOLUME_STEP_COUNT; i++) {
    if ((uint8_t)lroundf(SPEAKER_VOLUME_STEPS_FRACTION[i] * 100.0f) == percent) {
      speakerVolumeIndex = i;
      printSpeakerVolume();
      return true;
    }
  }
  Serial.printf("[SPEAKER] %u%% is not a supported volume level -- use one of: 35/50/60/70/80/90/100\n",
                (unsigned)percent);
  return false;
}

void printSpeakerBenchHelp() {
  Serial.println(F("[SPEAKER] Bring-up test bench commands:"));
  Serial.println(F("[SPEAKER]   speaker t    = 440 Hz, 750 ms, at current volume"));
  Serial.println(F("[SPEAKER]   speaker 1    = 220 Hz, 750 ms, at current volume"));
  Serial.println(F("[SPEAKER]   speaker 2    = 440 Hz, 750 ms, at current volume"));
  Serial.println(F("[SPEAKER]   speaker 3    = 880 Hz, 500 ms, at current volume"));
  Serial.println(F("[SPEAKER]   speaker stop = immediate stop, continuous digital silence"));
  Serial.println(F("[SPEAKER]   speaker volume            = print current volume"));
  Serial.println(F("[SPEAKER]   speaker volume <percent>  = set volume (35/50/60/70/80/90/100)"));
  Serial.println(F("[SPEAKER]   speaker volume up|down    = step volume (alias: speaker +|-)"));
  Serial.println(F("[SPEAKER]   speaker v    = alias for 'speaker volume'"));
  Serial.println(F("[SPEAKER]   speaker h    = this help"));
  Serial.println(F("[SPEAKER]   speaker sweep      = 150->3000 Hz sweep, ~6s, at current volume"));
  Serial.println(F("[SPEAKER]   speaker melody     = original diagnostic melody, ~9s, at current volume"));
  Serial.println(F("[SPEAKER]   speaker chord      = C4-E4-G4-C5 arpeggio up/down, ~5s, at current volume"));
  Serial.println(F("[SPEAKER]   speaker lowmidhigh = 150/440/1500 Hz in sequence, at current volume"));
  Serial.println(F("[SPEAKER]   speaker speechtest = original synthetic speech-like diagnostic, ~10s"));
  Serial.println(F("[SPEAKER]   speaker musictest  = original short musical diagnostic, ~11s"));
  Serial.println(F("[SPEAKER]   speaker noise  = white noise, 2s, capped at 10% regardless of volume"));
  Serial.println(F("[SPEAKER]   speaker silencecheck = continuous digital zero until 'speaker stop'"));
  Serial.println(F("[SPEAKER]   speaker carriercheck = same, plus I2S clock/pin diagnostics"));
  Serial.println(F("[SPEAKER]   speaker isolate on|off|status = diagnostic motor-stop + LED-mute mode"));
  Serial.println(F("[SPEAKER]   speaker voltest   = automatic 2->100% multi-tone volume ladder"));
  Serial.println(F("[SPEAKER]   speaker volquick  = shorter automatic volume ladder"));
  Serial.println(F("[SPEAKER]   speaker volstop   = abort volume test and return to silence"));
  Serial.println(F("[SPEAKER]   speaker volstatus = show automatic-volume-test state"));
}

// ----------------------------------------------------------------------------
// Stage S3 buzz/distortion format diagnostic ('speaker fmt1'/'fmt2'/'fmt3'/
// 'fmtstatus') -- see fmtDiagSample() above and the SPEAKER_FMT_DIAG_*
// constants in Config.h for the full rationale. Shares startTest()'s
// scheduler/ramp/silence-return machinery with every test in this file --
// no second I2S write path, no pin/clock/format changes to SharedI2S.
// ----------------------------------------------------------------------------

static const char *fmtDiagFormatLabel(SpeakerFmtDiagFormat fmt) {
  switch (fmt) {
    case SpeakerFmtDiagFormat::FMT1_16IN32: return "fmt1: 16-bit signed, MSB-justified in bits[31:16]";
    case SpeakerFmtDiagFormat::FMT2_24IN32: return "fmt2: 24-bit signed, left-justified in bits[31:8]";
    case SpeakerFmtDiagFormat::FMT3_32DIRECT: return "fmt3: 32-bit signed, full-scale in bits[31:0]";
  }
  return "unknown";
}

static bool startSpeakerFmtDiag(SpeakerFmtDiagFormat fmt) {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  currentFmtDiagFormat = fmt;
  lastStartedFmtDiagFormat = fmt;
  fmtDiagEverStarted = true;
  Serial.printf("[SPEAKER] Format test: %s | %.0f Hz | amplitude=%.0f%% | duration=%lu ms\n", fmtDiagFormatLabel(fmt),
                (double)SPEAKER_FMT_DIAG_FREQUENCY_HZ, (double)(SPEAKER_FMT_DIAG_AMPLITUDE_FRACTION * 100.0f),
                (unsigned long)SPEAKER_FMT_DIAG_DURATION_MS);
  startTest(TestKind::FMT_DIAG, SPEAKER_FMT_DIAG_FREQUENCY_HZ, SPEAKER_FMT_DIAG_DURATION_MS, SPEAKER_FMT_DIAG_RAMP_MS,
            SPEAKER_FMT_DIAG_AMPLITUDE_FRACTION, fmtDiagFormatLabel(fmt));
  return true;
}

bool startSpeakerFmt1() { return startSpeakerFmtDiag(SpeakerFmtDiagFormat::FMT1_16IN32); }
bool startSpeakerFmt2() { return startSpeakerFmtDiag(SpeakerFmtDiagFormat::FMT2_24IN32); }
bool startSpeakerFmt3() { return startSpeakerFmtDiag(SpeakerFmtDiagFormat::FMT3_32DIRECT); }

void printSpeakerFmtStatus() {
  Serial.println(F("[SPEAKER FMT] fmt1: 16-bit signed sine, MSB-justified into bits[31:16] of each 32-bit "
                    "slot, bits[15:0]=0, duplicated L=R"));
  Serial.println(F("[SPEAKER FMT] fmt2: 24-bit signed sine, left-justified into bits[31:8] of each 32-bit "
                    "slot, bits[7:0]=0, duplicated L=R"));
  Serial.println(F("[SPEAKER FMT] fmt3: 32-bit signed sine, full-scale across bits[31:0] of each 32-bit "
                    "slot, duplicated L=R"));
  Serial.printf("[SPEAKER FMT] shared bus bits-per-slot=32 (SharedI2S.cpp, fixed for both RX and TX)\n");
  if (fmtDiagEverStarted) {
    Serial.printf("[SPEAKER FMT] last started: %s | active now=%d\n", fmtDiagFormatLabel(lastStartedFmtDiagFormat),
                  (currentTestKind == TestKind::FMT_DIAG && phase == SpeakerPhase::TONE_PLAYING) ? 1 : 0);
  } else {
    Serial.println(F("[SPEAKER FMT] no fmt1/2/3 test has been started yet this session"));
  }
}

// ----------------------------------------------------------------------------
// Multi-tone speaker bring-up tests ('speaker sweep'/'melody'/'chord'/
// 'noise') -- for judging the MAX98357A + a real speaker load more
// realistically than a single 440Hz tone. All reuse this file's existing
// generic engines/scheduler unchanged (sweepSample()/boundedNoteSequenceSample()/
// noiseSample(), all already generic over currentX state) -- no second I2S
// write path, no new TX pin/clock/format. Sweep/melody/chord all read the
// CURRENT bench volume ('speaker v'/'+'/'-') live at start time; noise is
// independently capped -- see SPEAKER_BENCH_NOISE_MAX_AMPLITUDE_FRACTION's
// own comment in Config.h.
// ----------------------------------------------------------------------------

bool startSpeakerBenchSweep() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Sweep: %.0f -> %.0f Hz | %lus | amplitude=%.0f%%\n", (double)SPEAKER_SWEEP_START_HZ,
                (double)SPEAKER_SWEEP_END_HZ, (unsigned long)(SPEAKER_BENCH_SWEEP_DURATION_MS / 1000),
                (double)(amplitudeFraction * 100.0f));
  currentSweepStartHz = SPEAKER_SWEEP_START_HZ;
  currentSweepEndHz = SPEAKER_SWEEP_END_HZ;
  startTest(TestKind::SWEEP, 0.0f, SPEAKER_BENCH_SWEEP_DURATION_MS, SPEAKER_BENCH_SWEEP_RAMP_MS, amplitudeFraction,
            "sweep (150-3000Hz bench)");
  return true;
}

bool startSpeakerBenchMelody() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Melody diagnostic started | amplitude=%.0f%%\n", (double)(amplitudeFraction * 100.0f));
  armBenchNotes(SPEAKER_BENCH_MELODY_NOTES, SPEAKER_BENCH_MELODY_NOTE_COUNT);
  startTest(TestKind::BENCH_NOTES, 0.0f, currentBenchNotesTotalMs, 0, amplitudeFraction, "melody diagnostic");
  return true;
}

bool startSpeakerBenchChord() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.println(F("[SPEAKER] Chord/arpeggio diagnostic started"));
  armBenchNotes(SPEAKER_BENCH_CHORD_NOTES, SPEAKER_BENCH_CHORD_NOTE_COUNT);
  startTest(TestKind::BENCH_NOTES, 0.0f, currentBenchNotesTotalMs, 0, amplitudeFraction, "chord/arpeggio diagnostic");
  return true;
}

bool startSpeakerBenchNoise() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  // "Maximum 10% amplitude regardless of current bench volume" -- never
  // louder than the cap, and never louder than whatever the user's bench
  // volume is currently set to, whichever is lower.
  float amplitudeFraction =
      fminf(SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex], SPEAKER_BENCH_NOISE_MAX_AMPLITUDE_FRACTION);
  Serial.printf("[SPEAKER] Noise diagnostic started | capped=%.0f%%\n",
                (double)(SPEAKER_BENCH_NOISE_MAX_AMPLITUDE_FRACTION * 100.0f));
  startTest(TestKind::NOISE, 0.0f, SPEAKER_BENCH_NOISE_DURATION_MS, NOISE_RAMP_MS, amplitudeFraction,
            "noise diagnostic");
  return true;
}

// ----------------------------------------------------------------------------
// V1.1 buzz/noise isolation diagnostics ('speaker silencecheck'/
// 'carriercheck') -- see include/SpeakerTest.h for the full rationale. Both
// force phase back to SILENCE (continuous digital zero, the same signal
// already transmitted between every test in this file) and hold it there
// under a human-readable label until 'speaker stop'/'stopmusic'/'k' ends it
// -- no fixed duration, no new I2S write path.
// ----------------------------------------------------------------------------

bool startSpeakerSilenceCheck() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  stopSpeakerMusic();  // clears any playing test/ladder, forces phase back to SILENCE, prints "Stopped"
  silenceCheckActive = true;
  carrierCheckActive = false;
  silenceCheckStartMs = millis();
  lastSilenceCheckStatusMs = silenceCheckStartMs;
  Serial.println(F("[SPEAKER] Silence check ACTIVE -- true digital silence (no tone), continuous"));
  Serial.println(F("[SPEAKER] Microphone capture continues normally. Issue 'speaker stop' to end."));
  return true;
}

bool startSpeakerCarrierCheck() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  stopSpeakerMusic();
  carrierCheckActive = true;
  silenceCheckActive = false;
  silenceCheckStartMs = millis();
  lastSilenceCheckStatusMs = silenceCheckStartMs;
  Serial.println(F("[SPEAKER] Carrier check ACTIVE -- I2S clocks running, zero samples only (no tone)"));
  printSharedI2SStatus();
  printGpio16Routing("carrier check");
  Serial.printf("[SPEAKER DIAG] i2s_get_clk(I2S_NUM_0)=%.1f Hz\n", i2s_get_clk(I2S_PORT));
  Serial.println(F("[SPEAKER] Microphone capture continues normally. Issue 'speaker stop' to end."));
  return true;
}

// ----------------------------------------------------------------------------
// V1.1 realistic-content diagnostics -- 'speaker lowmidhigh'/'speechtest'/
// 'musictest'. lowmidhigh/speechtest reuse boundedNoteSequenceSample() (the
// SAME engine 'speaker melody'/'chord' already use, via armBenchNotes());
// musictest uses its own small dynamicNoteSequenceSample() engine (see that
// function's own comment for why). All three read the CURRENT normal
// speaker volume live at start time, same as melody/chord/sweep.
// ----------------------------------------------------------------------------

bool startSpeakerLowMidHigh() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Low/mid/high diagnostic started | 150/440/1500 Hz | amplitude=%.0f%%\n",
                (double)(amplitudeFraction * 100.0f));
  armBenchNotes(SPEAKER_LOWMIDHIGH_NOTES, SPEAKER_LOWMIDHIGH_NOTE_COUNT);
  startTest(TestKind::BENCH_NOTES, 0.0f, currentBenchNotesTotalMs, 0, amplitudeFraction, "low/mid/high diagnostic");
  return true;
}

bool startSpeakerSpeechTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Speech-like diagnostic started (original synthetic content) | amplitude=%.0f%%\n",
                (double)(amplitudeFraction * 100.0f));
  armBenchNotes(SPEAKER_SPEECHTEST_NOTES, SPEAKER_SPEECHTEST_NOTE_COUNT);
  startTest(TestKind::BENCH_NOTES, 0.0f, currentBenchNotesTotalMs, 0, amplitudeFraction, "speech-like diagnostic");
  return true;
}

// Arms dynamicNoteSequenceSample()'s table/count/total-duration state, same
// pattern as armBenchNotes() above but for the amplitude-carrying DynamicNote
// shape 'speaker musictest' needs.
static void armMusicTestNotes(const DynamicNote *notes, uint8_t count) {
  currentMusicTestNotes = notes;
  currentMusicTestNoteCount = count;
  uint32_t total = 0;
  for (uint8_t i = 0; i < count; i++) total += notes[i].durationMs;
  currentMusicTestTotalMs = total;
}

bool startSpeakerMusicTest() {
  if (!speakerReady) {
    Serial.println(F("[SPEAKER] Refused -- I2S TX not initialized"));
    return false;
  }
  float amplitudeFraction = SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex];
  Serial.printf("[SPEAKER] Music diagnostic started (original synthetic content) | amplitude=%.0f%% (peak)\n",
                (double)(amplitudeFraction * 100.0f));
  armMusicTestNotes(SPEAKER_MUSICTEST_NOTES, SPEAKER_MUSICTEST_NOTE_COUNT);
  startTest(TestKind::MUSICTEST_NOTES, 0.0f, currentMusicTestTotalMs, 0, amplitudeFraction, "music diagnostic");
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
  // 'speaker stop'/'stopmusic' (and, indirectly, 'speaker volstop' -- see
  // stopSpeakerVolTest()) must also cancel an active volume-ladder run.
  // Idempotent (no-op if no ladder is running); harmless if the caller
  // already called this directly beforehand.
  abortVolLadderIfActive(false);
  bool wasChecking = silenceCheckActive || carrierCheckActive;
  clearActiveChecks();
  phase = SpeakerPhase::SILENCE;
  toneSampleIndex = 0;
  diagCallsRemaining = 0;
  currentSong = nullptr;
  songTotalDurationMs = 0;
  if (wasChecking) Serial.println(F("[SPEAKER] Silence/carrier check ended"));
  Serial.println(F("[SPEAKER] Stopped"));
  return true;
}

// ----------------------------------------------------------------------------
// V1.1 noise-isolation mode ('speaker isolate on'/'off'/'status') -- see the
// SPEAKER_ISOLATE_* comment in Config.h for the full rationale and its
// "best-effort, not a hard interlock" caveat. Calls only MotorDriver's own
// exported motorStop() and Controls.h's existing isMuted()/setMuted() --
// the SAME public APIs every other behavior layer/diagnostic already uses,
// not a new owner of either resource (see AGENTS.md's single-owner table).
// ----------------------------------------------------------------------------

namespace {
bool speakerIsolateActive = false;
bool speakerIsolatePriorMuteState = false;
}  // namespace

void speakerIsolateOn() {
  if (speakerIsolateActive) {
    Serial.println(F("[SPEAKER ISOLATE] Already ON"));
    return;
  }
  speakerIsolatePriorMuteState = isMuted();
  motorStop();
  setMuted(true);
  speakerIsolateActive = true;
  Serial.println(F("[SPEAKER ISOLATE] ON -- motor commanded stopped, LEDs muted (diagnostic only)"));
  Serial.println(
      F("[SPEAKER ISOLATE] NOTE: best-effort, not a hard interlock -- an independently-active motor "
        "behavior (e.g. MusicMotorController) can still re-engage the motor during this window"));
}

void speakerIsolateOff() {
  if (!speakerIsolateActive) {
    Serial.println(F("[SPEAKER ISOLATE] Already OFF"));
    return;
  }
  setMuted(speakerIsolatePriorMuteState);
  speakerIsolateActive = false;
  Serial.println(F("[SPEAKER ISOLATE] OFF -- prior LED mute state restored"));
}

void printSpeakerIsolateStatus() {
  Serial.printf("[SPEAKER ISOLATE] active=%d\n", speakerIsolateActive ? 1 : 0);
}

bool isSpeakerIsolateActive() { return speakerIsolateActive; }

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
  Serial.printf("[SPEAKER] volume=%.0f%% silenceCheck=%d carrierCheck=%d isolate=%d\n",
                (double)(SPEAKER_VOLUME_STEPS_FRACTION[speakerVolumeIndex] * 100.0f), silenceCheckActive ? 1 : 0,
                carrierCheckActive ? 1 : 0, speakerIsolateActive ? 1 : 0);
}
