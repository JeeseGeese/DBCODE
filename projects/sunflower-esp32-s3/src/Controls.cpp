#include "Controls.h"
#include "AudioAnalyzer.h"
#include "AudioVisualState.h"
#include "AutoShowcase.h"
#include "ExpressiveMotion.h"
#include "VisualCue.h"
#include <ctype.h>
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

// Button4 is now a plain immediate toggle: no click-count/hold state
// machine at all (an earlier single/double-click gesture architecture was
// removed here -- Button4 is a dedicated ON/OFF button, and the LED Mode
// button now handles overlay *mode* cycling instead).

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

// Trigger point for the visual cue and the ON/OFF toggle itself --
// Button4's press edge and serial 'x'. Never changes which overlay mode
// is selected, only whether it's currently active. The cue is armed and
// the confirmation printed unconditionally, even while muted; only the
// actual LED rendering is suppressed while muted, in main.cpp.
static void toggleOverlayOffOn() {
  unsigned long now = millis();
  audioOverlayEnabled = !audioOverlayEnabled;
  if (audioOverlayEnabled) {
    resetAudioOverlayState(selectedOverlayMode, now);
    Serial.println(F("[AUDIO] Overlay: ON"));
    startVisualCue(VisualCueType::OVERLAY_ENABLED, now);
    Serial.println(F("[CUE] Audio overlay enabled (green flash)"));
  } else {
    Serial.println(F("[AUDIO] Overlay: OFF"));
    startVisualCue(VisualCueType::OVERLAY_DISABLED, now);
    Serial.println(F("[CUE] Audio overlay disabled (double red flash)"));
  }
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

// Button4 is a dedicated, immediate audio-overlay ON/OFF button: no
// click-count, double-click, or hold detection of any kind. The toggle
// fires on the debounced PRESS edge itself, so a sustained hold produces
// exactly one action (the edge only fires once, on HIGH->LOW) and release
// does nothing beyond ordinary debounce bookkeeping.
static void handleButton4(unsigned long now) {
  // Read raw GPIO.
  int reading = digitalRead(button4.pin);
  if (reading != button4.lastRawReading) {
    if (button4DebugEnabled) {
      Serial.printf("[BUTTON4 RAW] %s -> %s\n", levelName(button4.lastRawReading), levelName(reading));
    }
    button4.lastDebounceTime = now;
  }

  // Update debounce state; capture the press edge.
  bool pressedEdge = false;
  if ((now - button4.lastDebounceTime) > BUTTON4_DEBOUNCE_MS) {
    if (reading != button4.stableState) {
      button4.stableState = reading;
      if (button4.stableState == LOW) {
        pressedEdge = true;
        if (button4DebugEnabled) Serial.println(F("[BUTTON4 DEBOUNCED] PRESSED"));
      } else {
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

  if (!pressedEdge) return;

  Serial.println(F("[BUTTON4] Audio overlay toggle"));
  toggleOverlayOffOn();
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
  Serial.println(F("  g = [DIAGNOSTIC ONLY] force the enabled/green cue, no overlay state change"));
  Serial.println(F("  r = [DIAGNOSTIC ONLY] force the disabled/red cue, no overlay state change"));
  Serial.println(F("  b = [DIAGNOSTIC ONLY] toggle raw/debounced Button4 transition trace"));
  Serial.println(F("  a = toggle AUTO_SHOWCASE (jump to it, or back to the last normal effect)"));
  Serial.println(F("  c = force AUTO_SHOWCASE to its next effect now (no-op if not active)"));
  Serial.println(F("  v = print current audio visual-control state (level/bass/transient/derived bands, pool counts)"));
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
  Serial.printf("  Current audio energy=%.3f Current bass control=%.3f Current transient control=%.3f\n", v.level,
                v.bass, v.highRange);
  Serial.printf("  Active ripple count=%u Active particle/comet count=%u\n", getActiveRippleCount(),
                (uint16_t)getActiveSparkCount() + getActiveCometCount());
  Serial.printf("  Button 4 pin: GPIO%d\n", BUTTON4_PIN);
  Serial.printf("  Button 4 raw state: %s\n", levelName(digitalRead(BUTTON4_PIN)));
  Serial.printf("  Button 4 debounced state: %s\n", button4.stableState == LOW ? "PRESSED" : "RELEASED");
  Serial.printf("  Button 4 debounce interval: %ums\n", (unsigned)BUTTON4_DEBOUNCE_MS);
}

// "motion" word command -- see include/ExpressiveMotion.h. `args` is
// whatever follows "motion" in the line (leading spaces stripped below),
// e.g. "" or "next" for a bare "motion", or "off"/"idle"/"audio"/"status"/
// "demo" for "motion off" etc. Routed here from dispatchCommand() below,
// which is itself fed one byte at a time by main.cpp's central serial
// dispatcher via feedSerialByte() -- no new Serial reader is introduced.
static void dispatchMotionCommand(const char *args) {
  while (*args == ' ') args++;
  if (*args == '\0' || strcasecmp(args, "next") == 0) {
    cycleExpressiveMotionMode();
  } else if (strcasecmp(args, "off") == 0) {
    setExpressiveMotionMode(ExpressiveMotionMode::OFF);
  } else if (strcasecmp(args, "idle") == 0) {
    setExpressiveMotionMode(ExpressiveMotionMode::IDLE_ALIVE);
  } else if (strcasecmp(args, "audio") == 0) {
    setExpressiveMotionMode(ExpressiveMotionMode::AUDIO_REACTIVE);
  } else if (strcasecmp(args, "status") == 0) {
    printExpressiveMotionDebugState();
  } else if (strcasecmp(args, "demo") == 0) {
    startMotionDemo();
  } else {
    Serial.printf("[CMD] Unknown 'motion' subcommand '%s' -- try: motion [next|off|idle|audio|status|demo]\n", args);
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
      default: break;
    }
    Serial.printf("[CMD] Unknown command '%c' -- press 'h' for help\n", cmd[0]);
    return;
  }

  if (strcasecmp(cmd, "effects") == 0) printEffectsList();
  else if (strcasecmp(cmd, "overlays") == 0) printOverlaysList();
  else if (strcasecmp(cmd, "status") == 0) printStatus();
  else if (strncasecmp(cmd, "motion", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ')) dispatchMotionCommand(cmd + 6);
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
  Serial.println(F("[BUTTON] GPIO5 using INPUT_PULLUP (Button4: press=toggle audio overlay ON/OFF)"));

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
