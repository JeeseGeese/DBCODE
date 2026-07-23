# sunflower-esp32-s3

ESP32-S3 firmware project driving a 58-pixel WS2812B-compatible addressable
LED strip.

## Hardware

- **MCU:** ESP32-S3-WROOM module, N16R8 variant (16 MB flash, 8 MB octal PSRAM)
- **Board:** generic ESP32-S3 devkit, USB-C, UART0 bridged to USB via an
  onboard WCH CH343 chip (not the ESP32-S3's native USB-OTG peripheral)
- **LEDs:** 58x WS2812B-compatible addressable LEDs, single data line
- **Power:** ESP32 5V pin and LED strip 5V share a common ground with the
  ESP32 GND pin

## GPIO assignments

| Signal        | GPIO | Notes                                   |
|---------------|------|------------------------------------------|
| LED data (DIN)| 4    | 3.3V logic-level data signal to LED strip|

No other GPIOs are currently in use by this firmware.

## Build instructions

```
cd ~/DOBETTERCODE/MICROCONTROLLERS/projects/sunflower-esp32-s3
export PATH="$HOME/.platformio/penv/bin:$PATH"   # if pio isn't already on PATH
pio run
```

## Upload instructions

Connect the board via USB-C, then:

```
pio device list                          # confirm the port, typically /dev/ttyACM0
pio run -t upload --upload-port /dev/ttyACM0
```

If your user account isn't in the `dialout` group, prefix the upload command
with `sg dialout -c "..."` or add yourself to the group and re-login:

```
sudo usermod -aG dialout $USER
```

## Serial monitor instructions

```
pio device monitor -p /dev/ttyACM0 -b 115200
```

(Ctrl+C to exit.)

## Current LED test behavior

On boot, the firmware runs through the following stages once, printing each
stage name to Serial as `[STAGE] ...`, then loops the final stage forever:

1. Clear all 58 LEDs
2. LED 0 solid red for 1 second
3. LED 57 solid green for 1 second
4. Dim white pixel chase from LED 0 through LED 57
5. Red fill from LED 0 through LED 57
6. Green fill backward from LED 57 through LED 0
7. Dim solid blue for 2 seconds
8. Slow rainbow animation — runs forever

Global brightness is capped at 15/255 in firmware (`BRIGHTNESS` constant in
`src/main.cpp`), so no stage — including the "white" stages — ever drives the
LEDs at full brightness.

## Safety warnings

- **Do not power the 58-LED strip from the ESP32's 3V3 pin.** At even modest
  brightness, 58 WS2812B LEDs can draw more current than the ESP32's onboard
  3.3V regulator is rated for. GPIO4 provides a **3.3V data signal only** —
  it does not and must not supply LED power.
- **Use a suitable external power supply for the LED strip**, sized for the
  strip's actual current draw at the brightness/color patterns you intend to
  run, with a **common ground** between that supply, the LED strip, and the
  ESP32 GND pin. Do not float the grounds.
