# Architecture — ESP32 Platform

How the ESP32-S3 platform choice shapes the rest of the system. For
the exact current module/GPIO values, see `docs/current/HARDWARE_ARCHITECTURE.md`/
`GPIO_MAP.md`. This file is about the durable *why*.

## Why ESP32-S3

Single chip with WiFi/BLE (available for future use, not currently
exercised), enough GPIO for five real-time subsystems, hardware I2S
(needed for both the microphone and the amplifier), hardware PWM/LEDC
(needed for motor speed control), and enough flash/PSRAM headroom (the
N16R8 variant: 16MB flash, 8MB octal PSRAM) for future growth (audio
assets, a Raspberry Pi comms stack) without a platform change.

## Why the ESP32 owns real-time hardware, not a higher-level processor

Every safety-critical guarantee in this system — emergency stop,
the motor max-energized backstop, `MotorPowerGuard`'s LED/motor
coexistence timing — lives entirely on the ESP32, checked
unconditionally in the central serial dispatcher and the main loop.
This is a deliberate, permanent architectural boundary, not a
temporary simplification: a future Raspberry Pi companion is planned
to own higher-level compute (vision, LLM/voice), but real-time safety
must **never** depend on that companion being present, connected, or
responsive. See `DESIGN_DECISIONS.md`'s "Why ESP32 owns real-time
hardware?" and `PI_INTERFACE.md`.

## Single-owner GPIO/peripheral discipline

Every hardware resource (a GPIO pin, the I2S controller, the `Serial`
port, the NeoPixel strip object) has exactly one owning module. This
is enforced by convention (code review/inspection), not by a
compiler-checked mechanism — the ESP32 Arduino framework doesn't
provide one. See `docs/standards/GPIO_STANDARD.md` for the concrete
rule this produces, and `docs/architecture/SOFTWARE_ARCHITECTURE.md`
for the current resource-ownership table.

## Boot sequencing philosophy

Motor pins are forced to a known-safe state as the literal first
action in `setup()`, before `Serial`/LED/mic init — hardware safety
before anything observable. I2S (the shared full-duplex bus) is
brought up and primed with digital silence before either the
microphone or speaker application logic touches it. This "safe state
first, observability second, application logic third" ordering is a
deliberate pattern, reusable for any future peripheral added to this
boot sequence.

## See also

`docs/current/HARDWARE_ARCHITECTURE.md` (exact current values),
`docs/playbooks/ESP32_BRINGUP.md`,
`docs/lessons/esp32-s3-gpio-selection.md`.
