#pragma once

#include <Arduino.h>
#include "AudioOverlays.h"
#include "Config.h"
#include "LedEffects.h"

// Configures button GPIOs, sets initial base effect/overlay/brightness
// state, and prints the corresponding boot announcements. Call once from
// setup(), after LedEffects/AudioOverlays are ready to receive reset calls.
void initControls();

// Polls all four buttons (debounced, edge/hold/double-click detection).
// Non-blocking -- call every loop() iteration regardless of frame pacing
// or mute state. Does NOT read Serial -- see feedSerialByte() below.
void updateControls(unsigned long now);

// Feeds one incoming serial byte to Controls' Enter-terminated
// line-command parser (n/p/o/x/+/-/m/d/h/g/r/b/a/c/v and the word
// commands effects/overlays/status). Called by main.cpp's
// pollSerialDispatcher() for every byte not claimed by the motor/LED
// interceptor -- that dispatcher is the ONLY place in the program that
// reads Serial directly; this function must never call
// Serial.read()/available() itself (see pollSerialDispatcher()'s comment
// in main.cpp for why a former independent reader here was unsafe).
void feedSerialByte(char c);

// Discards any partially-typed word-command line without dispatching it.
// Used by main.cpp's emergency-stop handling so an interrupted line can't
// combine with later input into an unintended command.
void clearPendingSerialLine();

// True while a word-command line is mid-type (bytes received, no '\n'
// yet). See main.cpp's pollSerialDispatcher() for how this is used to
// avoid misinterpreting a byte inside a word (e.g. "effects") as a
// reserved single-char motor command.
bool isSerialLinePending();

BaseEffect getCurrentBaseEffect();

// getCurrentAudioOverlay() returns the EFFECTIVE overlay for rendering:
// selectedOverlayMode when enabled, AudioOverlay::OFF when disabled. The
// selected mode and the enabled flag are independent -- see
// getSelectedOverlayMode() / isAudioOverlayEnabled() below to read them
// separately (e.g. the mode stays RIPPLE even while disabled).
AudioOverlay getCurrentAudioOverlay();
AudioOverlay getSelectedOverlayMode();
bool isAudioOverlayEnabled();
bool isMuted();

// Exported setter for MotorPowerGuard (see include/MotorPowerGuard.h) --
// saves/forces/restores mute state around motor engagement. Idempotent:
// no-op if already at the requested value. Does not touch brightness,
// base effect, or overlay selection.
void setMuted(bool value);
uint8_t getBrightnessIndex();
uint8_t getBrightnessRaw();
uint8_t getBrightnessPercent();

// Full system status dump ('status' serial command).
void printStatus();
