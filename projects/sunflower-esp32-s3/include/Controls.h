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

// Unified "Audio Mode" -- the one user-facing state Button4's long-hold
// gesture (and the 'audiomode on/off' serial command) controls. ON means
// BOTH the LED audio-reactive overlay AND MusicMotorController are active
// together; OFF means both are inactive and the motor is safely stopped.
// DanceEngine is never part of this state (see DanceEngine.h -- disabled
// by default, superseded by MusicMotorController).
//
// isUserAudioModeEnabled() is a derived read, not a separately-stored
// flag -- it can never drift from the two real states it reports on
// (isAudioOverlayEnabled() && isMusicMotorControllerActive()). It returns
// false for every "half enabled" state (e.g. MusicMotorController started
// standalone via 'musicmotor on' with the overlay still off) -- callers
// that need to distinguish a partial state from full Audio Mode should
// check isAudioOverlayEnabled()/isMusicMotorControllerActive() directly
// (see 'status' output).
bool isUserAudioModeEnabled();

// setUserAudioModeEnabled(true): enables the LED overlay and
// MusicMotorController together, atomically from the caller's point of
// view -- if motor ownership can't be secured, neither half is left
// enabled (see Controls.cpp for the exact rejection path and
// AUDIO_MODE_BLOCKED cue). No-op/success if Audio Mode is already fully
// ON. Idempotent-safe to call even if MusicMotorController was already
// running standalone (completes the LED half rather than refusing).
//
// setUserAudioModeEnabled(false): always succeeds -- disables the overlay,
// safely stops/resets MusicMotorController (musicMotorDisable()), and
// leaves DanceEngine untouched (already disabled by default). Matches the
// emergency-stop philosophy that disabling/stopping is never refused.
//
// Returns true if Audio Mode ends the call ON (enable) or OFF (disable);
// false only for a rejected enable request.
bool setUserAudioModeEnabled(bool enabled);

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
