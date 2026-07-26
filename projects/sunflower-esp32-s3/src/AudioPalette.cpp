#include "AudioPalette.h"

// Low/medium/high tier stops, concatenated into one 12-stop gradient so
// audioEnergyColor() sweeps smoothly across tier boundaries via the
// existing paletteLookup() helper instead of hard tier cuts.
static const RGB8 ENERGY_STOPS[12] = {
    // low: deep blue -> violet -> cyan -> soft green
    {20, 30, 140},
    {110, 40, 200},
    {30, 190, 220},
    {90, 200, 110},
    // medium: teal -> magenta -> gold -> orange
    {30, 170, 150},
    {210, 60, 180},
    {230, 180, 40},
    {230, 120, 30},
    // high: yellow -> hot pink -> red -> orange-red
    {240, 220, 40},
    {255, 60, 140},
    {230, 30, 30},
    {255, 90, 30},
};

static const RGB8 BASS_WARM_ACCENT = {200, 60, 20}; // warm ember, blended in proportionally to `bass`
const RGB8 AUDIO_ACCENT_WHITE = {255, 250, 240};

RGB8 audioEnergyColor(float energy, float bass, float transient) {
  energy = constrain(energy, 0.0f, 1.0f);
  RGB8 c = paletteLookup(ENERGY_STOPS, 12, (uint8_t)(energy * 255.0f));
  // Bass thickens/warms the color -- capped so it nudges the hue, never overwrites it.
  c = blendAudioColors(c, BASS_WARM_ACCENT, constrain(bass, 0.0f, 1.0f) * 0.35f);
  // Transient adds a brief, capped near-white accent -- never a full wash.
  c = blendAudioColors(c, AUDIO_ACCENT_WHITE, constrain(transient, 0.0f, 1.0f) * 0.25f);
  return c;
}
