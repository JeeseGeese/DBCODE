#include "Controls.h"
#include "AudioAnalyzer.h"
#include "AudioVisualState.h"
#include "AutoShowcase.h"
#include "BehaviorEngine.h"
#include "ExpressiveMotion.h"
#include "DanceEngine.h"
#include "MusicMotorController.h"
#include "MotorPwmCalibration.h"
#include "SpeakerTest.h"
#include "VisualCue.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Real measured frame rate, maintained by main.cpp; read here only for the
// 'status' command. Documented cross-file coupling for one lightweight
// value rather than a full getter API.
extern float g_measuredFps;

// --- Selection state ---
static BaseEffect currentBaseEffect = BaseEffect::PETAL_BREATHE;
// The most recently selected NON-AUTO_SHOWCASE effect. AUTO_SHOWCASE never
// overwrites this -- it's how 'a'/re-entering AUTO_SHOWCASE-then-leaving
// gets back to where the user actually was, and it's how the Mode
// button's own effect selection is preserved across a trip through
// AUTO_SHOWCASE.
static BaseEffect lastNormalBaseEffect = BaseEffect::PETAL_BREATHE;
// selectedOverlayMode and audioOverlayEnabled are deliberately independent:
// the selected mode is always one of the real overlays (PULSE, RIPPLE,
// SPARK, LIGHTNING, BASS_BLOOM) and is never itself "OFF" -- OFF is
// represented purely by audioOverlayEnabled being false, so disabling
// never erases which mode was selected.
static AudioOverlay selectedOverlayMode = AudioOverlay::PULSE;
static bool audioOverlayEnabled = false;
static bool muted = false;
static uint8_t brightnessIndex = DEFAULT_BRIGHTNESS_INDEX;

// --- Button debounce state (same edge-triggered pattern as the original firmware) ---
struct DebouncedButton {
  uint8_t pin;
  int lastRawReading = HIGH;
  int stableState = HIGH;
  unsigned long lastDebounceTime = 0;
  explicit DebouncedButton(uint8_t p) : pin(p) {}
};

static DebouncedButton modeButton{BUTTON_MODE_PIN};
static DebouncedButton muteButton{BUTTON_MUTE_PIN};
static DebouncedButton brightnessButton{BUTTON_BRIGHTNESS_PIN};
static DebouncedButton button4{BUTTON4_PIN};

static bool buttonPressedEdge(DebouncedButton &b) {
  int reading = digitalRead(b.pin);
  if (reading != b.lastRawReading) b.lastDebounceTime = millis();

  bool pressed = false;
  if ((millis() - b.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) pressed = true;
    }
  }
  b.lastRawReading = reading;
  return pressed;
}

// --- Single/double click detection, shared by Mode and Button4 ---
// Fed an "edge" (a debounced press, or in Button4's case a debounced
// release-without-long-press) each call; fires SINGLE once the window
// passes with no follow-up edge, or DOUBLE immediately if a second edge
// arrives inside the window. Never blocks -- both outcomes are detected
// by polling elapsed time, not by waiting.
enum class ClickEvent { NONE, SINGLE, DOUBLE };
struct ClickTracker {
  unsigned long lastEdgeTime = 0;
  bool awaitingSingle = false;
};

static ClickEvent pollClickTracker(ClickTracker &t, bool edgeNow, unsigned long now, unsigned long windowMs) {
  if (edgeNow) {
    if (t.awaitingSingle && (now - t.lastEdgeTime) < windowMs) {
      t.awaitingSingle = false;
      return ClickEvent::DOUBLE;
    }
    t.lastEdgeTime = now;
    t.awaitingSingle = true;
    return ClickEvent::NONE;
  }
  if (t.awaitingSingle && (now - t.lastEdgeTime) >= windowMs) {
    t.awaitingSingle = false;
    return ClickEvent::SINGLE;
  }
  return ClickEvent::NONE;
}

static ClickTracker modeClickTracker;

// Temporary diagnostic: prints raw/debounced Button4 transitions only
// (never every loop) when toggled on via the 'b' serial command.
static bool button4DebugEnabled = false;

// Button4 is dual-purpose: a short press (release before
// BUTTON4_LONG_PRESS_MS) toggles the audio-reactive LED overlay -- the
// original single-purpose behavior, preserved exactly, just now fired on
// RELEASE instead of press so it can be suppressed if a long press
// occurs. A press held past the threshold instead toggles expressive
// audio-reactive motor movement, firing once on threshold crossing (not
// on release), so holding longer never fires it twice and the
// short-press action never also fires on release afterward. (An earlier
// single/double-click gesture architecture was removed from this button
// entirely -- the LED Mode button handles overlay *mode* cycling.)
static unsigned long button4PressStartMs = 0;
static bool button4LongPressFired = false;

// Boot-arming guard: if GPIO5 is LOW at boot (observed on real hardware --
// a wiring/short issue, not something firmware can fix), the debounce
// layer would otherwise treat that as a legitimate press and could
// auto-fire actions before anyone touches the button. Action processing
// stays disabled until one stable debounced HIGH (released) is observed.
static bool button4ArmedAfterRelease = false;
static bool button4ArmWaitPrinted = false;

static const char *levelName(int level) { return level == HIGH ? "HIGH" : "LOW"; }

// --- Serial line buffer (Enter-terminated, so single-letter commands like
// 'o' never collide with word commands like "overlays") ---
static char lineBuf[32];
static uint8_t lineLen = 0;

// ============================================================================
// Selection transitions
// ============================================================================
// AUTO_SHOWCASE is just another BaseEffect value (last before COUNT), so
// it's reached/left by the exact same cycling code path as every other
// effect (advanceBaseEffect(), 'n'/'p', Mode button) -- no special-casing
// needed there. This function is the one hook point that starts/stops its
// timer and tracks lastNormalBaseEffect.
static void setBaseEffect(BaseEffect effect) {
  bool wasAutoShowcase = (currentBaseEffect == BaseEffect::AUTO_SHOWCASE);
  currentBaseEffect = effect;

  if (effect == BaseEffect::AUTO_SHOWCASE) {
    startAutoShowcase(millis());
  } else {
    if (wasAutoShowcase) stopAutoShowcase();
    lastNormalBaseEffect = effect;
    resetBaseEffectState(effect, millis());
  }
  Serial.printf("[EFFECT] Base: %s\n", BASE_EFFECT_NAMES[(uint8_t)effect]);
}

static void advanceBaseEffect(int direction) {
  int idx = (int)currentBaseEffect;
  idx = (idx + direction + NUM_BASE_EFFECTS) % NUM_BASE_EFFECTS;
  setBaseEffect((BaseEffect)idx);
}

// Serial 'a': jump straight to/from AUTO_SHOWCASE without cycling through
// every effect in between. Leaving always returns to whichever normal
// effect was last selected (never resets to PETAL_BREATHE unless that
// was genuinely the last one chosen).
static void toggleAutoShowcase() {
  if (currentBaseEffect == BaseEffect::AUTO_SHOWCASE) {
    setBaseEffect(lastNormalBaseEffect);
  } else {
    setBaseEffect(BaseEffect::AUTO_SHOWCASE);
  }
}

