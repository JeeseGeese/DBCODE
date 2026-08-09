# Claude Context Guide — Minimal Read Sets Per Task

Read `CURRENT_STATUS.md` first, always — it's short and tells you
whether anything below has changed. Then use the task-specific set
below. Read additional files only when the requested change actually
depends on them — these are starting points, not ceilings.

## LED effect work

**Read:** `CURRENT_STATUS.md`, `docs/development/LED_AUDIO_QUICK_REFERENCE.md`,
`docs/development/ADDING_LED_EFFECTS.md`, `include/LedEffects.h`,
`src/LedEffects.cpp`, `include/Config.h` (LED section only). If the
change affects per-frame current draw (a new effect, brightness
handling, `NUM_LEDS`), also check `docs/lessons/led-power-limiting.md`
and `docs/current/POWER.md`'s "LED-side software power limiting"
section — otherwise skip both.

**Do NOT read:** speaker logs, motor history, Raspberry Pi/camera
docs, `archive/`, `HardwareTest.cpp`/`MicRetest.cpp`, the full V1.1
power/brownout investigation narrative in `docs/current/POWER.md`
(the one section above is the exception, not the whole file).

## Audio overlay work

**Read:** `CURRENT_STATUS.md`, `docs/development/LED_AUDIO_QUICK_REFERENCE.md`,
`docs/development/ADDING_AUDIO_OVERLAYS.md`, `include/AudioOverlays.h`,
`src/AudioOverlays.cpp`, `include/AudioVisualState.h`,
`include/AudioAnalyzer.h`, `include/Config.h` (audio/overlay section).

**Do NOT read:** speaker logs, motor history, Raspberry Pi/camera docs,
`archive/`.

## Speaker debugging

**Read:** `CURRENT_STATUS.md`, `docs/current/SPEAKER.md`,
`docs/current/I2S_ARCHITECTURE.md`, `include/SharedI2S.h`/`.cpp`,
`include/SpeakerTest.h`/`.cpp`, `include/Config.h` (speaker section),
`test_host/speaker_*.cpp` (whichever are relevant to the specific
change). If the task is specifically about power/brownout behavior
during speaker use, also read `docs/current/POWER.md`'s "Current
brownout investigation" section — otherwise skip it.

