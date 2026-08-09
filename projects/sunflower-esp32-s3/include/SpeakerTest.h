#pragma once
#include <stdint.h>

// MAX98357A speaker-output diagnostic suite. Writes to the SAME shared
// full-duplex I2S_NUM_0 bus the INMP441 microphone (AudioAnalyzer.cpp)
// reads from -- see include/SharedI2S.h, the sole owner of that port's
// i2s_driver_install()/i2s_set_pin() configuration. This module never
// installs, reconfigures, or uninstalls the I2S driver itself; it only
// calls i2s_write()/i2s_zero_dma_buffer() on the port SharedI2S.cpp
// already brought up.
//
// Generates digital silence continuously and a set of short, clearly
// human-audible test signals only on explicit request (serial commands
// below, plus one automatic 't'-equivalent demonstration play shortly
// after init) -- never routes live microphone audio to the speaker, so
// there is no acoustic-feedback risk. See README's "Speaker hardware
// test" section for the required SD-pin startup sequence.
//
// Also includes a small procedural music player ('music1'-'music4',
// 'stopmusic') built on a reusable Note/Song data model and one generic
// playback engine (SpeakerTest.cpp's songSample()) -- see that file's
// top-of-file comment for how this is deliberately structured so a future
// sample-based source (playwav/playpcm/playtts) can plug into the exact
// same scheduler without changes.
//
// Every command below INTERRUPTS any test already playing and starts the
// new one cleanly (no "already playing" refusal) -- see startTest() in
// SpeakerTest.cpp. Emergency stop ('k') always wins immediately via
// stopSpeakerTest().
//
// Development/bring-up feature -- clearly isolated so it can be removed or
// expanded (e.g. real audio playback) later without touching the
// microphone, LED, motor, or button subsystems.

// Verifies the shared I2S bus (see include/SharedI2S.h) is ready and
// (re-)confirms digital silence is being transmitted via
// i2s_zero_dma_buffer() -- does NOT install or configure the I2S driver
// itself. Prints the required diagnostic banner, including "[SPEAKER]
// Connect MAX98357A SD to 3.3V now" once silence is confirmed active. Call
// this after initSharedI2S() has already succeeded (and, by convention,
// after initAudioAnalyzer()).
void initSpeakerTest();

// Must be called every loop() iteration, unconditionally. Feeds the shared
// I2S TX DMA buffer with either continuous digital silence or the
// in-progress test signal; never stops the peripheral. No-op if
// initSpeakerTest() did not succeed.
//
// NOTE ON BLOCKING: i2s_write() here uses a small BOUNDED wait
// (SPEAKER_WRITE_TICKS_TO_WAIT in SpeakerTest.cpp, from pdMS_TO_TICKS(20)),
// never 0 and never portMAX_DELAY. This means every call to this function
// can block for up to that bound.
void updateSpeakerTest(unsigned long now);

// Immediately stops any in-progress test and returns to continuous
// digital silence -- called from main.cpp's serviceEmergencyStop() so 'k'
// always wins. Idempotent; safe to call even if nothing is playing.
void stopSpeakerTest();

// --- Test commands -- each interrupts any test already playing (see this
// header's own top-of-file note) and returns false only if the speaker
// itself failed to initialize. All are non-blocking: they arm state that
// updateSpeakerTest() plays out over subsequent loop() iterations. ---

