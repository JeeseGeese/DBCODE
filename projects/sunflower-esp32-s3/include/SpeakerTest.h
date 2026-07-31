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

bool isSpeakerReady();

// For the '?' status command.
void printSpeakerTestStatus();

// NOTE: an isolated-task portMAX_DELAY write diagnostic ('w') was tried
// (on an earlier, since-replaced slave-TX architecture) and permanently
// removed -- it froze the entire application, not just the diagnostic
// task, requiring a hardware reset. Do not reintroduce a portMAX_DELAY
// (or any unbounded) i2s_write() call on this port, from any task.
