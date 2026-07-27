#pragma once

#include <Arduino.h>
#include "AudioOverlays.h"
#include "Config.h"
#include "LedEffects.h"

// Configures button GPIOs, sets initial base effect/overlay/brightness
// state, and prints the corresponding boot announcements. Call once from
// setup(), after LedEffects/AudioOverlays are ready to receive reset calls.
void initControls();

// Polls all four buttons (debounced, edge/hold/double-click detection) and
// any pending serial command bytes. Non-blocking -- call every loop()
// iteration regardless of frame pacing or mute state.
void updateControls(unsigned long now);

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