// 't': 440Hz sine, ~2s, ramped, ~7.5% amplitude (original test, unchanged).
bool startSpeakerTestTone();
// 's': 440Hz square wave, ~2s, ramped, ~5% amplitude (original test, unchanged).
bool startSpeakerSquareWaveTest();
// 'low': 150Hz sine, 2s, ramped, ~10% amplitude.
bool startSpeakerLowTest();
// 'mid': 440Hz sine, 2s, ramped, ~10% amplitude.
bool startSpeakerMidTest();
// 'high': 1500Hz sine, 2s, ramped, ~10% amplitude.
bool startSpeakerHighTest();
// 'sweep': logarithmic sweep 150Hz -> 3000Hz over 4s, ramped.
bool startSpeakerSweepTest();
// 'melody': C5 E5 G5 C6, 350ms/note + 100ms gap, played twice.
bool startSpeakerMelodyTest();
// 'beep': 1000Hz, 150ms on / 150ms off, 5 repeats.
bool startSpeakerBeepTest();
// 'noise': white noise, 1s, ~5% amplitude.
bool startSpeakerNoiseTest();
// 'loud': TEMPORARY DIAGNOSTIC ONLY -- 1000Hz, 500ms, capped at 20%
// amplitude (never exceeded). Prints an explicit warning when triggered.
bool startSpeakerLoudTest();

// 'speaker tone': the Stage S2 bring-up test (see
// docs/SPEAKER_BRINGUP_PLAN.md) -- 440Hz, 300ms, 5% amplitude (see
// SPEAKER_BRINGUP_TONE_* in Config.h for the full rationale). Explicit
// serial command only; never fires automatically. Prints the selected
// frequency/amplitude/sample-rate/duration before playing. Refuses (same
// as every other test here) if the speaker never initialized. Bounded
// duration, auto-stops, non-blocking, cannot latch on indefinitely --
// same safe scheduler every other test in this file already uses. Does
// not touch MusicMotorController, Audio Mode, or the microphone in any
// way.
bool startSpeakerBringupTone();

// 'speaker stop': explicit stop for the above (also works as a general
// "stop whatever's playing" command, same as 'stopmusic' -- this is a
// thin, differently-named entry point to the same underlying stop logic,
// not a second implementation).
bool stopSpeakerBringupTone();

// --- 'speaker t'/'speaker 1'/'speaker 2'/'speaker 3'/'speaker v'/
// 'speaker +'/'speaker -'/'speaker h': the gain/volume bring-up test bench
// (see the SPEAKER_BENCH_* constants in Config.h for the full rationale,
// including why these are namespaced under 'speaker' rather than given
// bare single-char tokens -- 't'/'s'/'v'/'h'/'+'/'-' are all already
// reserved elsewhere, and bare '1'/'2'/'3' are live, no-Enter motor test
// triggers in main.cpp). 'speaker stop' (above) doubles as this bench's
// stop command -- same underlying stopSpeakerMusic(), not a second one. ---

// 'speaker t': 440Hz, 750ms, at the current bench volume (SPEAKER_BENCH_PRESETS[0]).
bool startSpeakerBenchT();
// 'speaker 1': 220Hz, 750ms, at the current bench volume (SPEAKER_BENCH_PRESETS[1]).
bool startSpeakerBench1();
// 'speaker 2': 440Hz, 750ms, at the current bench volume (SPEAKER_BENCH_PRESETS[2]).
bool startSpeakerBench2();
// 'speaker 3': 880Hz, 500ms, at the current bench volume (SPEAKER_BENCH_PRESETS[3]).
bool startSpeakerBench3();

// --- V1.1 normal-use volume model (see SPEAKER_VOLUME_* in Config.h) --
// 'speaker t'/'1'/'2'/'3'/'sweep'/'melody'/'chord'/'lowmidhigh'/'speechtest'/
// 'musictest' all read this SAME live volume, one model for normal speaker
// output. 'speaker v'/'+'/'-' are aliases for the three functions below --
// same underlying volume, kept for continuity with the pre-V1.1 bring-up
// bench naming. ---

