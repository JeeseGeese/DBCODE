#pragma once
// BEGIN MIC RETEST
// Dedicated, from-scratch INMP441 diagnostic -- see src/MicRetest.cpp for
// what it does and why. Runs once at the end of setup(), after the existing
// HardwareTest LED/mic sequence, before the normal loop() begins. Reads raw
// I2S samples directly off the already-initialized I2S peripheral (does not
// reinitialize I2S, does not touch AudioAnalyzer's production pipeline).
//
// To disable without deleting anything: flip this to 0 and rebuild --
// runMicRetest() becomes a no-op, no other file needs to change.
#define ENABLE_MIC_RETEST 1

// To remove entirely: delete this file, src/MicRetest.cpp, and the one
// include + one call in src/main.cpp (both marked BEGIN/END MIC RETEST).
void runMicRetest();
// END MIC RETEST
