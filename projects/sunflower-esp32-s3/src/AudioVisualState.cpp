#include "AudioVisualState.h"
#include "Config.h"

static AudioVisualState state{};
static unsigned long lastUpdateMs = 0;
static bool haveLastUpdate = false;

void updateAudioVisualState(const AudioFeatures &f, unsigned long now) {
  float dtSeconds = 0.0f;
  if (haveLastUpdate) dtSeconds = (now - lastUpdateMs) / 1000.0f;
  lastUpdateMs = now;
  haveLastUpdate = true;

  state.level = f.envelope;
  state.envelope = f.envelope;
  state.bass = f.lowFrequencyEnergy;
  state.transientStrength = f.transientStrength;
  state.clap = f.clap;

  // Decaying "spike": jumps to 1.0 on a transient/clap edge, otherwise
  // decays linearly -- gives overlays a continuous 0..1 signal instead of
  // a single-frame boolean blip.
  if (f.transient || f.clap) {
    state.transient = 1.0f;
  } else {
    state.transient = max(0.0f, state.transient - AUDIO_VISUAL_TRANSIENT_DECAY_PER_SEC * dtSeconds);
  }

  state.lowRange = state.bass;
  state.highRange = state.transient;
  state.midRange = constrain(
      state.level - AUDIO_VISUAL_MID_BASS_WEIGHT * state.lowRange - AUDIO_VISUAL_MID_HIGH_WEIGHT * state.highRange,
      0.0f, 1.0f);

  state.energy8 = (uint8_t)constrain(state.level * 255.0f, 0.0f, 255.0f);
  state.bass8 = (uint8_t)constrain(state.bass * 255.0f, 0.0f, 255.0f);
  state.transient8 = (uint8_t)constrain(state.transient * 255.0f, 0.0f, 255.0f);
}

const AudioVisualState &getAudioVisualState() { return state; }
