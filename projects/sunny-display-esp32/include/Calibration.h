#pragma once
#include <stdint.h>

// Pure, host-testable touch-calibration math: calibration target
// geometry, raw-sample noise filtering, and fitting the raw-to-screen
// transform from measured (raw, known-target-screen-position) sample
// pairs. No LVGL/hardware here -- see CalibrationManager.h for the
// stateful screen-building/touch-sampling driver that uses these.
// Mirrors TouchManager.h's own pure/stateful split.
//
// V1.2.2 BUG FIX (2026-08-09): the original model treated each inset
// calibration target's own averaged raw reading as if it were measured
// at the true screen EDGE (raw min -> screen 0, raw max -> screen
// width-1), because targets are drawn inset ~30px from the edge for
// tappability. That produced a systematic ~30-42px error at every
// corner (physically confirmed) while leaving the center accurate by
// coincidence (the center target's true position IS the middle of the
// raw range, so the edge-assumption error cancels out there). The fix
// below fits scale+offset directly against each target's ACTUAL known
// screen position instead of assuming raw-at-target equals raw-at-edge.
// See test_host/final_touch_calibration.cpp, which reproduces both the
// old bug (large corner error) and the fix (sub-2px error) from the
// same measured dataset.
//
// See test_host/calibration_math.cpp for coverage.

enum class CalibrationTargetId : uint8_t {
  TOP_LEFT = 0,
  TOP_RIGHT = 1,
  BOTTOM_RIGHT = 2,
  BOTTOM_LEFT = 3,
  CENTER = 4,
};

constexpr int CALIBRATION_TARGET_COUNT = 5;

// Inset from the physical screen edges, in pixels -- exact corner pixels
// are hard to tap precisely on a resistive panel, so targets are placed
// slightly inward (a standard touchscreen-calibration practice, not an
// invented calibration value itself).
constexpr int32_t CALIBRATION_TARGET_INSET_PX = 30;

// Known screen-pixel location of a calibration target for the given
// logical screen size (post-rotation).
void calibrationTargetScreenPoint(CalibrationTargetId target, int32_t screenWidth, int32_t screenHeight,
                                   int32_t *outX, int32_t *outY);

const char *calibrationTargetName(CalibrationTargetId target);

// Trimmed-mean filter: sorts samples, discards the lowest/highest
// trimFraction of samples (rejecting outlier noise spikes), averages
// what's left. Falls back to a plain mean of all samples if trimming
// would leave nothing. count must be >= 1; samples beyond the first 64
// are ignored (fixed-size stack buffer, no dynamic allocation).
int32_t calibrationTrimmedMean(const int32_t *samples, int count, float trimFraction);

struct CalibrationRawSample {
  int32_t rawX;
  int32_t rawY;
};

// screenX = scaleX * xChannel + offsetX, where xChannel is rawY if
// swapAxes else rawX (and symmetrically for screenY / yChannel) -- a
// simple per-axis linear (scale + offset) fit, not a full 2D affine.
// Model comparison on the real V1.2.2 dataset (5 measured points) showed
// a full affine fit's cross-axis coefficients were statistically
// negligible (~0.0001, two orders of magnitude below the primary
// coefficients) -- this decoupled model reproduces the same <1px mean
// error with fewer, simpler parameters. See DISPLAY_HARDWARE.md's
// "Touch calibration procedure" for the full model-comparison record.
struct DerivedCalibration {
  bool swapAxes;
  float scaleX;
  float offsetX;
  float scaleY;
  float offsetY;
};

// Fits the linear scale+offset transform above from measured sample
// pairs: rawSamples[i] paired with the KNOWN true screen position
// (targetScreenX[i], targetScreenY[i]) that sample was captured at --
// e.g. the calibration screen's 5 targets (4 corners + center), not just
// the corners. Least-squares over all `count` samples (count >= 2;
// the real calibration flow provides 5). Which raw channel drives which
// screen axis (swapAxes) is determined by comparing |Pearson
// correlation| of each raw channel against each screen axis -- robust
// to sample count/order, unlike a fixed corner-index heuristic.
// Deterministic from the input samples -- no invented/guessed/hand-tuned
// constants.
DerivedCalibration deriveCalibrationFromSamples(const CalibrationRawSample *rawSamples, const int32_t *targetScreenX,
                                                 const int32_t *targetScreenY, int count);