// 'speaker volume' / 'speaker v': prints the current volume (step index +
// percent), with a loudness label at 80/90/100% -- see
// SPEAKER_VOLUME_STEPS_FRACTION in Config.h.
void printSpeakerVolume();
// 'speaker volume up' / 'speaker +': steps to the next-louder entry in
// SPEAKER_VOLUME_STEPS_FRACTION, clamped at the last (100%) step. Prints
// the new value.
void speakerVolumeUp();
// 'speaker volume down' / 'speaker -': steps to the next-quieter entry,
// clamped at the first (35%) step. Prints the new value.
void speakerVolumeDown();
// 'speaker volume <percent>': sets the volume directly to one of the
// supported ladder steps (35/50/60/70/80/90/100). Returns false and prints
// a rejection message (listing the supported values) for any other
// percent -- never rounds or clamps to the nearest supported step.
bool setSpeakerVolumePercent(uint8_t percent);
// 'speaker h': prints this bench's own command list (distinct from the
// general 'h'/printHelp()).
void printSpeakerBenchHelp();

// --- Stage S3 buzz/distortion format diagnostic: 'speaker fmt1'/'fmt2'/
// 'fmt3'/'fmtstatus'. Added after a full source-level review of this
// module's and SharedI2S's sample formatting (mono/stereo duplication,
// slot order, sign handling, overflow, phase continuity across DMA-buffer
// boundaries, double amplitude scaling) found no coding defect -- see the
// task report for the full checklist result. The shared bus is hard-fixed
// at 32 bits-per-slot for both RX and TX (SharedI2S.cpp; changing it would
// also change the microphone's verified RX format, out of scope here), so
// what these three isolate is which PORTION of that 32-bit slot carries
// real data -- a question only physical A/B listening can settle. All
// three: 440Hz, 750ms, fixed 2% amplitude (independent of the 'speaker v'/
// '+'/'-' bench ladder), 20ms fade in/out, mono duplicated to both I2S
// stereo slots, continuous phase accumulator (no reset between DMA
// buffers), returns to continuous digital silence on completion -- same
// safe scheduler every other test in this file already uses. Refuses (same
// as every other test) if the speaker never initialized. ---

// 'speaker fmt1': 16-bit signed sine, MSB-justified into bits[31:16] of
// each 32-bit slot (bits[15:0]=0) -- this is what the shared bus already
// produces today for every other tone/test in this file.
bool startSpeakerFmt1();
// 'speaker fmt2': 24-bit signed sine, left-justified into bits[31:8] of
// each 32-bit slot (bits[7:0]=0) -- the conventional "24-bit audio in a
// 32-bit container" packing.
bool startSpeakerFmt2();
// 'speaker fmt3': 32-bit signed sine occupying the full 32-bit slot,
// bits[31:0] -- the only one of the three with no zero-padded bits, i.e.
// the one that uses every bit the hardware is actually clocking out at the
// shared bus's fixed 32-bit slot width.
bool startSpeakerFmt3();
// 'speaker fmtstatus': prints all three formats' exact bit layout plus
// which one was last started (and whether it's still playing).
void printSpeakerFmtStatus();

// --- Multi-tone speaker bring-up tests -- 'speaker sweep'/'melody'/
// 'chord'/'noise'. For judging the MAX98357A + a real speaker load more
// realistically than a single tone. All reuse this file's existing generic
// engines (sweep/note-sequence/noise) and the same safe scheduler as every
// other test above -- no second I2S write path, no pin/clock/format
// changes. Sweep/melody/chord use the CURRENT bench volume ('speaker v'/
// '+'/'-'), read live when each test starts; noise is independently capped
// regardless of bench volume (see its own comment below). Refuses (same as
// every other test) if the speaker never initialized. ---

// 'speaker sweep': smooth logarithmic sine sweep, 150Hz -> 3000Hz (same
// range as the existing bare 'sweep' test), ~6s, 20ms fade in/out, at the
// current bench volume.
bool startSpeakerBenchSweep();
// 'speaker melody': an original ~8.8s diagnostic phrase (not a copy or
// encoding of any existing song) spanning roughly 220-880Hz, mixing short
// and sustained notes with brief silent gaps, at the current bench volume.
bool startSpeakerBenchMelody();
// 'speaker chord': a monophonic arpeggio (this engine has no simultaneous
// polyphony -- see this file's top-of-file comment) standing in for a
// simple major-chord test -- C4-E4-G4-C5 ascending then descending, twice,
// ~4.76s ("about 5 seconds"), at the current bench volume.
bool startSpeakerBenchChord();
// 'speaker noise': white noise, 2s, capped at min(current bench volume,
// 10%) regardless of how high the bench volume ladder is set -- for
// detecting hiss/buzz/mechanical rattling, not a loudness test.
bool startSpeakerBenchNoise();