// Advances the SELECTED overlay mode only -- never touches
// audioOverlayEnabled. selectedOverlayMode is always one of the real
// overlays (never OFF), so this is a plain wraparound cycle with no OFF
// slot to skip. Used by the Mode button (paired with a base-effect
// advance) and by serial 'o'.
static void advanceOverlayMode() {
  uint8_t nonOffCount = NUM_AUDIO_OVERLAYS - 1; // OFF excluded: selectedOverlayMode is never OFF
  uint8_t prevIdx = (uint8_t)selectedOverlayMode;
  uint8_t idx = (prevIdx % nonOffCount) + 1;
  const char *prevName = AUDIO_OVERLAY_NAMES[prevIdx];
  selectedOverlayMode = (AudioOverlay)idx;
  resetAudioOverlayState(selectedOverlayMode, millis());
  if (audioOverlayEnabled) {
    Serial.printf("[AUDIO] Selected overlay: %s -> %s\n", prevName, AUDIO_OVERLAY_NAMES[idx]);
  } else {
    Serial.printf("[AUDIO] Selected overlay: %s -> %s (overlay currently OFF)\n", prevName, AUDIO_OVERLAY_NAMES[idx]);
  }
}

// Mode-button single press: advances the base effect AND the selected
// overlay mode together. advanceBaseEffect()'s own [EFFECT] print is
// preserved exactly (unchanged, still used standalone by serial 'n'/'p'
// and by Mode's own double-press-for-previous); this adds the paired
// [MODE] LED: PREV -> NEW announcement on top so the pairing is obvious,
// without altering advanceBaseEffect()'s existing behavior at all.
static void advanceModeAndOverlay() {
  const char *prevEffectName = BASE_EFFECT_NAMES[(uint8_t)currentBaseEffect];
  uint8_t nextEffectIdx = ((uint8_t)currentBaseEffect + 1) % NUM_BASE_EFFECTS;
  Serial.printf("[MODE] LED: %s -> %s\n", prevEffectName, BASE_EFFECT_NAMES[nextEffectIdx]);
  advanceBaseEffect(1);
  advanceOverlayMode();
}

// Sets audioOverlayEnabled to an explicit target state and prints the
// "[AUDIO] Overlay: ON/OFF" line -- shared by toggleOverlayOffOn() (Button4
// short press / serial 'x') and setUserAudioModeEnabled() (Button4 long
// hold / unified Audio Mode) below. Idempotent no-op if already at the
// target. Deliberately does NOT fire a visual cue -- a bare LED-overlay
// toggle and a full Audio Mode transition use different cues, so each
// caller fires its own.
static void setAudioOverlayEnabledInternal(bool enabled, unsigned long now) {
  if (audioOverlayEnabled == enabled) return;
  audioOverlayEnabled = enabled;
  if (audioOverlayEnabled) {
    resetAudioOverlayState(selectedOverlayMode, now);
    Serial.println(F("[AUDIO] Overlay: ON"));
  } else {
    Serial.println(F("[AUDIO] Overlay: OFF"));
  }
}

// Trigger point for the visual cue and the ON/OFF toggle itself --
// Button4's short-press release edge and serial 'x'. Never changes which
// overlay mode is selected, only whether it's currently active. The cue is
// armed and the confirmation printed unconditionally, even while muted;
// only the actual LED rendering is suppressed while muted, in main.cpp.
static void toggleOverlayOffOn() {
  unsigned long now = millis();
  setAudioOverlayEnabledInternal(!audioOverlayEnabled, now);
  if (audioOverlayEnabled) {
    startVisualCue(VisualCueType::OVERLAY_ENABLED, now);
    Serial.println(F("[CUE] Audio overlay enabled (green flash)"));
  } else {
    startVisualCue(VisualCueType::OVERLAY_DISABLED, now);
    Serial.println(F("[CUE] Audio overlay disabled (double red flash)"));
  }
}

// Unified Audio Mode -- see Controls.h for the full contract. Derived
// read, never stored separately, so it can't drift from the two states it
// reports on.
bool isUserAudioModeEnabled() {
  return audioOverlayEnabled && isMusicMotorControllerActive();
}

// The ONLY coordinated enable/disable path for Audio Mode -- Button4's
// long-hold handler and the 'audiomode on/off' serial command both funnel
// through this. See Controls.h for the full contract (rejection semantics,
// idempotence, return value).
bool setUserAudioModeEnabled(bool enabled) {
  unsigned long now = millis();

  if (!enabled) {
    // Disabling is always allowed, matching emergency-stop's own "always
    // permitted to stop" philosophy -- never refused, even if only one
    // half was actually on. musicMotorDisable() is MusicMotorController's
    // own safe-shutdown entry point (see MusicMotorController.cpp's
    // hardStop()/resetRuntimeState()) -- this function does not duplicate
    // any of that internal cleanup.
    setAudioOverlayEnabledInternal(false, now);
    musicMotorDisable();
    startVisualCue(VisualCueType::OVERLAY_DISABLED, now);
    Serial.println(F("[AUDIO MODE] OFF | LED overlay=OFF | MusicMotor=OFF | Motor=STOPPED"));
    return true;
  }

  if (isUserAudioModeEnabled()) {
    Serial.println(F("[AUDIO MODE] Already ON"));
    return true;
  }

  // Motor ownership must be secured before touching the LED half -- do not
  // silently enable the LED overlay while the motor half fails. Note:
  // MusicMotorController already being active (e.g. a prior standalone
  // 'musicmotor on' with the overlay still off) is a partial state this
  // call can complete, not a conflict to reject -- isAnyMotorDiagnosticActive()
  // would otherwise report true for MusicMotorController's own presence.
  if (isAnyMotorDiagnosticActive() && !isMusicMotorControllerActive()) {
    const char *owner = currentMotorOwnerName();
    Serial.printf("[AUDIO MODE] Enable rejected: motor owned by %s\n", owner ? owner : "another diagnostic");
    startVisualCue(VisualCueType::AUDIO_MODE_BLOCKED, now);
    return false;
  }

  musicMotorEnable();  // idempotent -- "[MUSIC MOTOR] Already enabled" no-op if already on
  if (!isMusicMotorControllerActive()) {
    // musicMotorEnable() refused internally (e.g. isExpressiveMotionMoving())
    // despite the ownership check above -- report and leave both halves off.
    Serial.println(F("[AUDIO MODE] Enable rejected: MusicMotorController failed to start"));
    startVisualCue(VisualCueType::AUDIO_MODE_BLOCKED, now);
    return false;
  }

  setAudioOverlayEnabledInternal(true, now);
  startVisualCue(VisualCueType::OVERLAY_ENABLED, now);
  Serial.println(F("[AUDIO MODE] ON | LED overlay=ON | MusicMotor=ON"));
  return true;
}

// Any command that gives the user direct control of expressive movement
// (motion off/idle/audio/next/demo, Button4 long-press) must first hand
// movement ownership back from the Behavior Engine, if it currently has
// it -- see BehaviorEngine.h's setBehaviorState() doc comment. Guarded so
// the common case (Behavior Engine was never touched, already MANUAL)
// doesn't print a redundant "(unchanged)" line on every motion command.
static void takeManualMotionControl() {
  if (getBehaviorState() != BehaviorState::MANUAL) setBehaviorState(BehaviorState::MANUAL);
}

static void toggleMute() {
  muted = !muted;
  Serial.println(muted ? F("[MUTE] ON") : F("[MUTE] OFF"));
}

