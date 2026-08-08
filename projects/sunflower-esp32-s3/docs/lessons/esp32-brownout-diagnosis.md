---
name: esp32-brownout-diagnosis
description: The ESP32 ROM bootloader always prints reset reason — use it as free brownout observability, and never disable the hardware detector
metadata:
  type: lesson
---

# ESP32 brownout diagnosis

## Problem

Determine whether an unexpected reset was caused by a brownout
(insufficient supply voltage under load) without adding new
instrumentation.

## Root cause / discovery

The ESP32 ROM bootloader unconditionally prints its reset reason on
every boot, before any application code runs — e.g. `rst:0xf
(BROWNOUT_RST)`. This project has this visible in its own captured
boot logs already, at no code cost. UVLO/brownout was considered as a
possible explanation for an early, unreproduced motor-startup failure
in this project, but was explicitly **not** treated as a confirmed root
cause — supply voltage was never captured during the actual transient
failure, so it remained a plausible hypothesis, not proof (the real
root cause, later found, was mechanical belt preload).

## How it was verified

Reset-reason printing is a property of the ROM bootloader, verifiable
by simply reading any boot log. The brownout-as-hypothesis vs.
mechanical-root-cause distinction was verified by later confirming the
belt fix resolved the failure, which a power-only cause would not
explain.

## Correct approach

Check the ROM boot banner's reset reason first — it's free and always
present. Never disable the ESP32's hardware brownout detector to make
a symptom "go away." Do not promote "brownout" from hypothesis to
documented root cause without direct supply-voltage measurement during
the actual failure.

## Common failure modes

- Disabling brownout detection to suppress resets instead of fixing
  the underlying supply problem — trades a visible, safe failure mode
  for silent, unsafe operation under low voltage.
- Documenting an unmeasured brownout as "the" cause of an intermittent
  failure because it's a convenient, plausible-sounding explanation.

## Applies to future projects?

Yes — general ESP32/ESP-IDF guidance, not Sunny-specific.

## Related Sunny files

`docs/current/POWER.md`, `docs/current/SPEAKER.md` ("Known brownout history"),
`docs/playbooks/POWER_BROWNOUT_DEBUGGING.md`,
`archive/motor_bringup/DRV8833_MOTOR_BRINGUP.md` section 8.