// --- Automatic volume-ladder diagnostic -- 'speaker voltest'/'volquick'/
// 'volstop'/'volstatus'. Progressively raises the DIGITAL amplitude through
// a fixed ladder, playing a short multi-frequency diagnostic sequence at
// each level, for judging usable loudness, where distortion begins, and
// whether hiss/buzz/instability grows with level. Fully non-blocking --
// driven from updateSpeakerTest()/updateVolLadder() every loop() tick, no
// delay() anywhere -- and reuses this file's existing sine/note-sequence
// engines and startTest() scheduler completely unchanged (no second I2S
// write path, no pin/clock/format/gain changes). Every level change waits
// for the previous level's audio to reach genuine digital silence first
// (see armVolLadderStep()'s own comment in SpeakerTest.cpp) -- never an
// instant jump mid-tone. Aborts immediately (returns to continuous digital
// silence) on any I2S write error, zero, or partial-write outcome, or when
// any other test command interrupts it (including 'k'/'speaker stop'). ---

// 'speaker voltest': 11 levels, 2% -> 100%, ~4-5s each (three sine tones +
// gaps + a short excerpt of the existing diagnostic melody), ~49s total.
// Prints "HIGH OUTPUT TEST" before every level >=50%, plus dedicated
// warnings immediately before the 80% and 100% levels. Refuses (same as
// every other test) if the speaker never initialized.
bool startSpeakerVolTest();
// 'speaker volquick': 5 levels, 12/25/50/75/100%, ~1.9s each (two sine
// tones + gaps), ~9.5s total -- for quickly repeating the test later.
bool startSpeakerVolQuick();
// 'speaker volstop': immediately cancels an active voltest/volquick run and
// returns to continuous digital silence. 'speaker stop'/'stopmusic'/'k'
// also cancel an active run (see SpeakerTest.cpp's stopSpeakerMusic()/
// stopSpeakerTest()) -- this is a dedicated, explicitly-named entry point
// for the same underlying stop logic, not a second implementation.
bool stopSpeakerVolTest();
// 'speaker volstatus': prints active/inactive, test type (FULL/QUICK),
// current level/amplitude/sequence stage, the last level fully completed,
// and whether the most recent run ended via manual or safety-triggered
// abort rather than reaching COMPLETE.
void printSpeakerVolStatus();

// --- Procedural music player -- see SpeakerTest.cpp's Note/Song/
// songSample() for the generic playback engine. Unlike every test above,
// these LOOP CONTINUOUSLY (printing "[SPEAKER] Loop N" on each repeat)
// until interrupted by another command or stopped -- see stopSpeakerMusic()
// below. All are generated procedurally from note frequencies (sine
// waves only, no samples/files) -- no external library, no filesystem. ---

// 'music1': "Twinkle Twinkle Little Star" (full traditional melody).
bool startSpeakerMusic1();
// 'music2': "Mary Had a Little Lamb" (full traditional melody).
bool startSpeakerMusic2();
// 'music3': "Ode to Joy" (Beethoven's public-domain melody, first two phrases).
bool startSpeakerMusic3();
// 'music4': Super Mario Bros. overworld theme -- ONLY the opening flourish
// (~2s), not the full copyrighted song.
bool startSpeakerMusic4();

// 'stopmusic': immediately stops music playback and returns to continuous
// digital silence, printing "[SPEAKER] Stopped". Also usable (and safe) as
// a general "stop whatever's playing" command, not just for music --
// unlike the other test commands, this always prints, even if nothing was
// playing. Distinct from stopSpeakerTest() (used only by 'k'/emergency
// stop, which prints its own "Emergency stop" message instead).
bool stopSpeakerMusic();

