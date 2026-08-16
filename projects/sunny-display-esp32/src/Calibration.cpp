#include "Calibration.h"

#include <algorithm>
#include <math.h>

void calibrationTargetScreenPoint(CalibrationTargetId target, int32_t screenWidth, int32_t screenHeight,
                                   int32_t *outX, int32_t *outY) {
  int32_t inset = CALIBRATION_TARGET_INSET_PX;
  switch (target) {
    case CalibrationTargetId::TOP_LEFT:
      *outX = inset;
      *outY = inset;
      break;
    case CalibrationTargetId::TOP_RIGHT:
      *outX = screenWidth - 1 - inset;
      *outY = inset;
      break;
    case CalibrationTargetId::BOTTOM_RIGHT:
      *outX = screenWidth - 1 - inset;
      *outY = screenHeight - 1 - inset;
      break;
    case CalibrationTargetId::BOTTOM_LEFT:
      *outX = inset;
      *outY = screenHeight - 1 - inset;
      break;
    case CalibrationTargetId::CENTER:
      *outX = screenWidth / 2;
      *outY = screenHeight / 2;
      break;
  }
}

const char *calibrationTargetName(CalibrationTargetId target) {
  switch (target) {
    case CalibrationTargetId::TOP_LEFT:
      return "TOP-LEFT";
    case CalibrationTargetId::TOP_RIGHT:
      return "TOP-RIGHT";
    case CalibrationTargetId::BOTTOM_RIGHT:
      return "BOTTOM-RIGHT";
    case CalibrationTargetId::BOTTOM_LEFT:
      return "BOTTOM-LEFT";
    case CalibrationTargetId::CENTER:
      return "CENTER";
  }
  return "UNKNOWN";
}

int32_t calibrationTrimmedMean(const int32_t *samples, int count, float trimFraction) {
  if (count <= 0) return 0;
  constexpr int MAX_SAMPLES = 64;
  int n = count > MAX_SAMPLES ? MAX_SAMPLES : count;

  int32_t sorted[MAX_SAMPLES];
  for (int i = 0; i < n; i++) sorted[i] = samples[i];
  std::sort(sorted, sorted + n);

  int trim = (int)(n * trimFraction);
  int lo = trim;
  int hi = n - trim;
  if (hi <= lo) {  // trimming would leave nothing -- fall back to the plain mean
    lo = 0;
    hi = n;
  }

  int64_t sum = 0;
  int cnt = 0;
  for (int i = lo; i < hi; i++) {
    sum += sorted[i];
    cnt++;
  }
  if (cnt == 0) return 0;
  return (int32_t)(sum / cnt);
}

namespace {
float meanOfInt(const int32_t *v, int n) {
  int64_t sum = 0;
  for (int i = 0; i < n; i++) sum += v[i];
  return n > 0 ? (float)sum / (float)n : 0.0f;
}

// |Pearson correlation| between two equal-length integer series. Used
// only to decide WHICH raw channel drives which screen axis -- not for
// the fit itself.
float absCorrelation(const int32_t *a, const int32_t *b, int n) {
  if (n < 2) return 0.0f;
  float ma = meanOfInt(a, n), mb = meanOfInt(b, n);
  double num = 0, sumSqA = 0, sumSqB = 0;
  for (int i = 0; i < n; i++) {
    double da = a[i] - ma, db = b[i] - mb;
    num += da * db;
    sumSqA += da * da;
    sumSqB += db * db;
  }
  double denom = sqrt(sumSqA) * sqrt(sumSqB);
  if (denom == 0) return 0.0f;
  float c = (float)(num / denom);
  return c < 0 ? -c : c;
}

// 1D least-squares scale+offset fit: target[i] ~= scale*channel[i] + offset.
void linearFit(const int32_t *channel, const int32_t *target, int n, float *outScale, float *outOffset) {
  float meanC = meanOfInt(channel, n), meanT = meanOfInt(target, n);
  double num = 0, den = 0;
  for (int i = 0; i < n; i++) {
    double dc = channel[i] - meanC;
    num += dc * (target[i] - meanT);
    den += dc * dc;
  }
  float scale = den != 0 ? (float)(num / den) : 0.0f;
  float offset = meanT - scale * meanC;
  *outScale = scale;
  *outOffset = offset;
}
}  // namespace

DerivedCalibration deriveCalibrationFromSamples(const CalibrationRawSample *rawSamples, const int32_t *targetScreenX,
                                                 const int32_t *targetScreenY, int count) {
  constexpr int MAX_SAMPLES = 16;
  int n = count > MAX_SAMPLES ? MAX_SAMPLES : count;

  int32_t rawXArr[MAX_SAMPLES];
  int32_t rawYArr[MAX_SAMPLES];
  for (int i = 0; i < n; i++) {
    rawXArr[i] = rawSamples[i].rawX;
    rawYArr[i] = rawSamples[i].rawY;
  }

  // Which raw channel actually drives screen X? Compare how strongly the
  // controller's own rawX channel correlates with screenX vs. screenY --
  // if it correlates far better with screenY, the axes are swapped.
  float corrRawXvsScreenX = absCorrelation(rawXArr, targetScreenX, n);
  float corrRawXvsScreenY = absCorrelation(rawXArr, targetScreenY, n);

  DerivedCalibration result{};
  result.swapAxes = corrRawXvsScreenY > corrRawXvsScreenX;

  const int32_t *xChannel = result.swapAxes ? rawYArr : rawXArr;
  const int32_t *yChannel = result.swapAxes ? rawXArr : rawYArr;

  linearFit(xChannel, targetScreenX, n, &result.scaleX, &result.offsetX);
  linearFit(yChannel, targetScreenY, n, &result.scaleY, &result.offsetY);

  return result;
}