// Exported setter (unlike toggleMute() above, which stays file-local) --
// added for MotorPowerGuard to save/force/restore mute state around motor
// engagement without duplicating LED-control logic elsewhere. Same
// underlying `muted` flag and print behavior as toggleMute(); idempotent
// (no-op, no print) if already at the requested value. Does not touch
// brightness, base effect, or overlay selection -- those are unaffected by
// mute either way.
void setMuted(bool value) {
  if (muted == value) return;
  muted = value;
  Serial.println(muted ? F("[MUTE] ON") : F("[MUTE] OFF"));
}

static void brightnessUp() {
  brightnessIndex = (brightnessIndex + 1) % NUM_BRIGHTNESS_LEVELS;
  Serial.printf("[BRIGHTNESS] %d%% | raw=%d\n", BRIGHTNESS_PERCENTS[brightnessIndex], BRIGHTNESS_RAW[brightnessIndex]);
}

static void brightnessDown() {
  brightnessIndex = (brightnessIndex == 0) ? (NUM_BRIGHTNESS_LEVELS - 1) : (brightnessIndex - 1);
  Serial.printf("[BRIGHTNESS] %d%% | raw=%d\n", BRIGHTNESS_PERCENTS[brightnessIndex], BRIGHTNESS_RAW[brightnessIndex]);
}

// ============================================================================
// Button handlers
// ============================================================================
// Single press: existing behavior, now also advances the overlay mode.
// Double press: existing "previous base effect" behavior, unchanged --
// does not touch the overlay (only the single/"next" case pairs with it,
// matching the paired-announcement example this was specified against).
static void handleModeButton(unsigned long now) {
  bool pressed = buttonPressedEdge(modeButton);
  ClickEvent evt = pollClickTracker(modeClickTracker, pressed, now, DOUBLE_PRESS_WINDOW_MS);
  if (evt == ClickEvent::SINGLE) advanceModeAndOverlay();
  else if (evt == ClickEvent::DOUBLE) advanceBaseEffect(-1);
}

static void handleMuteButton() {
  if (buttonPressedEdge(muteButton)) toggleMute();
}

static void handleBrightnessButton() {
  if (buttonPressedEdge(brightnessButton)) brightnessUp();
}

// Button4 long-press action: toggles the unified Audio Mode (LED
// audio-reactive overlay + MusicMotorController together -- see
// setUserAudioModeEnabled() above). This is the ONLY physical-button path
// to music-driven motor dancing; normal users never need the serial
// monitor to start it.
//
// Formerly toggled ExpressiveMotion's AUDIO_REACTIVE mode instead (gentle
// idle-style audio-reactive pulses, a different and still-fully-present
// system -- see ExpressiveMotion.h). That mode has simply lost its
// physical-button binding now that MusicMotorController Revision 10.1 is
// Sunny's production music-driven dancing engine; it remains reachable via
// the 'motion audio' serial command for development/comparison.
static void handleButton4LongPress(unsigned long now) {
  (void)now;  // setUserAudioModeEnabled() times its own cue internally
  setUserAudioModeEnabled(!isUserAudioModeEnabled());
}

// Button4 is dual-purpose (see the state variables' comment above): a
// short press toggles the audio-reactive LED overlay (fires on RELEASE,
// only if no long press occurred this hold); a press held past
// BUTTON4_LONG_PRESS_MS instead toggles expressive audio-reactive motor
// movement (fires once, on threshold crossing).
static void handleButton4(unsigned long now) {
  // Read raw GPIO.
  int reading = digitalRead(button4.pin);
  if (reading != button4.lastRawReading) {
    if (button4DebugEnabled) {
      Serial.printf("[BUTTON4 RAW] %s -> %s\n", levelName(button4.lastRawReading), levelName(reading));
    }
    button4.lastDebounceTime = now;
  }

  // Update debounce state; capture the press/release edges.
  bool pressedEdge = false;
  bool releasedEdge = false;
  if ((now - button4.lastDebounceTime) > BUTTON4_DEBOUNCE_MS) {
    if (reading != button4.stableState) {
      button4.stableState = reading;
      if (button4.stableState == LOW) {
        pressedEdge = true;
        if (button4DebugEnabled) Serial.println(F("[BUTTON4 DEBOUNCED] PRESSED"));
      } else {
        releasedEdge = true;
        if (button4DebugEnabled) Serial.println(F("[BUTTON4 DEBOUNCED] RELEASED"));
      }
    }
  }
  button4.lastRawReading = reading;

  // Boot-arming: don't act on a press that was already in progress (or a
  // stuck-LOW pin) before firmware even started. Require one observed
  // stable released/HIGH state first. If the pin never reaches HIGH, this
  // intentionally disables Button4 entirely rather than firing a spurious
  // toggle -- that's a hardware fault to fix, not something more firmware
  // logic can paper over.
  if (!button4ArmedAfterRelease) {
    if (!button4ArmWaitPrinted) {
      Serial.println(F("[BUTTON4] Waiting for released HIGH state before arming"));
      button4ArmWaitPrinted = true;
    }
    if (button4.stableState == HIGH) {
      button4ArmedAfterRelease = true;
      Serial.println(F("[BUTTON4] Armed"));
    }
    return;
  }

  if (pressedEdge) {
    button4PressStartMs = now;
    button4LongPressFired = false;
  }

  // Long-press: fires exactly once, the instant the held duration crosses
  // the threshold -- not on release, so a longer hold never fires it
  // again (button4LongPressFired latches until the next press edge), and
  // switch bounce can't create duplicate events since this only evaluates
  // against the already-debounced stableState==LOW, not raw readings.
  if (button4.stableState == LOW && !button4LongPressFired && (now - button4PressStartMs) >= BUTTON4_LONG_PRESS_MS) {
    button4LongPressFired = true;
    handleButton4LongPress(now);
  }

  if (releasedEdge) {
    if (button4LongPressFired) {
      // Long press already handled its own action above -- release after
      // a long press must not also fire the short-press overlay toggle.
      return;
    }
    Serial.println(F("[BUTTON4] Audio overlay toggle"));
    toggleOverlayOffOn();
  }
}

