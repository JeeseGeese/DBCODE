# DBCODE

Part of the **DOBETTERCODE** development workspace. `DOBETTERCODE` is the parent
workspace directory for ongoing development work; `DBCODE` is the Git
repository within it for general software, firmware, and embedded
development — ESP32, ESP8266, Raspberry Pi Pico, Arduino, Raspberry Pi,
Python utilities, desktop software, CLI tools, shared libraries,
experimental projects, and future robotics firmware.

## Repository layout

```
DBCODE/
├── README.md
├── .gitignore
├── docs/                     Cross-project documentation and test plans
├── shared/                   Code/utilities shared across multiple projects
└── projects/
    └── sunflower-esp32-s3/   First active project (see below)
```

Each subdirectory under `projects/` is an independent PlatformIO project with
its own `platformio.ini`, `src/`, `include/`, `lib/`, and `test/` folders.

## Active projects

### sunflower-esp32-s3

First active project in this repository. See
[`projects/sunflower-esp32-s3/README.md`](projects/sunflower-esp32-s3/README.md)
for full details.

**Hardware:**
- ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM)
- 58x WS2812-compatible addressable LEDs
- LED data signal on GPIO4

**Status:**
- Both ESP32-S3 boards tested
- Firmware upload verified
- Serial output verified
- 58-LED color control verified
- LED subsystem testing in progress (see [`docs/LED_TEST_PLAN.md`](docs/LED_TEST_PLAN.md))
