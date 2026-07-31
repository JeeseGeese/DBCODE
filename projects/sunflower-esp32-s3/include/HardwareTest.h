#pragma once
// BEGIN HARDWARE TEST
// Temporary bring-up verification for a newly-flashed board. Runs once at
// the end of setup(), before the normal loop() takes over: an LED
// color/rainbow sequence, then a timed window of continuous mic
// Peak/RMS/NoiseFloor printing. Not part of normal Sunflower operation.
//
// To remove: delete this file, src/HardwareTest.cpp, the two
// "BEGIN/END HARDWARE TEST" blocks in src/main.cpp, and the
// AudioFeatures::peak field (+ its one assignment) in
// include/AudioAnalyzer.h / src/AudioAnalyzer.cpp.
#include <Adafruit_NeoPixel.h>

void runHardwareTestSequence(Adafruit_NeoPixel &strip);
// END HARDWARE TEST
