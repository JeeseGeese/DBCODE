#pragma once

#include "LedEffects.h" // RGB8, paletteLookup(), lerpRGB(), scaleClamp()

// Reusable audio-reactive color helpers, shared by every overlay so
// selected colors shift with the audio state instead of each overlay
// using one fixed color. Built entirely on the project's existing RGB8
// type and paletteLookup()/lerpRGB()/scaleClamp() helpers (LedEffects.h)
// rather than a parallel uint32_t-packed-color system, per the existing
// project's color architecture.

// Smooth low->medium->high tiered color sweep (deep blue/violet/cyan/soft
// green -> teal/magenta/gold/orange -> yellow/hot pink/red), warmed by
// `bass` and given a brief, capped near-white accent at high `transient`.
// White is intentionally never a large fraction of the result -- it's an
// accent, not a fill color, so loud audio doesn't wash the strip white.
RGB8 audioEnergyColor(float energy, float bass, float transient);

// Thin, explicitly-named wrappers around the existing RGB8 helpers, so
// overlay code reads as "audio-driven color blending" without duplicating
// lerpRGB()/scaleClamp()'s logic.
inline RGB8 blendAudioColors(RGB8 a, RGB8 b, float amount) { return lerpRGB(a, b, amount); }
inline RGB8 scaledColor(RGB8 c, float brightness) {
  scaleClamp(c, brightness);
  return c;
}

// Reserved accent color for transient/clap highlights, spark tips, and
// lightning cores -- used sparingly (small blend amounts / small LED
// regions), never as a sustained fill color across the strip.
extern const RGB8 AUDIO_ACCENT_WHITE;
