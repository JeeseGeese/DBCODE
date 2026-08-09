# Sunny — Speaker (MAX98357A) Current Status

**Living document — always reflects the current active development state.** For the frozen snapshot captured at the Sunny V1 baseline (2026-08-07), see the matching file in `docs/V1/`. Update this file, not `docs/V1/`, when things change.

**Speaker debugging is still active/ongoing.** This file is the
CURRENT status only. For the detailed historical bring-up narrative
(Stage S0-S3, buzz-diagnosis investigation, format-packing audit), see
`archive/speaker_bringup/` — the useful conclusions from that history
are extracted below; the raw narrative logs are preserved, not deleted.

## Architecture

Shares the microphone's full-duplex I2S bus — see `I2S_ARCHITECTURE.md`.
No dedicated I2S controller of its own; `SpeakerTest.cpp` is the sole
owner of `i2s_write()`/`i2s_zero_dma_buffer()` calls on that shared bus.

## Current wiring and configuration

See `ELECTRICAL.md` for the full pin table. Summary:

- VIN: shared 5V rail. GND: common ground.
- BCLK=GPIO6, LRC=GPIO7 (shared with the mic), DIN=GPIO16.
- **SD (enable)**: currently tied to 3.3V — amplifier enabled. Manually
  moved, not GPIO-driven; the firmware's startup sequence requires
  confirming `[SPEAKER] Digital silence active` on serial before this
  is moved from GND.
- **GAIN**: currently tied to GND — a specific hardware-gain
  configuration being evaluated. Physically observed to improve
  loudness somewhat versus the prior (floating/default) configuration.
- **Speaker**: 40mm / 4Ω / 3W full-range. Physically observed to
  perform **substantially better** than the original small toy speaker
  used in earlier bring-up — this alone accounts for some of the
  perceived improvement, independent of any firmware/gain change.
- Decoupling: 1000 µF + 1 µF, both shunt across 5V/GND (see
  `ELECTRICAL.md`).

## Physically observed usable range

- Roughly **35% through 100% digital amplitude** is currently useful
  output.
- Higher digital levels tend to make the residual buzz **less
  noticeable** (relatively, not necessarily lower in absolute terms —
  not measured).
- **50-100% produced clear, recognizable tones.**
- 100% was physically observed to sound "surprisingly good" with the
  best apparent signal-to-noise ratio of the range tested.
- The frequency sweep sounded worse at lower frequencies and cleaner as
  frequency increased; melody/chord were intelligible but still had
  some fuzz; the noise test sounded like broad "shhh" plus a small buzz.
- A residual buzz/static is present and **not yet resolved**.

## V1.1 normal-use volume model (current, as of this sprint)

Based on the usable-range findings above, the digital-amplitude ladder
for NORMAL speaker output was finalized this sprint, superseding the
earlier Stage S1/S2 bring-up ladder (2%-25%, conservative because gain/
volume had not yet been physically confirmed at all):

```
35% / 50% / 60% / 70% / 80% / 90% / 100%
```

**Boot default: 70%** — below ~35% wasn't physically useful, 50-100%
was clearly intelligible, 70% is a reasonable normal-use midpoint, and
100% remains selectable when specifically wanted. Not persisted across
reboot (no settings/EEPROM system exists in this firmware).

`speaker t`/`1`/`2`/`3`/`sweep`/`melody`/`chord`/`lowmidhigh`/
`speechtest`/`musictest` all read this SAME live volume — one model for
normal speaker output, not two. `speaker volume`/`v` prints it;
`speaker volume <percent>` sets it directly (rejects unsupported
values); `speaker volume up`/`down` (aliases `speaker +`/`-`) step it.
Printed as `[SPEAKER] Volume: 70%`, with `(LOUD)`/`(HIGH)`/`(MAX)`
suffixes at 80/90/100% respectively.

**Explicitly NOT tied to this ladder** (preserved as their own fixed,
diagnostic-safe levels, per this sprint's own requirement not to
disturb them):

- `speaker tone` (Stage S2 bring-up tone) — fixed 5%.
- `speaker fmt1`/`fmt2`/`fmt3` (format A/B diagnostic) — fixed 2%.
- `speaker noise` — capped at min(current volume, 10%), same "never
  louder than this regardless of volume" guarantee as before.
- `loud` (temporary bring-up diagnostic) — hard-capped 20%.

## V1.1 buzz/noise isolation and realistic-content diagnostics (new)

Added this sprint, all reusing the existing generic sample engines (no
new I2S write path, no pin/clock/format change):

- `speaker silencecheck` — forces continuous digital zero (the same
  signal already transmitted between every test) and holds it under an
  explicit label until `speaker stop`; no fixed duration.
- `speaker carriercheck` — same signal, plus an explicit print of the
  live I2S clock rate (`i2s_get_clk()`) and GPIO16 routing state, for
  distinguishing "I2S clocks active + zero samples" from "actual audio
  content" when judging where a noise source sits.