// ============================================================================
// Serial commands
// ============================================================================
static void printHelp() {
  Serial.println();
  Serial.println(F("[HELP] Serial commands (press Enter after each):"));
  Serial.println(F("  n = next base effect       p = previous base effect"));
  Serial.println(F("  o = next audio overlay     x = toggle audio overlay off/on"));
  Serial.println(F("  + = brightness up          - = brightness down"));
  Serial.println(F("  m = mute toggle            d = audio diagnostics"));
  Serial.println(F("  h = this help"));
  Serial.println(F("  effects  = list base effects"));
  Serial.println(F("  overlays = list audio overlays"));
  Serial.println(F("  status   = full system status"));
  Serial.println(F("  motion [next|off|idle|audio|status|demo] = expressive motion (dev branch, see README)"));
  Serial.println(F("  behavior/beh [next|manual|idle|curious|listening|pondering|excited|sleeping|status|demo]"));
  Serial.println(F("           = personality-state coordinator (dev branch, see README)"));
  Serial.println(F("  t/s = speaker sine/square 440Hz test tone (dev branch, see README)"));
  Serial.println(F("  low|mid|high = speaker sine test at 150/440/1500Hz     sweep = 150->3000Hz log sweep"));
  Serial.println(F("  melody = C5 E5 G5 C6 x2      beep = 1000Hz 150ms on/off x5      noise = white noise"));
  Serial.println(F("  loud = [TEMPORARY DIAGNOSTIC] 1000Hz at 20% amplitude"));
  Serial.println(F("  music1|music2|music3|music4 = Twinkle/Mary/Ode to Joy/Mario (loops until stopped)"));
  Serial.println(F("  stopmusic = stop music playback, return to digital silence"));
  Serial.println(F("  g = [DIAGNOSTIC ONLY] force the enabled/green cue, no overlay state change"));
  Serial.println(F("  r = [DIAGNOSTIC ONLY] force the disabled/red cue, no overlay state change"));
  Serial.println(F("  b = [DIAGNOSTIC ONLY] toggle raw/debounced Button4 transition trace"));
  Serial.println(F("  a = toggle AUTO_SHOWCASE (jump to it, or back to the last normal effect)"));
  Serial.println(F("  c = force AUTO_SHOWCASE to its next effect now (no-op if not active)"));
  Serial.println(F("  v = print current audio visual-control state (level/bass/transient/derived bands, pool counts)"));
  Serial.println(F("  mf/mr = select motor PWM-test direction forward/reverse     mstop = coast + cancel motor test"));
  Serial.println(F("  m20|m30|...|m100 (any m1-m100) = run selected direction continuously at that % duty"));
  Serial.println(F("  mramp = automatic PWM ramp test      mcycle = automatic dance-style speed/direction test"));
  Serial.println(F("  mkick = toggle startup kick on/off   mstatus = full motor PWM-test status (dev branch, see README)"));
#if ENABLE_LEGACY_DANCE_ENGINE
  Serial.println(F("  danceon/danceoff = enable/disable live mic-driven Dance Engine (80-100% validated active range)"));
  Serial.println(F("  dancestatus = full dance status      dancetest/dancetestoff = deterministic simulated sequence"));
  Serial.println(F("  dancequiet|dancemid|dancehigh|dancepeak = temporarily simulate that energy band (dev branch, see README)"));
#endif
  Serial.println(F("  audiomode on/off/status = unified Audio Mode (LED overlay + MusicMotor together) -- same as"));
  Serial.println(F("           Button4 long-hold; normal users use the button, this is a dev/test convenience"));
  Serial.println(F("  musicmotor on/off = enable/disable music-reactive movement ALONE (intensity sway -> bass accent"));
  Serial.println(F("           -> hip shake/extended spin -> decel)   musicmotor status = full status"));
  Serial.println(F("  musicmotor intensity = energy/band/threshold snapshot   musicmotor spin = manual test spin"));
  Serial.println(F("  musicmotor motion = physical calibration/tuning surface (floor, ranges, drop-hold, hip-shake, spin)"));
  Serial.println(F("  musicmotor slow|fast|hitthreshold|beatthreshold|accel|hold|decel <value> = temporary tuning"));
  Serial.println(F("  musicmotor lowthreshold|mediumthreshold|highthreshold <value> = intensity-band tuning"));
  Serial.println(F("  musicmotor peakthreshold <0.0-1.0> = set PEAK threshold (validated: must exceed highthreshold)"));
  Serial.println(F("  musicmotor bandpeak <0.0-1.0>    alias for peakthreshold, same underlying value"));
  Serial.println(F("  musicmotor spintime|spincooldown <value> = spin tuning (does not persist through reboot; see README)"));
  Serial.println(F("  musicmotor rotationhold <ms> = min time committed to a direction before an ordinary reversal (favor continuation)"));
  Serial.println(F("  musicmotor debug on|off|status = detailed decision diagnostics (dropHold/band/strongHit/"));
  Serial.println(F("           choreography evaluation lines); diagnostic only, never changes movement behavior"));
  Serial.println(F("  musicmotor summary = revision 9 compact post-song session stats (band time, drops, phrases, max speed)"));
  Serial.println(F("  musicmotor dropdetect on|off = revision 9 relative/song-adaptive (EDM/dubstep) drop-detection A/B toggle"));
  Serial.println(F("  musicmotor dynamics status = revision 10 motion palette/duty-cycle/drop-phrase config surface"));
  Serial.println(F("  musicmotor quietmotion on|off = revision 10 QUIET_BUILDUP subtle-sway toggle"));
  Serial.println(F("  musicmotor switchchance <0-100>|switchcooldown <ms>|switchlimit <count> = revision 10 drop-phrase tuning"));
  Serial.println(F("  musicmotor test = one-command physical-test setup (on+dropdetect+debug+quietmotion, summary reset)"));
  Serial.println(F("  musicmotor test stop = ends test mode: prints summary, disables debug logging, stops safely"));
  Serial.println();
}

static void printEffectsList() {
  Serial.println(F("[EFFECTS]"));
  for (uint8_t i = 0; i < NUM_BASE_EFFECTS; i++) {
    Serial.printf("  %s%d - %s\n", (i == (uint8_t)currentBaseEffect) ? "-> " : "   ", i, BASE_EFFECT_NAMES[i]);
  }
}

static void printOverlaysList() {
  Serial.println(F("[OVERLAYS]"));
  for (uint8_t i = 0; i < NUM_AUDIO_OVERLAYS; i++) {
    Serial.printf("  %s%d - %s\n", (i == (uint8_t)selectedOverlayMode) ? "-> " : "   ", i, AUDIO_OVERLAY_NAMES[i]);
  }
}

static void printAudioVisualState() {
  const AudioVisualState &v = getAudioVisualState();
  Serial.println(F("[VISUAL]"));
  Serial.printf("  level=%.3f envelope=%.3f bass=%.3f transient=%.3f transientStrength=%.3f clap=%d\n", v.level,
                v.envelope, v.bass, v.transient, v.transientStrength, v.clap ? 1 : 0);
  Serial.printf("  lowRange=%.3f midRange=%.3f highRange=%.3f (derived animation-control bands -- NOT real frequency bands, see README)\n",
                v.lowRange, v.midRange, v.highRange);
  Serial.printf("  energy8=%u bass8=%u transient8=%u\n", v.energy8, v.bass8, v.transient8);
  Serial.printf("  activeRipples=%u activeSparks=%u activeComets=%u\n", getActiveRippleCount(), getActiveSparkCount(),
                getActiveCometCount());
}

