#include "QuatSmoother.h"

#include <math.h>

namespace gaia {
namespace {
// Cutoff of the low-pass applied to the speed estimate itself, as in the reference
// 1-euro implementation: without it the noisy derivative would modulate the cutoff.
constexpr float kSpeedCutoffHz = 1.0f;
constexpr float kTwoPi = 6.283185307179586f;

// One-pole coefficient for a cutoff frequency and a sample interval.
float alphaFor(float cutoffHz, float dt) {
  if (cutoffHz <= 0.0f) return 0.0f;
  const float tau = 1.0f / (kTwoPi * cutoffHz);
  return dt / (tau + dt);
}
}  // namespace

void QuatSmoother::configure(QuatSmoothMode mode, uint8_t slerpAmount, float fcMinHz,
                             float betaHzPerDps) {
  mode_ = mode;
  // Legacy mapping, kept bit-exact: amount 0 = off, 100 = t 0.1 (heaviest).
  const uint8_t amount = slerpAmount > 100 ? 100 : slerpAmount;
  slerpT_ = 1.0f - (float)amount / 100.0f * 0.9f;
  fcMinHz_ = fcMinHz > 0.0f ? fcMinHz : 1.0f;
  beta_ = betaHzPerDps >= 0.0f ? betaHzPerDps : 0.0f;
}

void QuatSmoother::reset() {
  seeded_ = false;
  speedSeeded_ = false;
  speedLp_ = 0.0f;
}

Quat QuatSmoother::update(const Quat &q, float dt) {
  if (mode_ == QuatSmoothMode::Off) {
    prev_ = q;
    seeded_ = true;
    return q;
  }
  if (!seeded_) {
    prev_ = q;
    seeded_ = true;
    return q;
  }
  if (!isfinite(dt) || dt <= 0.0f) return prev_;  // no time passed: nothing to filter

  float t;
  if (mode_ == QuatSmoothMode::SlerpFixed) {
    t = slerpT_;
  } else {
    const float speed = quatAngleDeg(prev_, q) / dt;
    const float aSpeed = alphaFor(kSpeedCutoffHz, dt);
    if (!speedSeeded_) {
      speedLp_ = speed;
      speedSeeded_ = true;
    } else {
      speedLp_ += aSpeed * (speed - speedLp_);
    }
    t = alphaFor(fcMinHz_ + beta_ * fabsf(speedLp_), dt);
  }
  prev_ = quatSlerp(prev_, q, t);
  return prev_;
}

}  // namespace gaia
