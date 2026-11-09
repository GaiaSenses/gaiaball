#include "GyroBias.h"

#include <math.h>

namespace gaia {
namespace {
// Time constant of the low-pass used by the rest detector. Short enough to see a
// shake, long enough that sensor noise alone does not look like motion.
constexpr float kRateLpTauS = 0.1f;

float alphaFor(float tauS, float dt) {
  if (tauS <= 0.0f) return 1.0f;
  const float a = dt / (tauS + dt);
  return a > 1.0f ? 1.0f : a;
}
}  // namespace

void GyroBiasTracker::configure(const GyroBiasSettings &settings, float sampleRateHz) {
  s_ = settings;
  const float dt = (sampleRateHz > 0.0f) ? 1.0f / sampleRateHz : 0.02f;
  alphaDev_ = alphaFor(kRateLpTauS, dt);
  alphaBias_ = alphaFor(s_.tauS, dt);
}

void GyroBiasTracker::reset() {
  bias_[0] = bias_[1] = bias_[2] = 0.0f;
  seeded_ = false;
  atRest_ = false;
  adapting_ = false;
}

void GyroBiasTracker::correct(float gDps[3], const float accelG[3], uint32_t nowMs) {
  const float raw[3] = {gDps[0], gDps[1], gDps[2]};

  float accelMag = 1.0f;
  if (accelG != nullptr) {
    accelMag = sqrtf(accelG[0] * accelG[0] + accelG[1] * accelG[1] + accelG[2] * accelG[2]);
    if (!isfinite(accelMag)) accelMag = 1.0f;
  }

  if (!seeded_) {
    for (int i = 0; i < 3; i++) lpRate_[i] = raw[i];
    lpAccelMag_ = accelMag;
    seeded_ = true;
    restSinceMs_ = nowMs;
  }

  // Rest test 1: how much the rate wobbles around its own mean (offset-independent).
  bool still = true;
  for (int i = 0; i < 3; i++) {
    if (fabsf(raw[i] - lpRate_[i]) >= s_.restThresholdDps) still = false;
    lpRate_[i] += alphaDev_ * (raw[i] - lpRate_[i]);
  }
  // Rest test 2: the mean rate must be small enough to be an offset at all.
  for (int i = 0; i < 3; i++)
    if (fabsf(lpRate_[i]) >= s_.maxBiasDps) still = false;
  // Rest test 3: the ball must be hanging on gravity alone, not being moved.
  if (accelG != nullptr) {
    if (fabsf(accelMag - 1.0f) >= s_.accelTolG) still = false;
    if (fabsf(accelMag - lpAccelMag_) >= s_.accelDevTolG) still = false;
    lpAccelMag_ += alphaDev_ * (accelMag - lpAccelMag_);
  }

  if (still) {
    if (!atRest_) {
      atRest_ = true;
      restSinceMs_ = nowMs;
    }
    adapting_ = (uint32_t)(nowMs - restSinceMs_) >= s_.restHoldMs;
    if (adapting_) {
      for (int i = 0; i < 3; i++) {
        bias_[i] += alphaBias_ * (raw[i] - bias_[i]);
        if (bias_[i] > s_.maxBiasDps) bias_[i] = s_.maxBiasDps;
        if (bias_[i] < -s_.maxBiasDps) bias_[i] = -s_.maxBiasDps;
      }
    }
  } else {
    atRest_ = false;
    adapting_ = false;
  }

  for (int i = 0; i < 3; i++) gDps[i] = raw[i] - bias_[i];
}

}  // namespace gaia