void printStatus() {
  const AudioFeatures &f = getAudioFeatures();
  const AudioVisualState &v = getAudioVisualState();
  unsigned long now = millis();
  Serial.println(F("[STATUS]"));
  Serial.printf("  baseEffect=%s\n", BASE_EFFECT_NAMES[(uint8_t)currentBaseEffect]);
  if (currentBaseEffect == BaseEffect::AUTO_SHOWCASE) {
    Serial.printf("  autoShowcaseCurrentEffect=%s\n", BASE_EFFECT_NAMES[(uint8_t)getAutoShowcaseCurrentEffect()]);
    Serial.printf("  autoShowcaseMsRemaining=%lu\n", (unsigned long)getAutoShowcaseMsRemaining(now));
  }
  Serial.printf("  brightness=%d%% raw=%d\n", BRIGHTNESS_PERCENTS[brightnessIndex], BRIGHTNESS_RAW[brightnessIndex]);
  Serial.printf("  mute=%s\n", muted ? "ON" : "OFF");
  Serial.printf("  rms=%.0f normalized=%.3f envelope=%.3f noiseFloor=%.0f\n", f.rms, f.normalized, f.envelope,
                getNoiseFloorEstimate());
  Serial.printf("  clap=%d transient=%d lowFrequencyEnergy=%.3f\n", f.clap ? 1 : 0, f.transient ? 1 : 0,
                f.lowFrequencyEnergy);
  Serial.printf("  frameIntervalMs=%d measuredFps=%.1f\n", FRAME_INTERVAL_MS, g_measuredFps);
  Serial.printf("  Selected audio-overlay mode: %s\n", AUDIO_OVERLAY_NAMES[(uint8_t)selectedOverlayMode]);
  Serial.printf("  Audio overlay enabled: %s\n", audioOverlayEnabled ? "YES" : "NO");
  {
    // Unified Audio Mode -- derived, never stored separately (see
    // isUserAudioModeEnabled()). PARTIAL means exactly one half is on,
    // which only happens after independently toggling the LED overlay
    // ('x') or MusicMotorController ('musicmotor on/off') via serial --
    // Button4 long-hold and 'audiomode on/off' always drive both together.
    bool motorOn = isMusicMotorControllerActive();
    const char *audioModeState = (audioOverlayEnabled && motorOn)   ? "ON"
                                  : (!audioOverlayEnabled && !motorOn) ? "OFF"
                                                                        : "PARTIAL";
    Serial.printf("  Audio Mode (unified): %s | LED overlay=%s MusicMotor=%s\n", audioModeState,
                  audioOverlayEnabled ? "ON" : "OFF", motorOn ? "ON" : "OFF");
  }
  Serial.printf("  Current audio energy=%.3f Current bass control=%.3f Current transient control=%.3f\n", v.level,
                v.bass, v.highRange);
  Serial.printf("  Active ripple count=%u Active particle/comet count=%u\n", getActiveRippleCount(),
                (uint16_t)getActiveSparkCount() + getActiveCometCount());
  Serial.printf("  Button 4 pin: GPIO%d\n", BUTTON4_PIN);
  Serial.printf("  Button 4 raw state: %s\n", levelName(digitalRead(BUTTON4_PIN)));
  Serial.printf("  Button 4 debounced state: %s\n", button4.stableState == LOW ? "PRESSED" : "RELEASED");
  Serial.printf("  Button 4 debounce interval: %ums\n", (unsigned)BUTTON4_DEBOUNCE_MS);
  Serial.printf("  Button 4 long-press threshold: %ums\n", (unsigned)BUTTON4_LONG_PRESS_MS);
}

// "motion" word command -- see include/ExpressiveMotion.h. `args` is
// whatever follows "motion" in the line (leading spaces stripped below),
// e.g. "" or "next" for a bare "motion", or "off"/"idle"/"audio"/"status"/
// "demo" for "motion off" etc. Routed here from dispatchCommand() below,
// which is itself fed one byte at a time by main.cpp's central serial
// dispatcher via feedSerialByte() -- no new Serial reader is introduced.
static void dispatchMotionCommand(const char *args) {
  while (*args == ' ') args++;
  // "status" is a read-only query -- it does not change movement ownership,
  // so it deliberately does not call takeManualMotionControl() (see that
  // function's comment). Every other recognized subcommand below takes
  // direct manual control of expressive movement.
  if (strcasecmp(args, "status") == 0) {
    printExpressiveMotionDebugState();
  } else if (*args == '\0' || strcasecmp(args, "next") == 0) {
    takeManualMotionControl();
    cycleExpressiveMotionMode();
  } else if (strcasecmp(args, "off") == 0) {
    takeManualMotionControl();
    setExpressiveMotionMode(ExpressiveMotionMode::OFF);
  } else if (strcasecmp(args, "idle") == 0) {
    takeManualMotionControl();
    setExpressiveMotionMode(ExpressiveMotionMode::IDLE_ALIVE);
  } else if (strcasecmp(args, "audio") == 0) {
    takeManualMotionControl();
    setExpressiveMotionMode(ExpressiveMotionMode::AUDIO_REACTIVE);
  } else if (strcasecmp(args, "demo") == 0) {
    takeManualMotionControl();
    startMotionDemo();
  } else {
    Serial.printf("[CMD] Unknown 'motion' subcommand '%s' -- try: motion [next|off|idle|audio|status|demo]\n", args);
  }
}

// "behavior"/"beh" word command -- see include/BehaviorEngine.h. `args` is
// whatever follows the matched prefix in the line (leading spaces stripped
// below). No-arg "behavior" (or "beh") prints subcommand help + current
// status, matching the spec's "no-arg = help+status" requirement.
// "pondering", not "thinking": the central serial dispatcher
// (main.cpp's pollSerialDispatcher()) intercepts 'k' unconditionally, even
// mid-word (deliberately -- see its own comment, from the original
// emergency-stop race investigation). "thinking" contains a 'k', so typing
// it would always trigger an emergency stop partway through and corrupt
// the rest of the line -- found via serial validation testing. The
// BehaviorState::THINKING enum name itself is unaffected; only this
// serial-facing token is renamed.
static void printBehaviorHelp() {
  Serial.println(F(
      "[BEHAVIOR] Subcommands: next|manual|idle|curious|listening|pondering|excited|sleeping|status|demo"));
}

static BehaviorState nextBehaviorState(BehaviorState s) {
  switch (s) {
    case BehaviorState::MANUAL: return BehaviorState::IDLE;
    case BehaviorState::IDLE: return BehaviorState::CURIOUS;
    case BehaviorState::CURIOUS: return BehaviorState::LISTENING;
    case BehaviorState::LISTENING: return BehaviorState::THINKING;
    case BehaviorState::THINKING: return BehaviorState::EXCITED;
    case BehaviorState::EXCITED: return BehaviorState::SLEEPING;
    case BehaviorState::SLEEPING: return BehaviorState::MANUAL;
  }
  return BehaviorState::MANUAL;
}

static void dispatchBehaviorCommand(const char *args) {
  while (*args == ' ') args++;
  if (*args == '\0') {
    printBehaviorHelp();
    printBehaviorStatus();
  } else if (strcasecmp(args, "next") == 0) {
    setBehaviorState(nextBehaviorState(getBehaviorState()));
  } else if (strcasecmp(args, "manual") == 0) {
    setBehaviorState(BehaviorState::MANUAL);
  } else if (strcasecmp(args, "idle") == 0) {
    setBehaviorState(BehaviorState::IDLE);
  } else if (strcasecmp(args, "curious") == 0) {
    setBehaviorState(BehaviorState::CURIOUS);
  } else if (strcasecmp(args, "listening") == 0) {
    setBehaviorState(BehaviorState::LISTENING);
  } else if (strcasecmp(args, "pondering") == 0) {
    setBehaviorState(BehaviorState::THINKING);
  } else if (strcasecmp(args, "excited") == 0) {
    setBehaviorState(BehaviorState::EXCITED);
  } else if (strcasecmp(args, "sleeping") == 0) {
    setBehaviorState(BehaviorState::SLEEPING);
  } else if (strcasecmp(args, "status") == 0) {
    printBehaviorStatus();
  } else if (strcasecmp(args, "demo") == 0) {
    startBehaviorDemo();
  } else {
    printBehaviorHelp();
    Serial.printf("[CMD] Unknown 'behavior' subcommand '%s'\n", args);
  }
}