// --- V1.1 buzz/noise isolation diagnostics -- 'speaker silencecheck'/
// 'carriercheck'. Neither has a fixed duration; both run until explicitly
// ended by 'speaker stop'/'stopmusic'/'k' (same stop paths as every other
// test in this file) -- "user-controlled period", not a timer. Never
// touches LEDs or the microphone; AudioAnalyzer.cpp's RX path is
// unaffected. ---

// 'speaker silencecheck': forces continuous digital zero (no synthesized
// tone) and prints that true digital silence is active. This is actually
// the SAME signal the speaker already transmits between tests (phase ==
// SILENCE) -- this command exists so a human can deliberately hold that
// state and listen for residual buzz/hiss with zero tone playing, rather
// than catching it only in the gap between two commands.
bool startSpeakerSilenceCheck();
// 'speaker carriercheck': same digital-zero signal as silencecheck, plus
// explicit reporting of the I2S clock/pin state (i2s_get_clk(), GPIO16
// routing) -- for distinguishing "I2S clocks active + zero samples" from
// "actual audio content" when judging whether a noise source is digital/
// clock-related versus something else.
bool startSpeakerCarrierCheck();

// 'speaker lowmidhigh': 150/440/1500Hz in sequence, equal duration and
// fades, at the current normal speaker volume -- reuses the same bounded
// note-sequence engine 'speaker melody'/'chord' already use (see
// SpeakerTest.cpp's boundedNoteSequenceSample()), not a new engine.
bool startSpeakerLowMidHigh();

// 'speaker speechtest': an ORIGINAL synthetic speech-like diagnostic (NOT
// copyrighted audio) -- alternating short "syllable" notes across the
// fundamental adult-speech frequency range with syllable/word-boundary
// gaps, at the current normal speaker volume, ~10s. Reuses the same
// bounded note-sequence engine as 'speaker melody'/'chord'/'lowmidhigh'.
bool startSpeakerSpeechTest();

// 'speaker musictest': an ORIGINAL short musical diagnostic (NOT a
// copyrighted melody) -- low/mid/high notes, rests, and both short
// transient-attack notes and longer sustained notes with per-note
// amplitude variation ("changing dynamics"), at the current normal speaker
// volume, ~10-15s. Its own small engine (dynamicNoteSequenceSample() in
// SpeakerTest.cpp) since this is the one diagnostic in this file that
// needs per-note amplitude, not just per-note frequency/duration.
bool startSpeakerMusicTest();

// --- V1.1 noise-isolation mode -- 'speaker isolate on'/'off'/'status'. See
// the SPEAKER_ISOLATE_* comment in Config.h for the full rationale and its
// "best-effort, not a hard interlock" caveat. Diagnostic-only; never
// activated automatically by any other command in this file. ---

// 'speaker isolate on': commands the motor stopped once (MotorDriver's own
// motorStop()) and mutes LEDs (Controls.h's setMuted(true)), saving the
// prior mute state to restore later. No-op (prints already-on) if already
// active.
void speakerIsolateOn();
// 'speaker isolate off': restores the LED mute state saved by
// speakerIsolateOn(). Does not re-engage the motor (nothing in this file
// ever drives the motor forward/reverse). No-op (prints already-off) if
// not active.
void speakerIsolateOff();
// 'speaker isolate status': prints whether isolate mode is currently active.
void printSpeakerIsolateStatus();
bool isSpeakerIsolateActive();

bool isSpeakerReady();

// For the '?' status command.
void printSpeakerTestStatus();

// NOTE: an isolated-task portMAX_DELAY write diagnostic ('w') was tried
// (on an earlier, since-replaced slave-TX architecture) and permanently
// removed -- it froze the entire application, not just the diagnostic
// task, requiring a hardware reset. Do not reintroduce a portMAX_DELAY
// (or any unbounded) i2s_write() call on this port, from any task.
