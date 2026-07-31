#pragma once
#include <stdint.h>

// Single authoritative I2S initialization for the shared full-duplex bus
// (I2S_NUM_0) serving both the INMP441 microphone (RX, see
// AudioAnalyzer.cpp) and the MAX98357A amplifier (TX, see
// SpeakerTest.cpp) simultaneously, on one clock domain. This is the ONLY
// place in the firmware that calls i2s_driver_install()/i2s_set_pin()/
// i2s_driver_uninstall() -- AudioAnalyzer.cpp only calls i2s_read() on this
// port and SpeakerTest.cpp only calls i2s_write()/i2s_zero_dma_buffer() on
// it; neither reconfigures, reinstalls, or uninstalls the driver.
//
// Replaces an earlier two-controller design (I2S_NUM_0 master RX +
// I2S_NUM_1 slave TX sharing BCLK/WS) that failed conclusively on real
// hardware: i2s_write() on the slave TX port always returned ESP_OK with
// bytesWritten=0, at every bounded wait tried (20ms, 100ms), and an
// unbounded portMAX_DELAY wait froze the entire application (not just the
// write) until a hardware reset. A single full-duplex master port has
// exactly one clock domain generated once for both directions, so there is
// no cross-controller DMA-availability question to begin with. See
// initSharedI2S() below for the exact configuration, verified against this
// project's installed driver/i2s.h before writing.

bool initSharedI2S();
bool isSharedI2SReady();

// For the '?' status command / boot diagnostics -- includes a live re-read
// of GPIO16's GPIO-matrix output-routing register, so it can be called
// again later (e.g. after a tone trigger) to confirm nothing downstream
// ever reconfigures it.
void printSharedI2SStatus();