// "audiomode" word command -- the serial-facing equivalent of Button4's
// long-hold gesture (see setUserAudioModeEnabled() above). A development/
// testing convenience only; normal users operate Audio Mode via the
// physical button and never need this. "audiomode status" (or bare
// "audiomode") reports ON/OFF/PARTIAL the same way 'status' does, so a
// partial state left over from independent 'x' / 'musicmotor on|off' use
// is never mistaken for full Audio Mode.
static void dispatchAudioModeCommand(const char *args) {
  while (*args == ' ') args++;
  if (strcasecmp(args, "on") == 0) {
    setUserAudioModeEnabled(true);
  } else if (strcasecmp(args, "off") == 0) {
    setUserAudioModeEnabled(false);
  } else if (strcasecmp(args, "status") == 0 || *args == '\0') {
    bool motorOn = isMusicMotorControllerActive();
    const char *state = (audioOverlayEnabled && motorOn)      ? "ON"
                         : (!audioOverlayEnabled && !motorOn)  ? "OFF"
                                                                : "PARTIAL";
    Serial.printf("[AUDIO MODE] %s | LED overlay=%s MusicMotor=%s\n", state, audioOverlayEnabled ? "ON" : "OFF",
                  motorOn ? "ON" : "OFF");
  } else {
    Serial.printf("[CMD] Unknown 'audiomode' subcommand '%s' -- try: on|off|status\n", args);
  }
}

// "musicmotor" word command -- see include/MusicMotorController.h. `args`
// is whatever follows "musicmotor" in the line (leading spaces stripped
// below). Tuning subcommands ("slow"/"fast"/"hitthreshold"/"beatthreshold"/
// "accel"/"hold"/"decel") each take one trailing numeric value and are
// temporary physical-tuning aids -- values do not persist through reboot.
static void dispatchMusicMotorCommand(const char *args) {
  while (*args == ' ') args++;
  if (strcasecmp(args, "on") == 0) {
    musicMotorEnable();
    return;
  } else if (strcasecmp(args, "off") == 0) {
    musicMotorDisable();
    return;
  } else if (strcasecmp(args, "status") == 0) {
    musicMotorPrintStatus();
    return;
  } else if (strcasecmp(args, "intensity") == 0) {
    musicMotorPrintIntensity();
    return;
  } else if (strcasecmp(args, "motion") == 0) {
    musicMotorPrintMotion();
    return;
  } else if (strcasecmp(args, "summary") == 0) {
    musicMotorPrintSummary();
    return;
  } else if (strncasecmp(args, "test", 4) == 0 && (args[4] == '\0' || args[4] == ' ')) {
    // "musicmotor test" / "musicmotor test stop" -- one-command physical
    // validation setup/teardown. No other subcommands under "test".
    const char *testArg = args + 4;
    while (*testArg == ' ') testArg++;
    if (*testArg == '\0') {
      musicMotorEnterTestMode();
    } else if (strcasecmp(testArg, "stop") == 0) {
      musicMotorExitTestMode();
    } else {
      Serial.printf("[CMD] Unknown 'musicmotor test' subcommand '%s' -- try: (none)|stop\n", testArg);
    }
    return;
  } else if (strcasecmp(args, "spin") == 0) {
    musicMotorTriggerSpin();
    return;
  } else if (strncasecmp(args, "dropdetect", 10) == 0 && (args[10] == '\0' || args[10] == ' ')) {
    // "musicmotor dropdetect on|off" -- Revision 9 relative-drop A/B
    // toggle, same word-subcommand shape as "musicmotor debug".
    const char *dropArg = args + 10;
    while (*dropArg == ' ') dropArg++;
    if (strcasecmp(dropArg, "on") == 0) {
      musicMotorSetRelativeDropEnabled(true);
    } else if (strcasecmp(dropArg, "off") == 0) {
      musicMotorSetRelativeDropEnabled(false);
    } else {
      Serial.printf("[CMD] Unknown 'musicmotor dropdetect' subcommand '%s' -- try: on|off\n", dropArg);
    }
    return;
  } else if (strncasecmp(args, "debug", 5) == 0 && (args[5] == '\0' || args[5] == ' ')) {
    // "musicmotor debug on|off|status" -- a word sub-argument, not a
    // numeric value, so handled here rather than falling into the generic
    // "<name> <number>" parser below.
    const char *debugArg = args + 5;
    while (*debugArg == ' ') debugArg++;
    if (strcasecmp(debugArg, "on") == 0) {
      musicMotorSetDebugLogging(true);
    } else if (strcasecmp(debugArg, "off") == 0) {
      musicMotorSetDebugLogging(false);
    } else if (strcasecmp(debugArg, "status") == 0) {
      musicMotorPrintDebugStatus();
    } else {
      Serial.printf("[CMD] Unknown 'musicmotor debug' subcommand '%s' -- try: on|off|status\n", debugArg);
    }
    return;
  } else if (strncasecmp(args, "dynamics", 8) == 0 && (args[8] == '\0' || args[8] == ' ')) {
    // "musicmotor dynamics status" -- Revision 10 config surface.
    const char *dynArg = args + 8;
    while (*dynArg == ' ') dynArg++;
    if (strcasecmp(dynArg, "status") == 0) {
      musicMotorPrintDynamicsStatus();
    } else {
      Serial.printf("[CMD] Unknown 'musicmotor dynamics' subcommand '%s' -- try: status\n", dynArg);
    }
    return;
  } else if (strncasecmp(args, "quietmotion", 11) == 0 && (args[11] == '\0' || args[11] == ' ')) {
    // "musicmotor quietmotion on|off" -- Revision 10 QUIET_BUILDUP toggle.
    const char *qmArg = args + 11;
    while (*qmArg == ' ') qmArg++;
    if (strcasecmp(qmArg, "on") == 0) {
      musicMotorSetQuietBuildupMotionEnabled(true);
    } else if (strcasecmp(qmArg, "off") == 0) {
      musicMotorSetQuietBuildupMotionEnabled(false);
    } else {
      Serial.printf("[CMD] Unknown 'musicmotor quietmotion' subcommand '%s' -- try: on|off\n", qmArg);
    }
    return;
  }

  // Remaining subcommands are "<name> <number>" -- split on the first space.
  const char *space = strchr(args, ' ');
  size_t nameLen = space ? (size_t)(space - args) : strlen(args);
  const char *valueStr = space ? space + 1 : "";
  while (*valueStr == ' ') valueStr++;

  auto nameIs = [&](const char *name) { return strlen(name) == nameLen && strncasecmp(args, name, nameLen) == 0; };

  if (nameIs("slow")) {
    musicMotorSetSlowPercent((uint8_t)constrain((int)strtol(valueStr, nullptr, 10), 0, 100));
  } else if (nameIs("fast")) {
    musicMotorSetFastPercent((uint8_t)constrain((int)strtol(valueStr, nullptr, 10), 0, 100));
  } else if (nameIs("hitthreshold")) {
    musicMotorSetStrongHitThreshold((float)atof(valueStr));
  } else if (nameIs("beatthreshold")) {
    musicMotorSetBeatThreshold((float)atof(valueStr));
  } else if (nameIs("accel")) {
    musicMotorSetAccelMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("hold")) {
    musicMotorSetHoldMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("decel")) {
    musicMotorSetDecelMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("lowthreshold")) {
    musicMotorSetLowThreshold((float)atof(valueStr));
  } else if (nameIs("mediumthreshold")) {
    musicMotorSetMediumThreshold((float)atof(valueStr));
  } else if (nameIs("highthreshold")) {
    musicMotorSetHighThreshold((float)atof(valueStr));
  } else if (nameIs("peakthreshold") || nameIs("bandpeak")) {
    // "bandpeak" is an alias for "peakthreshold" -- both update the exact
    // same tunablePeakThreshold variable via the same setter (which also
    // owns the low<medium<high<peak<=1.0 ordering validation), never a
    // separate value. See MusicMotorController.cpp's musicMotorSetPeakThreshold().
    musicMotorSetPeakThreshold((float)atof(valueStr));
  } else if (nameIs("spintime")) {
    musicMotorSetSpinTimeMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("spincooldown")) {
    musicMotorSetSpinCooldownMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("rotationhold")) {
    musicMotorSetMinRotationHoldMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("switchchance")) {
    musicMotorSetSwitchChancePercent((uint8_t)constrain((int)strtol(valueStr, nullptr, 10), 0, 100));
  } else if (nameIs("switchcooldown")) {
    musicMotorSetSwitchCooldownMs((uint32_t)strtoul(valueStr, nullptr, 10));
  } else if (nameIs("switchlimit")) {
    musicMotorSetSwitchLimit((uint8_t)constrain((int)strtol(valueStr, nullptr, 10), 0, 255));
  } else {
    Serial.printf(
        "[CMD] Unknown 'musicmotor' subcommand '%s' -- try: on|off|status|intensity|motion|summary|test|test stop|spin|"
        "slow|fast|hitthreshold|beatthreshold|accel|hold|decel|lowthreshold|mediumthreshold|highthreshold|peakthreshold "
        "(alias: bandpeak)|spintime|spincooldown|rotationhold|switchchance|switchcooldown|switchlimit|debug "
        "on|off|status|dropdetect on|off|dynamics status|quietmotion on|off\n",
        args);
  }
}

