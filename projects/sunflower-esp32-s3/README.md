# sunflower-esp32-s3

ESP32-S3 firmware project driving a 58-pixel WS2812B-compatible addressable
LED strip, controlled by four physical pushbuttons.

## Hardware

- **MCU:** ESP32-S3-WROOM module, N16R8 variant (16 MB flash, 8 MB octal PSRAM)
- **Board:** generic ESP32-S3 devkit, USB-C, UART0 bridged to USB via an
  onboard WCH CH343 chip (not the ESP32-S3's native USB-OTG peripheral)
- **LEDs:** 58x WS2812B-compatible addressable LEDs, single data line
- **Buttons:** 4x momentary pushbuttons, each wired between its GPIO pin
  and GND, using the ESP32's internal pull-up (no external resistors)
- **Microphone:** 1x INMP441 I2S MEMS microphone
- **Power:** ESP32 5V pin and LED strip 5V share a common ground with the
  ESP32 GND pin; INMP441 VDD runs from the ESP32 3V3 pin

## GPIO assignments

| Signal | GPIO | Notes |
|---|---|---|
| LED data (DIN) | 4 | 3.3V logic-level data signal to LED strip |
| Mode button | 10 | `INPUT_PULLUP`, wired to GND — cycles LED mode |
| Mute button | 11 | `INPUT_PULLUP`, wired to GND — toggles LEDs off/on |
| Brightness button | 17 | `INPUT_PULLUP`, wired to GND — cycles brightness |
| Button 4 | 5 | `INPUT_PULLUP`, wired to GND — long press: toggle AUDIO_PULSE; short press: reserved for future audio-mode cycling |
| INMP441 SCK/BCLK | 6 | I2S bit clock, driven by the ESP32 (I2S master) |
| INMP441 WS/LRCLK | 7 | I2S word select, driven by the ESP32 (I2S master) |
| INMP441 SD/DATA | 15 | I2S data input to the ESP32 |

INMP441 `L/R` is tied to GND (selects the LEFT I2S channel) and `GND` is
tied to a common ground shared with the ESP32.

GPIO45 was tried as a candidate spare pin during wiring diagnostics and
failed to respond even to a direct short to GND (likely not broken out to a
usable header pin on this board, or a bad physical connection) — it is not
used by this firmware. GPIO8 was the originally planned mic data pin before
the board's actual wiring was traced to GPIO15 instead.

## Build instructions

```
cd ~/DOBETTERCODE/DBCODE/projects/sunflower-esp32-s3
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

## Current LED controller behavior

On boot the firmware initializes all four buttons and the LED strip, and
starts in **Mode 0 (Off)**. Each button is independently debounced (40ms,
edge-triggered on stable HIGH→LOW) so presses on different buttons never
interfere with each other, and each press is a single mode/setting change
with no repeat-while-held.

**Mode button (GPIO10)** — advances to the next mode on each press,
wrapping from the last mode back to the first. 13 modes:

`Off, Solid Red, Solid Green, Solid Blue, Solid White (dim), Forward
Walking Pixel, Reverse Walking Pixel, Rainbow, Theater Chase, Breathing,
Twinkle, Larson Scanner, AUDIO_PULSE`

Animated modes run on their own `millis()`-based timing in the main loop
(no `delay()`), so buttons stay responsive while an animation is running.
AUDIO_PULSE (the 13th mode, see its own section below) is reachable this
way too, in addition to the Button 4 shortcut.

**Mute button (GPIO11)** — toggles the LEDs off/on without losing the
currently selected mode; unmuting re-renders exactly where the mode was
left off. In every mode except AUDIO_PULSE this means blanking to black;
AUDIO_PULSE instead holds its idle glow while muted (see below).

**Brightness button (GPIO17)** — cycles global brightness through
`10 → 15 → 20 → 10 ...`. Never reaches full brightness at any level,
including in "white" modes. Fully functional while AUDIO_PULSE is active
and audio-reactive.

**Button 4 (GPIO5)** — dual behavior:
- **Long press** (≥600ms): jumps directly into AUDIO_PULSE mode, remembering
  the mode you were on; long-pressing again exits back to that remembered
  mode. Works as a toggle regardless of whether AUDIO_PULSE was originally
  reached via Button 4 or via the Mode button.
- **Short press**: reserved for cycling among audio-reactive modes once more
  than one exists. Currently a documented no-op (prints an acknowledgement
  line only while AUDIO_PULSE is active) since AUDIO_PULSE is the only one.

Microphone diagnostic output (`[MIC] ...`) is no longer gated by a button —
it's enabled by default at boot so nothing is hidden; see the dedicated
section below.

All brightness levels stay well below full brightness (255) — even the
brightest setting (20/255) keeps LED output conservative.

## AUDIO_PULSE mode

The first (and currently only) audio-reactive LED mode. Reads live
DC-corrected RMS from the existing INMP441 I2S capture path (no second I2S
driver, no FFT/frequency-band analysis) and turns it into a warm pulsing
animation across all 58 LEDs.

**Visual behavior:**
- **Silence:** dim warm idle glow, uniform across the whole strip.
- **Low audio:** a gentle brightening starting at the center LED.
- **Medium/loud audio:** the brighter region expands outward from the
  center as the (smoothed) level rises.
- **Clap / loud transient:** a brief near-full-brightness flash across the
  entire strip, decaying quickly and independently of the smoothed level.
- Never calls `strip.setBrightness()` — whatever global brightness level
  is currently selected (via the Brightness button) is left untouched.

**Normalization and smoothing** (`src/main.cpp`, near the top):

```
AUDIO_NOISE_FLOOR       20000.0   // top of the observed quiet-room RMS range
AUDIO_MAX_RMS           200000.0  // ceiling; loud speech/music reaches ~1.0 here
AUDIO_CLAP_THRESHOLD    400000.0  // above loud speech, below typical clap RMS
AUDIO_ATTACK_SMOOTHING  0.6       // fast rise on speech/claps
AUDIO_RELEASE_SMOOTHING 0.08      // slow fall -- avoids flicker between samples
AUDIO_CLAP_DECAY        0.85      // per-frame (~30ms) decay of the clap flash
AUDIO_IDLE_BRIGHTNESS   0.15      // idle glow, as a fraction of full color
AUDIO_EDGE_SOFTNESS     0.15      // falloff width of the expanding pulse edge
```

Raw RMS is normalized against the floor/ceiling and clamped to 0.0–1.0,
then smoothed with a fast attack / slow release (different rates depending
on whether the level is rising or falling) so speech and claps feel
immediate without the visual flickering sample-to-sample. Claps are
detected separately via `AUDIO_CLAP_THRESHOLD` and decay on their own
quick timeline, independent of the slower smoothed level.

**Mute behavior:** while muted, AUDIO_PULSE freezes at its idle glow and
stops reacting to audio — the animation loop is skipped entirely while
muted (same as every other mode), so no CPU is spent computing a pulse
that wouldn't be shown. The I2S microphone capture and `[MIC]` diagnostics
are unaffected and keep running underneath; unmuting resumes audio
reactivity immediately.

**Expected response** (hardware-verified):

| Condition | Behavior |
|---|---|
| Quiet room | Stable idle glow, no flicker; `smoothed` stays ~0.00–0.08 |
| Normal speech | Responsive pulses (`smoothed` rising to 0.5–1.0 on loud syllables), smooth decay between words |
| Music (phone speaker) | Continuous beat-following response, `smoothed` oscillating roughly 0.3–1.0 |
| Clap | Isolated sharp spike (`clap` flag fires, RMS 500k+), brief near-full flash, fast decay |

Throttled diagnostic (~5/second, only while AUDIO_PULSE is active):

```
[AUDIO] rms=316888 norm=1.00 smoothed=0.99 clap=0
```

**Tuning note:** `AUDIO_MAX_RMS` (200,000) means loud music sits pegged
near 1.0 fairly often. This is intentional headroom, not a bug — it may be
raised later (e.g. to 250,000–280,000) for more visual separation between
"loud" and "very loud" if desired.

**Not yet implemented (intentionally out of scope for this mode):**
audio-reactivity in the other 12 modes, FFT/frequency-band analysis, and
any motor/amplifier/speaker integration.

## Microphone (INMP441) diagnostic

Enabled by default at boot (no longer gated by a button — Button 4 now
controls AUDIO_PULSE instead; `toggleMicDiagnostic()` still exists in the
source for future rewiring, it's just unused today):

```
[MIC] bytes=8192 rawMin=-35207 rawMax=32493 peak=33953 rms=11739
```

It reads I2S audio continuously (non-blocking relative to the
button/animation loop — see I2S config below) and prints roughly 8
readings/second: bytes read that window, raw sample min/max, and a
DC-corrected peak and RMS. Buttons and LED animations keep working
normally while it runs. This same capture path feeds `micLatestRms`,
which AUDIO_PULSE (and any future audio-reactive mode) reads directly.

**I2S configuration** (`I2S_NUM_0`, master receive mode):

- Sample rate: 16000 Hz
- 32-bit I2S words; the INMP441 left-justifies a 24-bit sample in each
  word, so firmware recovers it with `sample >> 8`
- LEFT channel selected (`I2S_CHANNEL_FMT_ONLY_LEFT`) — correct because
  the INMP441's `L/R` pin is tied to GND; it would need RIGHT instead if
  `L/R` were tied to VDD
- Standard I2S format (`I2S_COMM_FORMAT_STAND_I2S`)
- 4 DMA buffers × 256 frames
- 20ms read timeout — a pure 0-tick non-blocking poll was tried first but
  raced the DMA's buffer-ready signal and produced false "0 bytes read"
  errors even though the peripheral was working correctly; 20ms resolved
  this while staying short enough to keep buttons/animations responsive
- Streaming DC correction (slow exponential offset tracker) applied to
  every sample before peak/RMS accumulation, so a constant bias can't
  mask or exaggerate real audio content

**Fault detection:** the diagnostic reports (once each, until the
condition clears) if I2S initialization fails, if reads return 0 bytes
repeatedly, if raw samples stay exactly zero for several seconds, if raw
samples stay constant/stuck at any value for several seconds, or if
samples stay saturated near the 24-bit signed range for several seconds.

**Expected response** (hardware-verified):

| Condition | Typical RMS |
|---|---|
| Quiet room | ~8,000–20,000 (ambient + mic self-noise floor) |
| Normal speech near mic | ~40,000–360,000, rising/falling with speech |
| Light tap near the mic board | ~19,000–23,000, brief bump |
| Clap near mic | ~100,000–900,000, sharp spike then fast decay |

Very close or very loud claps may briefly push raw samples to the 24-bit
saturation limit (~±8,388,607) — this is normal clipping from an
excessively loud/close transient, not a fault.

## Safety warnings

- **Do not power the 58-LED strip from the ESP32's 3V3 pin.** At even modest
  brightness, 58 WS2812B LEDs can draw more current than the ESP32's onboard
  3.3V regulator is rated for. GPIO4 provides a **3.3V data signal only** —
  it does not and must not supply LED power.
- **Use a suitable external power supply for the LED strip**, sized for the
  strip's actual current draw at the brightness/color patterns you intend to
  run, with a **common ground** between that supply, the LED strip, and the
  ESP32 GND pin. Do not float the grounds.
