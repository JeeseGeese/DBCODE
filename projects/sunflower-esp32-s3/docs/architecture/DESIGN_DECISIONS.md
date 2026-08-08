# Architecture — Design Decisions

Important engineering decisions, why they were made, what else was
considered, and the tradeoffs accepted. Add a new entry here whenever
a future session makes a comparably significant architectural choice
— this file is meant to keep growing.

---

## Why Shared I2S (one full-duplex controller, not two)?

**Decision:** Microphone RX and speaker TX share one I2S controller
(`I2S_NUM_0`) configured full-duplex master, rather than two separate
controllers.

**Reason:** A two-controller design (RX master + TX slave sharing
clocks) was tried first and failed conclusively — the slave TX port's
`i2s_write()` always returned 0 bytes written, and an unbounded wait
froze the entire application. See `docs/lessons/shared-full-duplex-i2s.md`.

**Alternatives considered:** Two independent controllers with separate
clocks (would need separate BCLK/WS pins — more GPIOs, and still
doesn't guarantee independence at the DMA level); a software-mixed
single RX-only controller with speaker output disabled (rejected —
defeats the goal of having a speaker at all).

**Tradeoffs accepted:** RX and TX share one clock domain, meaning they
must run at the same sample rate (currently 16kHz, chosen for the
microphone's use case) — a future need for higher-quality audio
playback at a different rate would require revisiting this, not a
free change. Also: both directions share a 32-bit slot container width,
constraining (not preventing) independent bit-depth choices — see
`docs/architecture/AUDIO_PIPELINE.md`.

---

## Why BaseEffects + AudioOverlays as two separate concepts?

**Decision:** LED rendering splits into an always-on "base effect"
layer (exclusive selection) and an independent "audio overlay" layer
(separate selection + separate enabled flag), composited together.

**Reason:** A single mutually-exclusive mode system can't represent
"my favorite background animation, with or without audio reactivity"
as independent user choices, and conflates selection state with
enabled state (losing "which overlay was selected" when disabled).

**Alternatives considered:** One flat list of "modes" (some audio-
reactive, some not) cycled together (the original, since-abandoned
design); a full generic layering/compositing engine supporting
arbitrary numbers of stacked layers (rejected as over-engineered for
two layers with a fixed relationship).

**Tradeoffs accepted:** Only two layers, not N — adding a third
independent layer (e.g. a future "motion accent" tied to physical
movement) was explicitly deferred rather than generalizing the
compositor further, since no third layer was justified yet. See
`docs/current/EXPRESSIVE_MOTION.md`'s "LED coordination — deliberately
deferred" section.

---

## Why does the ESP32 own real-time hardware (not a higher-level processor)?

**Decision:** All real-time safety guarantees (emergency stop, motor
max-energized backstop, `MotorPowerGuard`) live entirely on the ESP32
and must never depend on any other processor.

**Reason:** A future Raspberry Pi companion can crash, disconnect,
reboot, or simply be slow to respond — none of that may be allowed to
leave a motor energized or make emergency stop unavailable. Real-time
safety on a small, single-purpose, always-present MCU is a much
stronger guarantee than real-time safety mediated through a
general-purpose Linux companion computer and a comms link.

**Alternatives considered:** A unified architecture where the Pi is the
primary controller and the ESP32 is a "dumb" peripheral (rejected —
inverts the safety guarantee); safety logic duplicated on both
processors (rejected — two implementations of the same safety logic
drift and disagree, which is worse than one authoritative one).

**Tradeoffs accepted:** The ESP32-side command surface for future
Pi integration must stay deliberately simple (semantic state requests,
not raw actuation) — see `docs/architecture/PI_INTERFACE.md`.

---

## Why does the Raspberry Pi own AI and camera (not the ESP32)?

**Decision:** Vision processing and LLM/voice compute are planned to
live entirely on a future Raspberry Pi companion, never on the ESP32.

**Reason:** The ESP32-S3 has real but limited compute/memory
compared to what vision/LLM workloads want, and — more importantly —
loading that work onto the same chip responsible for real-time safety
risks exactly the kind of blocking/latency problem the whole
non-blocking architecture exists to prevent (see the next entry).
Keeping the boundary clean (ESP32 = real-time control, Pi = higher-
level compute) is simpler to reason about than a hybrid.

**Alternatives considered:** A more powerful single-MCU platform
running everything (rejected — real-time guarantees become much
harder to reason about alongside heavyweight vision/LLM workloads on
one chip); offloading only some AI work to the ESP32 (e.g. a small
on-device wake-word model) — not rejected outright, but not adopted;
would need its own justification if proposed later.

**Tradeoffs accepted:** Two processors means a real integration/
transport problem to solve (see `docs/architecture/PI_INTERFACE.md`,
currently undecided) and two power domains to manage.

---

## Why non-blocking architecture (no `delay()`, `millis()`-based state machines)?

**Decision:** No behavior/controller module may call `delay()` or
otherwise block; every multi-step behavior is a `millis()`-timestamped
state machine advanced once per `loop()` call.

**Reason:** A single-threaded `loop()` serves LED rendering, audio
capture, motor behaviors, button polling, and serial dispatch (in
particular, emergency stop) all in the same thread. Any blocking call
in one starves every other subsystem for its duration — including
safety-critical ones.

**Alternatives considered:** A real RTOS task-per-subsystem model
(ESP32 does support FreeRTOS multitasking) — not adopted for the
application layer; the current single-loop model has worked without
needing it, and introducing real concurrency brings its own new
failure modes (races, needing locks) that this project has
deliberately avoided by staying single-threaded at the application
level.

**Tradeoffs accepted:** Every behavior author must think in explicit
states and timestamps rather than linear blocking code — more
verbose, but the failure mode (a stuck state) is far easier to reason
about and defend against (a generic max-duration backstop) than a
blocked thread.

---

## Why power limiting (a software current estimate)?

**Decision:** `main.cpp` estimates per-frame LED current draw and
scales the frame down if it exceeds a configured cap, as a
bring-up-time safety aid.

**Reason:** No current-sensing hardware exists in this project. A
software estimate, applied centrally after full frame compositing, is
a cheap defense against commanding an LED pattern that would draw more
current than the supply can provide — while development is still
figuring out the right supply sizing.

**Alternatives considered:** No software limiting, relying entirely on
correct electrical sizing (rejected during bring-up — no independent
verification existed yet that the sizing was correct); real current-
sense hardware (not ruled out for the future, just not present today).

**Tradeoffs accepted:** The estimate uses standard per-channel current
assumptions, not measurements of the actual installed LEDs — it's a
safety aid, explicitly documented as not a substitute for correct
electrical design. See `docs/lessons/led-power-limiting.md`.

---

## Why this GPIO layout?

**Decision:** See `docs/current/GPIO_MAP.md` for the exact table.

**Reason:** Each assignment was made incrementally as each peripheral
was brought up, respecting the ESP32-S3 N16R8's octal-PSRAM pin
reservations, boot-strapping pins, and (once I2S sharing was adopted)
the constraint that BCLK/WS be shared between the microphone and
amplifier.

**Alternatives considered:** Separate BCLK/WS pins per I2S device
(would need two full I2S controllers — rejected, see "Why Shared
I2S?" above).

**Tradeoffs accepted:** GPIO8/9 (motor) briefly become LEDC-PWM-
attached instead of plain digital outputs while `MotorPwmCalibration`
owns them — documented, not a conflict, since `MotorDriver` remains
the sole owner either way.

---

## See also

`docs/standards/`, `docs/lessons/`, `docs/current/SOFTWARE_ARCHITECTURE.md`.