**Do NOT read:** LED/overlay files, motor history, Raspberry Pi/camera
docs, unless the task explicitly involves them (e.g. combined-load
power testing). `archive/speaker_bringup/` only if investigating
whether something specific was already tried. The full V1.1 power/
brownout investigation (`docs/current/POWER.md`'s brownout sections) —
only pull in what's cross-referenced from `SPEAKER.md` if the task
actually touches power, not for general speaker work.

## Motor work

**Read:** `CURRENT_STATUS.md`, `docs/current/MOTOR.md`,
`docs/current/EXPRESSIVE_MOTION.md` (if touching idle/personality motion),
the specific active motor source/header file(s) for the behavior being
changed (`MotorDriver`, `MotorPowerGuard`, `MusicMotorController`,
`ExpressiveMotion`, `BehaviorEngine` — not all of them at once unless
the change genuinely spans layers), relevant `Config.h` sections,
relevant `test_host/music_motor_*.cpp` files, `docs/lessons/drv8833-motor-control.md`
and `docs/playbooks/DRV8833_MOTOR_BRINGUP.md` if debugging a new
physical symptom.

**Do NOT read:** `archive/motor_bringup/` unless investigating an old
failure that might be recurring — the belt-preload root cause and the
`k`-miss race are both closed; don't re-derive them from scratch. Do
NOT read the V1.1 speaker buzz investigation (`docs/current/SPEAKER.md`)
or its full history — `MusicMotorController` decision logic is
unrelated to it. If the task is specifically about brownouts/resets
during motor use (not motor decision logic itself), read
`docs/current/POWER.md`'s "Current brownout investigation" section and
`docs/lessons/motor-current-noise-mitigation.md` instead of the full
power investigation.

## Power/brownout debugging

**Read:** `CURRENT_STATUS.md`, `docs/current/POWER.md` (the "Current
brownout investigation" section is the load-bearing one — sections A
through H, plus the investigation list), `docs/current/HARDWARE_ARCHITECTURE.md`,
`docs/current/ELECTRICAL.md`, `docs/playbooks/POWER_BROWNOUT_DEBUGGING.md`,
and whichever of these lessons are relevant to the specific symptom:
`docs/lessons/esp32-brownout-diagnosis.md`,
`docs/lessons/motor-current-noise-mitigation.md`,
`docs/lessons/common-ground-design.md`,
`docs/lessons/breadboard-prototype-transient-load-risk.md`,
`docs/lessons/power-diagnostic-evidence-vs-proof.md`,
`docs/lessons/led-power-limiting.md`,
`docs/lessons/verify-physical-led-count.md`.

**Do NOT read:** the full speaker buzz-diagnosis narrative or
`MusicMotorController`'s decision-logic internals unless the specific
symptom involves them; Raspberry Pi/camera docs; `archive/` unless an
old, closed power finding might be recurring (e.g. re-verify it's
actually recurring before re-deriving history — see
`docs/lessons/esp32-brownout-diagnosis.md`'s UVLO caution).

**Before concluding a root cause:** re-read the OBSERVED/HYPOTHESIS/
CHANGE MADE/RESULT/CURRENT CONCLUSION/STILL UNPROVEN structure in
`docs/current/POWER.md`'s brownout investigation — several plausible-
looking causes (58-LED count, the 1000µF capacitor) were already
investigated and explicitly ruled out or confounded; don't re-propose
them without reading why first.

## Touchscreen/UI work (V1.2 — current next milestone)

**Read:** `CURRENT_STATUS.md`, `ROADMAP.md`'s "Sunny V1.2" section (goals
and scope), `docs/current/V1_1_STATUS.md` (confirms V1.1 is closed/what
was accepted as a limitation going in), `docs/current/GPIO_MAP.md`
(which pins are already owned — the UI's display/touch controller needs
GPIOs not already in that table), `docs/current/SOFTWARE_ARCHITECTURE.md`
(the existing single-owner-resource pattern the UI layer must follow —
read-only status queries + explicit command paths into existing
subsystems, never a second owner of any peripheral), and whatever
touchscreen/display hardware documentation exists once the hardware is
chosen. For status/diagnostics screens, read only the specific status-
query functions being surfaced (e.g. `printSpeakerTestStatus()`,
`printSpeakerVolStatus()`) — not the full implementation behind them.

**Do NOT read:** the V1.1 power/brownout debugging history
(`docs/current/POWER.md`'s investigation sections — a link from
`V1_1_STATUS.md` is enough), the speaker buzz-diagnosis narrative
(`docs/current/SPEAKER.md`'s investigation sections), old speaker
bring-up experiments, `archive/motor_bringup/`, `docs/V1/`, or
Raspberry Pi/camera planning — none of that is relevant to bringing up
a display and building a UI layer on top of already-working subsystems.
If a specific UI screen needs to show real subsystem state (e.g. "is
the speaker buzzing"), read only that subsystem's current status
section, not its full investigation history.

## Raspberry Pi integration (future)

**Read:** `CURRENT_STATUS.md`, `docs/current/SOFTWARE_ARCHITECTURE.md`,
`docs/current/I2S_ARCHITECTURE.md` (only if audio handoff is involved),
`ROADMAP.md`'s V1.2 section, and the still-relevant forward-
planning material referenced from there
(`archive/superseded_docs/EXPRESSIVE_MOTION_DEVELOPMENT.md` sections
15-19, `archive/superseded_docs/BEHAVIOR_ENGINE_DEVELOPMENT.md`
section 12) — these are archived but still describe the intended
integration surface, not stale in that respect. Then only the actual
integration files as they're created. Do NOT load the V1.1 power/
brownout debugging history (`docs/current/POWER.md`'s investigation
sections) unless the integration work becomes power-architecture-
relevant (e.g. the Pi adds its own current draw to a shared rail) — a
brief read of `docs/current/POWER.md`'s "Current rail layout" and "The
real electrical risk" sections is enough to start.

## Camera work (future — after Raspberry Pi, not before)

The camera is planned to connect to and be managed by the Raspberry
Pi, not the ESP32 directly. **Read:** `CURRENT_STATUS.md`,
`docs/current/GPIO_MAP.md`, `docs/current/POWER.md`'s "Current rail
layout" section only, whatever Raspberry Pi integration exists by the
time this work starts, camera-specific docs/code once they exist. Do
not plan the camera as a direct ESP32 peripheral unless that's
explicitly redesigned later. Do NOT load the full V1.1 power/brownout
debugging history unless power architecture becomes directly relevant.

## General firmware work (doesn't fit a category above)

**Read:** `CURRENT_STATUS.md`, `docs/current/SOFTWARE_ARCHITECTURE.md`,
`include/Config.h`, `src/main.cpp`, then only the specific subsystem
files directly relevant to the change.

**If the change involves a genuinely new architectural choice** (a new
shared resource, a new cross-module pattern, a decision comparable to
what's already recorded) — also read
`docs/architecture/DESIGN_DECISIONS.md` first, to avoid re-deciding
something already settled, and add a new entry there once the decision
is made. **If the change touches GPIO/power/naming/documentation/
testing conventions** — check the matching file in `docs/standards/`
first.

---

## Estimated context reduction

Rough, not precise. The full project is ~40 source files (`src/` +
`include/`), ~3,000 lines of README/docs narrative at the top level
alone, plus 18 host test files and ~6 archived historical documents
(several hundred to ~1,400 lines each in the largest cases).

| Task | Full-project read (rough) | Guided read (this file) | Rough reduction |
|---|---|---|---|
| LED effect work | ~40 files, thousands of lines (incl. motor/speaker history) | 5-6 files, a few hundred lines | ~85-90% fewer files |
| Audio overlay work | same as above | 6-7 files | ~85% fewer files |
| Speaker debugging | same as above | 5-7 files (+ relevant tests) | ~80% fewer files |
| Motor work | same as above | 4-6 files (+ relevant tests) | ~80% fewer files |

These are file-count estimates, not token counts — actual savings
depend on file size (the largest single reduction is avoiding
`archive/motor_bringup/DRV8833_MOTOR_BRINGUP.md`'s ~800 lines and
README.md's ~1,400-line embedded motor narrative for any task that
doesn't need them).
