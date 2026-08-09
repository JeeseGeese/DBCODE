#include "SharedI2S.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_struct.h>

#include "Config.h"

// ----------------------------------------------------------------------------
// Verified against this project's installed framework headers
// (framework-arduinoespressif32's tools/sdk/esp32s3/include/driver/include/
// driver/i2s.h and hal/include/hal/i2s_types.h) before writing this file --
// not guessed:
//
// - i2s_mode_t's MASTER/RX/TX bits (hal/i2s_types.h) are independently
//   OR-able in a single i2s_driver_install() call -- I2S_MODE_MASTER |
//   I2S_MODE_RX | I2S_MODE_TX is a standard, documented full-duplex
//   configuration on a single ESP32 I2S controller (also demonstrated by
//   this exact framework's own bundled example,
//   libraries/I2S/examples/FullDuplex/FullDuplex.ino, via its higher-level
//   I2S.setDuplex() wrapper -- confirming the underlying legacy driver
//   supports this mode combination on this framework version).
// - i2s_pin_config_t (driver/i2s.h) has fields mck_io_num, bck_io_num,
//   ws_io_num, data_out_num, data_in_num -- one struct configures BOTH
//   directions' pins at once for a full-duplex port; there is no separate
//   RX-pin-config vs TX-pin-config call.
// - i2s_config_t.channel_format/bits_per_sample apply to BOTH RX and TX on
//   a full-duplex port (there is only one field, not one per direction) --
//   this is exactly why the microphone can no longer use its old
//   RX-only I2S_CHANNEL_FMT_ONLY_LEFT: the TX side would inherit that same
//   mono format and only be able to drive one slot. I2S_CHANNEL_FMT_
//   RIGHT_LEFT (true stereo, both slots present) is required so the
//   speaker can fill both slots while the microphone simply ignores
//   whichever slot it doesn't occupy in software (see AudioAnalyzer.cpp).
//
// One authoritative I2S initialization path: this is the ONLY file that
// calls i2s_driver_install()/i2s_set_pin()/i2s_driver_uninstall(). See this
// header's own top-of-file comment.
// ----------------------------------------------------------------------------

namespace {
bool sharedI2SReady = false;
}  // namespace

bool initSharedI2S() {
  Serial.println(F("[I2S] Initializing shared full-duplex bus (I2S_NUM_0)"));
  Serial.printf("[I2S] mode=MASTER|RX|TX BCLK=%d WS=%d DIN=%d(mic in) DOUT=%d(speaker out) sampleRate=%dHz\n",
                I2S_BCLK_PIN, I2S_WS_PIN, I2S_DIN_PIN, I2S_SPEAKER_DOUT_PIN, I2S_SAMPLE_RATE);
  Serial.println(F("[I2S] bitsPerSlot=32 channelFormat=RIGHT_LEFT(stereo) commFormat=STAND_I2S"));

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
      .sample_rate = I2S_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      // V1.1 buzz-diagnosis APLL investigation (see docs/current/SPEAKER.md):
      // verified directly against this project's installed framework HAL
      // source -- framework-arduinoespressif32/.../esp32s3/include/hal/
      // esp32s3/include/hal/i2s_ll.h's i2s_ll_tx_clk_set_src()/
      // i2s_ll_rx_clk_set_src() both hardcode tx_clk_sel/rx_clk_sel to 2
      // (D2CLK) and IGNORE their `src` parameter entirely, with the comment
      // "ESP32-S3 only support I2S_CLK_D2CLK". soc_caps.h for esp32s3 also
      // does not define SOC_I2S_SUPPORTS_APLL (present on the original
      // ESP32, absent here) -- APLL is a chip-level capability this SoC's
      // I2S peripheral does not have wired to it in this framework. The
      // legacy driver's i2s_config_t.use_apll field exists only because the
      // struct is shared across chips; toggling it on THIS chip/framework
      // has no effect on the actual clock source -- an APLL-vs-D2CLK A/B
      // diagnostic would be a no-op, not a real test, so none is
      // implemented. This closes the APLL hypothesis for the residual
      // speaker buzz with source-level evidence, not a physical test.
      .use_apll = false,
      // TX side auto-fills silence on underflow (DIN must never carry
      // garbage); this flag is TX-specific and does not affect RX.
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S] ERROR: driver install failed (err=%d)\n", (int)err);
    sharedI2SReady = false;
    return false;
  }

  i2s_pin_config_t pin_config = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = I2S_BCLK_PIN,
      .ws_io_num = I2S_WS_PIN,
      .data_out_num = I2S_SPEAKER_DOUT_PIN,
      .data_in_num = I2S_DIN_PIN,
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("[I2S] ERROR: set pin failed (err=%d)\n", (int)err);
    i2s_driver_uninstall(I2S_PORT);
    sharedI2SReady = false;
    return false;
  }

  // Prime TX with real digital silence immediately -- DIN must never float
  // or carry garbage, even before SpeakerTest's own first feed call.
  i2s_zero_dma_buffer(I2S_PORT);

  Serial.println(F("[I2S] Shared full-duplex initialization: SUCCESS"));
  sharedI2SReady = true;
  return true;
}

bool isSharedI2SReady() { return sharedI2SReady; }

void printSharedI2SStatus() {
  uint32_t sel = GPIO.func_out_sel_cfg[I2S_SPEAKER_DOUT_PIN].func_sel;
  Serial.printf(
      "[I2S] ready=%d port=I2S_NUM_0 mode=MASTER|RX|TX channelFormat=RIGHT_LEFT bitsPerSlot=32 rate=%dHz "
      "GPIO%d(DOUT) func_out_sel=%lu expected=I2S0O_SD_OUT_IDX(%d) match=%d\n",
      sharedI2SReady ? 1 : 0, I2S_SAMPLE_RATE, I2S_SPEAKER_DOUT_PIN, (unsigned long)sel, (int)I2S0O_SD_OUT_IDX,
      sel == I2S0O_SD_OUT_IDX ? 1 : 0);
}
