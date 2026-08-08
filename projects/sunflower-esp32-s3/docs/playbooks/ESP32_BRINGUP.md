# Playbook: ESP32 Board Bring-Up

General SOP for bringing up a new ESP32/ESP32-S3 board in a PlatformIO
project. Sunny-specific values are marked "(example: Sunny)".

## Steps

1. **Identify the exact module variant** (e.g. ESP32-S3-WROOM-1 N16R8)
   from the physical board silkscreen/seller listing — not "ESP32-S3"
   generically. Flash size, PSRAM type (quad vs. octal), and reserved
   pins all depend on the exact variant.
2. **Identify the USB bridge**: native USB-OTG, or an external
   UART-USB bridge chip (example: Sunny uses a WCH CH343). This
   determines `ARDUINO_USB_MODE`/`USB_CDC_ON_BOOT` build flags — get
   this wrong and `Serial` output goes nowhere.
3. **Set `platformio.ini`** for the exact module: `board`,
   `board_build.flash_mode`, `board_build.psram_type`,
   `board_upload.flash_size`, `board_build.partitions`,
   `board_build.arduino.memory_type` as applicable.
4. **Flash a minimal blink+Serial.println sketch first** — confirm
   upload, boot, and serial output all work before wiring anything
   else.
5. **Confirm the reset-reason boot banner** is visible (ROM bootloader
   prints it unconditionally) — this becomes free brownout/crash
   observability for the rest of the project (see
   `docs/lessons/esp32-brownout-diagnosis.md`).
6. **Only then** begin wiring peripherals, one at a time, running
   `docs/playbooks/GPIO_VALIDATION.md` before each new pin assignment.

## Common pitfalls

- Assuming a generic pinout for a variant with non-standard PSRAM
  (see `docs/lessons/n16r8-reserved-flash-psram-pins.md`).
- Wrong USB mode build flag → silent serial output (looks like a dead
  board).
- Skipping the minimal-sketch step and debugging a fully-wired board
  when the problem was actually basic bring-up.

## See also

`docs/lessons/esp32-s3-gpio-selection.md`,
`docs/lessons/n16r8-reserved-flash-psram-pins.md`.