static void dispatchCommand(const char *cmd) {
  size_t len = strlen(cmd);
  if (len == 1) {
    switch (tolower((unsigned char)cmd[0])) {
      case 'n': advanceBaseEffect(1); return;
      case 'p': advanceBaseEffect(-1); return;
      case 'o': advanceOverlayMode(); return;
      case 'x': toggleOverlayOffOn(); return;
      case '+': brightnessUp(); return;
      case '-': brightnessDown(); return;
      case 'm': toggleMute(); return;
      case 'd': printAudioDiagnostics(); return;
      case 'h': printHelp(); return;
      // Diagnostic-only: force a cue in isolation, without touching overlay
      // state, to separate cue-rendering bugs from button/overlay logic.
      case 'g': startVisualCue(VisualCueType::OVERLAY_ENABLED, millis()); return;
      case 'r': startVisualCue(VisualCueType::OVERLAY_DISABLED, millis()); return;
      // Diagnostic-only: raw/debounced Button4 transition trace, for
      // isolating input-detection issues from overlay/cue logic.
      case 'b':
        button4DebugEnabled = !button4DebugEnabled;
        Serial.println(button4DebugEnabled ? F("[BUTTON4 DEBUG] ON") : F("[BUTTON4 DEBUG] OFF"));
        return;
      case 'a': toggleAutoShowcase(); return;
      case 'c':
        if (currentBaseEffect == BaseEffect::AUTO_SHOWCASE) {
          autoShowcaseForceNext(millis());
          Serial.println(F("[AUTO_SHOWCASE] Forced next effect"));
        } else {
          Serial.println(F("[CMD] 'c' only applies while AUTO_SHOWCASE is active (see 'a')"));
        }
        return;
      case 'v': printAudioVisualState(); return;
      // Speaker diagnostic suite -- Enter-terminated word commands, not
      // reserved immediate bytes in main.cpp's dispatcher (unlike an
      // earlier revision of this feature): 't'/'s' being reserved-and-
      // immediate meant the very first byte of any WORD command starting
      // with 't'/'s' -- including the pre-existing "status" and this
      // suite's own "sweep" -- was intercepted before a pending line
      // existed to route it through instead, silently corrupting the rest
      // of the word (e.g. "sweep" fired the 's' square-wave test, then
      // dispatched the leftover "weep" as an unknown command). Moving
      // both here removes the collision at its root; the only user-visible
      // change is that 't'/'s' now need Enter, matching every other
      // command in this suite.
      case 't': startSpeakerTestTone(); return;
      case 's': startSpeakerSquareWaveTest(); return;
      default: break;
    }
    Serial.printf("[CMD] Unknown command '%c' -- press 'h' for help\n", cmd[0]);
    return;
  }

  if (strcasecmp(cmd, "effects") == 0) printEffectsList();
  else if (strcasecmp(cmd, "overlays") == 0) printOverlaysList();
  else if (strcasecmp(cmd, "status") == 0) printStatus();
  else if (strncasecmp(cmd, "motion", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ')) dispatchMotionCommand(cmd + 6);
  else if (strncasecmp(cmd, "behavior", 8) == 0 && (cmd[8] == '\0' || cmd[8] == ' ')) dispatchBehaviorCommand(cmd + 8);
  else if (strncasecmp(cmd, "beh", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) dispatchBehaviorCommand(cmd + 3);
  else if (strncasecmp(cmd, "musicmotor", 10) == 0 && (cmd[10] == '\0' || cmd[10] == ' '))
    dispatchMusicMotorCommand(cmd + 10);
  else if (strncasecmp(cmd, "audiomode", 9) == 0 && (cmd[9] == '\0' || cmd[9] == ' '))
    dispatchAudioModeCommand(cmd + 9);
  // Speaker diagnostic suite word commands (see include/SpeakerTest.h) --
  // ordinary Enter-terminated word commands, matching "motion"/"behavior"
  // ('t'/'s' are single-char but ALSO Enter-terminated -- see this file's
  // own case 't'/case 's' labels above for why they moved out of
  // main.cpp's immediate-byte dispatcher).
  else if (strcasecmp(cmd, "low") == 0) startSpeakerLowTest();
  else if (strcasecmp(cmd, "mid") == 0) startSpeakerMidTest();
  else if (strcasecmp(cmd, "high") == 0) startSpeakerHighTest();
  else if (strcasecmp(cmd, "sweep") == 0) startSpeakerSweepTest();
  else if (strcasecmp(cmd, "melody") == 0) startSpeakerMelodyTest();
  else if (strcasecmp(cmd, "beep") == 0) startSpeakerBeepTest();
  else if (strcasecmp(cmd, "noise") == 0) startSpeakerNoiseTest();
  else if (strcasecmp(cmd, "loud") == 0) startSpeakerLoudTest();
  // Procedural music player -- see include/SpeakerTest.h. "stopmusic" is
  // checked as its own full word (not a "music" prefix) so it doesn't
  // collide with "music1".."music4".
  else if (strcasecmp(cmd, "music1") == 0) startSpeakerMusic1();
  else if (strcasecmp(cmd, "music2") == 0) startSpeakerMusic2();
  else if (strcasecmp(cmd, "music3") == 0) startSpeakerMusic3();
  else if (strcasecmp(cmd, "music4") == 0) startSpeakerMusic4();
  else if (strcasecmp(cmd, "stopmusic") == 0) stopSpeakerMusic();
  // Motor PWM calibration test -- see include/MotorPwmCalibration.h. Word
  // commands, Enter-terminated like every other multi-char command here;
  // 'm' itself stays the existing single-char mute toggle (see the len==1
  // switch above) since these are only checked once len>1.
  else if (strcasecmp(cmd, "mf") == 0) motorCalSelectDirection(MotorCalDirection::FORWARD);
  else if (strcasecmp(cmd, "mr") == 0) motorCalSelectDirection(MotorCalDirection::REVERSE);
  else if (strcasecmp(cmd, "mstop") == 0) {
    motorCalStop();
    cancelDanceEngine();  // 'mstop' is a universal "stop the motor now" -- also cancels DanceEngine and
    cancelMusicMotorController();  // MusicMotorController if either happened to own the motor (silent/idempotent
                                     // no-op if they didn't)
  }
  else if (strcasecmp(cmd, "mramp") == 0) motorCalStartRamp();
  else if (strcasecmp(cmd, "mcycle") == 0) motorCalStartCycle();
  else if (strcasecmp(cmd, "mkick") == 0) motorCalToggleKick();
  else if (strcasecmp(cmd, "mstatus") == 0) motorCalPrintStatus();
  // Dance Engine -- see include/DanceEngine.h. Superseded by
  // MusicMotorController; these commands only exist in a build with
  // ENABLE_LEGACY_DANCE_ENGINE set (see DanceEngine.h). Word commands,
  // Enter-terminated like every other multi-char command here.
#if ENABLE_LEGACY_DANCE_ENGINE
  else if (strcasecmp(cmd, "danceon") == 0) danceEngineEnable();
  else if (strcasecmp(cmd, "danceoff") == 0) danceEngineDisable();
  else if (strcasecmp(cmd, "dancestatus") == 0) danceEnginePrintStatus();
  else if (strcasecmp(cmd, "dancetestoff") == 0) danceEngineStopTest();
  else if (strcasecmp(cmd, "dancetest") == 0) danceEngineStartTest();
  else if (strcasecmp(cmd, "dancequiet") == 0) danceEngineSimQuiet();
  else if (strcasecmp(cmd, "dancemid") == 0) danceEngineSimMid();
  else if (strcasecmp(cmd, "dancehigh") == 0) danceEngineSimHigh();
  else if (strcasecmp(cmd, "dancepeak") == 0) danceEngineSimPeak();
#endif
  else if ((cmd[0] == 'm' || cmd[0] == 'M') && len > 1 && isdigit((unsigned char)cmd[1])) {
    bool allDigits = true;
    for (size_t i = 1; i < len; i++) {
      if (!isdigit((unsigned char)cmd[i])) { allDigits = false; break; }
    }
    long percent = allDigits ? strtol(cmd + 1, nullptr, 10) : -1;
    if (allDigits && percent >= 1 && percent <= 100) {
      motorCalManualSpeed((uint8_t)percent);
    } else {
      Serial.printf("[CMD] Motor speed must be 'm' + 1-100 (got '%s')\n", cmd);
    }
  }
  else Serial.printf("[CMD] Unknown command '%s' -- press 'h' for help\n", cmd);
}

// Fed one byte at a time by main.cpp's pollSerialDispatcher() -- the ONLY
// place in the whole program that calls Serial.read()/available() (see
// docs/DRV8833_MOTOR_BRINGUP.md, command-5 emergency-stop investigation).
// This function must never read Serial itself: an earlier version of this
// file independently drained Serial in its own while loop, racing
// main.cpp's motor/LED interceptor for the same byte stream -- proven (via
// temporary instrumentation) to occasionally steal 'k' before the
// interceptor ever saw it, since the two loops ran at different points in
// the same loop() iteration with an unbounded byte-arrival gap between
// them. Routing every byte through one owner removes that race entirely
// rather than shrinking its window. Implements the same Enter-terminated
// line buffering as before, just byte-at-a-time instead of self-driven.
void feedSerialByte(char c) {
  if (c == '\r') return;
  if (c == '\n') {
    lineBuf[lineLen] = '\0';
    if (lineLen > 0) dispatchCommand(lineBuf);
    lineLen = 0;
    return;
  }
  if (lineLen < sizeof(lineBuf) - 1) lineBuf[lineLen++] = c;
}

// Discards any partially-typed word-command line without dispatching it.
// Used by main.cpp's emergency-stop latch so a line interrupted mid-type
// (e.g. "stat" before 'k' arrives) can't silently combine with later
// input into an unintended command.
void clearPendingSerialLine() { lineLen = 0; }

// True while a word-command line is mid-type (bytes received, no '\n'
// yet). main.cpp's dispatcher uses this so a byte that would otherwise
// look like a reserved single-char motor command (e.g. the 'f' inside
// "effects") is routed here instead of intercepted, as long as it's really
// the continuation of an in-progress word -- see that dispatcher's
// comment for the full reasoning ('k' itself is the one exception: it
// stays reserved unconditionally, even mid-word).
bool isSerialLinePending() { return lineLen > 0; }

// ============================================================================
// Public API
// ============================================================================
void initControls() {
  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_MUTE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BRIGHTNESS_PIN, INPUT_PULLUP);
  pinMode(BUTTON4_PIN, INPUT_PULLUP);

  Serial.println(F("[BUTTON] GPIO10 using INPUT_PULLUP (Mode: press=next LED mode + next overlay mode, double-press=previous LED mode)"));
  Serial.println(F("[BUTTON] GPIO11 using INPUT_PULLUP (Mute: toggles LED output off/on)"));
  Serial.println(F("[BUTTON] GPIO17 using INPUT_PULLUP (Brightness: press=next level)"));
  Serial.println(F("[BUTTON] GPIO5 using INPUT_PULLUP (Button4: short press=toggle LED audio overlay ON/OFF, long press=toggle unified Audio Mode -- LED overlay + MusicMotorController)"));

  setBaseEffect(currentBaseEffect);
  resetAudioOverlayState(selectedOverlayMode, millis());
  Serial.printf("[AUDIO] Selected audio overlay: %s\n", AUDIO_OVERLAY_NAMES[(uint8_t)selectedOverlayMode]);
  Serial.printf("[AUDIO] Audio overlay enabled: %s\n", audioOverlayEnabled ? "YES" : "NO");
  Serial.printf("[BRIGHTNESS] %d%% | raw=%d\n", BRIGHTNESS_PERCENTS[brightnessIndex], BRIGHTNESS_RAW[brightnessIndex]);
  printHelp();
}

void updateControls(unsigned long now) {
  handleModeButton(now);
  handleMuteButton();
  handleBrightnessButton();
  handleButton4(now);
  // Serial is no longer polled here -- main.cpp's pollSerialDispatcher()
  // is the single owner of Serial.read()/available() and calls
  // feedSerialByte() for every byte it doesn't claim itself. See that
  // function's comment for why (a former independent poll here raced it).
}

BaseEffect getCurrentBaseEffect() { return currentBaseEffect; }
AudioOverlay getCurrentAudioOverlay() { return audioOverlayEnabled ? selectedOverlayMode : AudioOverlay::OFF; }
AudioOverlay getSelectedOverlayMode() { return selectedOverlayMode; }
bool isAudioOverlayEnabled() { return audioOverlayEnabled; }
bool isMuted() { return muted; }
uint8_t getBrightnessIndex() { return brightnessIndex; }
uint8_t getBrightnessRaw() { return BRIGHTNESS_RAW[brightnessIndex]; }
uint8_t getBrightnessPercent() { return BRIGHTNESS_PERCENTS[brightnessIndex]; }