- `speaker lowmidhigh` — 150/440/1500Hz in sequence, equal duration and
  fades, at the current volume (the volume-ladder-aware counterpart to
  the original fixed-10% `low`/`mid`/`high`, which are unchanged).
- `speaker speechtest` — an ORIGINAL synthetic speech-like diagnostic
  (~10s; alternating "syllable" notes across the fundamental adult-
  speech range with word/sentence-boundary gaps and rising/falling
  pitch contours). Not real speech, not copyrighted audio.
- `speaker musictest` — an ORIGINAL short musical diagnostic (~11s;
  low/mid/high notes, rests, short transient-attack notes, and genuine
  per-note amplitude variation/"changing dynamics"). Not a copyrighted
  melody.
- `speaker isolate on`/`off`/`status` — diagnostic-only: commands the
  motor stopped once (`MotorDriver`'s own `motorStop()`) and mutes LEDs
  (`Controls.h`'s existing `isMuted()`/`setMuted()`, the same mechanism
  `MotorPowerGuard`'s `FULL_MUTE` strategy already uses), for isolating
  whether the buzz correlates with motor/LED activity on the shared 5V
  rail. **Best-effort, not a hard interlock** — it does not add itself
  to `isAnyMotorDiagnosticActive()`, so an independently-active motor
  behavior (e.g. `MusicMotorController`) can still re-engage the motor
  during the isolate window. A true interlock would mean changing the
  motor mutual-exclusion architecture, out of scope for a diagnostic-
  only feature.

None of these has been physically tested yet — see "What is NOT yet
conclusively ruled out" below and `ROADMAP.md`.

## APLL clock-quality investigation — CLOSED (source-verified, not a physical test)

Investigated whether `use_apll=false` (`SharedI2S.cpp`) could plausibly
contribute to clock-quality artifacts at 16kHz/32-bit/stereo/1.024MHz
BCLK. Verified directly against this project's installed framework HAL
source before writing anything:

- `framework-arduinoespressif32/.../esp32s3/include/hal/esp32s3/include/hal/i2s_ll.h`'s
  `i2s_ll_tx_clk_set_src()`/`i2s_ll_rx_clk_set_src()` both hardcode
  `tx_clk_sel`/`rx_clk_sel` to `2` (D2CLK) and **ignore their `src`
  parameter entirely**, with the comment "ESP32-S3 only support
  I2S_CLK_D2CLK".
- `soc_caps.h` for esp32s3 does not define `SOC_I2S_SUPPORTS_APLL` (it
  is defined on the original ESP32; absent here).
- Conclusion: APLL is not wired to the I2S peripheral on ESP32-S3 in
  this framework version. The legacy driver's `i2s_config_t.use_apll`
  field exists only because the struct is shared across chips —
  toggling it on this chip/framework has zero effect on the actual
  clock source. An APLL-vs-D2CLK A/B diagnostic would be a no-op, not a
  real test, so **no runtime or compile-time toggle was implemented**.
  This closes the APLL hypothesis for the residual buzz with source-
  level evidence, not a physical test — it was never physically tested
  and now doesn't need to be. See `SharedI2S.cpp`'s `.use_apll = false`
  comment for the full citation.

## What has been ruled out (with evidence)

- **Mono-into-stereo, wrong slot order, sign-handling, integer
  overflow, phase discontinuity across DMA buffer boundaries, or
  double amplitude scaling** — a full source-level review of
  `SharedI2S.cpp`/`SpeakerTest.cpp`'s sample formatting found none of
  these as active bugs (see `archive/speaker_bringup/` for the full
  review). One real (but almost certainly inert on this toolchain)
  defect was found and fixed: a left-shift of a possibly-negative
  signed value (undefined behavior pre-C++20) was rewritten as a
  well-defined unsigned-shift-then-reinterpret — bit-identical output
  on this compiler, not a behavior change.
- **A two-controller I2S design** — conclusively failed differently
  (see `I2S_ARCHITECTURE.md`); not the cause of the current buzz, which
  persists under the working full-duplex architecture.
- **APLL clock source** — see the dedicated section above; closed by
  source-level evidence (not physically supported on this chip/
  framework, not a code bug).

## What is NOT yet conclusively ruled out

- **Which of the three 32-bit-slot packings** (16-bit MSB-justified /
  24-bit left-justified / full 32-bit direct — see `speaker fmt1`/
  `fmt2`/`fmt3` below) the MAX98357A's internal word-length
  auto-detection actually prefers. A diagnostic exists to A/B this
  physically; results have not been reported back as conclusive.
- **Electrical/power-rail noise** on the shared 5V rail (motor +
  amplifier + LEDs) — plausible given the known shared-rail contention
  (see `POWER.md`), not isolated as the specific buzz source. The new
  `speaker isolate`/`silencecheck`/`carriercheck` diagnostics (above)
  exist specifically to help narrow this down, but have not been
  physically exercised yet.
- **Do not claim a proven root cause for the residual buzz anywhere in
  this project.** It remains genuinely open.

## Speaker diagnostic commands (`SpeakerTest.cpp`, via `Controls.cpp`)

Full reference: `README.md`'s "Speaker hardware test" section and
`SpeakerTest.h`'s own comments. Summary of the current surface:

- `speaker tone` / `speaker stop` — the original Stage S2 bring-up
  tone + stop.
- `speaker t`/`1`/`2`/`3` — bench tones at the current normal speaker
  volume (see "V1.1 normal-use volume model" above).
- `speaker volume`/`v` [`<percent>`|`up`|`down`], `speaker +`/`-` — the
  V1.1 normal-use volume ladder (35/50/60/70/80/90/100%, default 70%).
- `speaker fmt1`/`fmt2`/`fmt3`/`fmtstatus` — the 32-bit-slot packing
  A/B diagnostic described above.
- `speaker sweep`/`melody`/`chord`/`lowmidhigh`/`speechtest`/
  `musictest`/`noise` — multi-tone/realistic-content diagnostics at the
  current normal volume (noise independently capped at 10%).
- `speaker silencecheck`/`carriercheck` — hold continuous digital zero
  until `speaker stop`, for buzz isolation (see "V1.1 buzz/noise
  isolation" above).
- `speaker isolate on`/`off`/`status` — diagnostic motor-stop +
  LED-mute mode (best-effort, see above).
- `speaker voltest`/`volquick`/`volstop`/`volstatus` — the automatic
  2%→100% (or shorter 5-level) volume-ladder diagnostic, playing a
  multi-tone sequence at each level, for characterizing usable loudness/
  distortion onset/buzz growth/stability across the full range. This is
  the tool that produced the "physically observed usable range" section
  above. Independent of, and unaffected by, the V1.1 normal-use volume
  ladder.
- `t`/`s`/`low`/`mid`/`high`/`sweep`/`melody`/`beep`/`noise`/`loud` —
  the original flat diagnostic suite (pre-dates the `speaker`-namespaced
  bench), unchanged.
- `music1`-`music4`/`stopmusic` — procedural melody player (original
  compositions, not copyrighted encodings), unchanged.

All reuse the same non-blocking scheduler, the same fade convention,
and the same continuous-digital-silence-between-tests guarantee — see
`docs/development/` if adding a new one is ever needed (not currently
documented as its own SOP; follow the existing pattern in
`SpeakerTest.cpp`).

## Known brownout history

The ESP32 ROM bootloader unconditionally prints its reset reason on
every boot (e.g. `rst:0xf (BROWNOUT_RST)`), visible in this project's
own captured boot logs — this is the existing, always-on brownout
observability. As of this sprint, `main.cpp`'s `setup()` also prints
`[SYSTEM] Reset reason: <name> (<code>)` via `esp_reset_reason()` right
after Serial comes up, plus an explicit warning line if that reason is
`ESP_RST_BROWNOUT` — so the same fact is visible in every captured log
after Serial init, not just in the raw ROM boot text before it. This is
read-only observability; it does not change reset/brownout behavior.
ESP32-S3's hardware brownout detector remains enabled and has not been
disabled anywhere in this firmware. No confirmed brownout event has
been tied specifically to speaker operation as of this baseline (see
`POWER.md` for the general shared-rail risk this sits inside).

Recommended manual measurement points during physical playback testing
(see `POWER.md`): MAX98357A VIN→GND, ESP32 5V→GND, ESP32 3V3→GND,
MAX98357A SD→GND, at silence/50%/70%/100%. Firmware cannot measure
these rails itself — no ADC wiring exists for them.

## Physical validation still needed (V1.1)

None of this sprint's software changes have been physically validated
yet — build/upload succeeded and host tests pass, but no human has
listened to the new volume ladder or diagnostics on real hardware. See
the final report for the exact physical test sequence. In particular:

- Confirm the new 35-100% ladder and 70% default sound as expected
  (no unexpected clipping/distortion at any step).
- Run `silencecheck`/`carriercheck` and listen for buzz with true
  digital silence.
- Run `lowmidhigh`/`speechtest`/`musictest` and assess intelligibility/
  cleanliness versus the existing `melody`/`chord`/sweep tests.
- Try `speaker isolate on` during a tone test and listen for any
  audible change versus isolate off (motor/LED activity permitting).

## Likely future power/noise cleanup topics

- Isolate whether the residual buzz scales with motor/LED activity
  (shared-rail noise) versus being present even with motor/LEDs fully
  idle (points toward digital-format causes instead) — the V1.1
  `isolate`/`silencecheck`/`carriercheck` diagnostics exist for this,
  not yet physically exercised.
- Physically test `speaker fmt1`/`fmt2`/`fmt3` and report which (if
  any) is audibly cleaner.
- Measure `VIN` voltage at the amplifier during idle vs. motor-active
  vs. LED-active vs. combined load (see `docs/SPEAKER_BRINGUP_PLAN.md`'s
  archived preflight checklist for the full list).

(The `use_apll=true` A/B item that used to be listed here is removed —
see "APLL clock-quality investigation" above; it's closed, not
deferred.)
