#pragma once
#include <stdint.h>

namespace gaia {

struct GyroBiasSettings {
  // Rest is decided on how much the rate DEVIATES from its own low-passed value, not
  // on the rate itself: that test is independent of the unknown offset we are trying
  // to measure (see the class comment).
  float restThresholdDps = 2.0f;
  // Plausibility bound for a zero-rate offset, and the clip applied to the estimate.
  // The MPU-9250 datasheet allows +-5 dps of initial offset, so 10 dps leaves margin
  // while still refusing to mistake a slow steady rotation for a bias.
  float maxBiasDps = 10.0f;
  float tauS = 4.0f;           // EMA time constant of the estimate while at rest
  uint16_t restHoldMs = 1000;  // stillness required before the estimate adapts
  float accelTolG = 0.15f;     // |a| must stay within 1 g +- this to count as rest
  float accelDevTolG = 0.05f;  // and must not be fluctuating more than this
};

// Gyroscope zero-rate offset tracker.
//
// WHY: the offset is what turns into orientation drift — integrated, 1 dps is 60 deg
// per minute, and in 6-axis mode the heading has no absolute reference to pull it back
// ("the globe slowly spinning on its own"). The MPU-9250's offset is specified at
// +-5 dps and moves with temperature, so it has to be measured at run time.
//
// WHY NOT the previous design: it declared the ball still when the ALREADY CORRECTED
// rate was under 2 dps. With a fresh estimate of zero and a real offset above that
// threshold, the ball never looked still, so the tracker never adapted and the offset
// was never removed — the worse the unit, the less the correction worked. x-io Fusion's
// bias module has the same shape (3 dps on the corrected rate) and the same blind spot.
//
// HOW: rest is detected from the DEVIATION of the raw rate around its own low-passed
// value (offset-independent, as VQF does), gated by a plausibility bound on that
// low-passed value and, when available, by the accelerometer magnitude. Shaking and
// vibration show up as deviation; a real rotation exceeds the plausibility bound; a
// large constant offset passes both and is finally learned.
//
// LIMIT, stated on purpose: a perfectly steady rotation slower than `maxBiasDps` about
// the gravity axis is indistinguishable from an offset. That is what the bound is for —
// the worst case is bounded by it, and the estimate is clipped to it.
class GyroBiasTracker {
 public:
  void configure(const GyroBiasSettings &settings, float sampleRateHz);
  void reset();

  // Subtracts the current estimate from `gDps` IN PLACE and, when the ball is at rest,
  // refines the estimate. `accelG` may be nullptr to skip the accelerometer gate.
  // `nowMs` is a monotonic millisecond clock (wrap-safe).
  void correct(float gDps[3], const float accelG[3], uint32_t nowMs);

  const float *bias() const { return bias_; }
  bool atRest() const { return atRest_; }
  bool adapting() const { return adapting_; }

 private:
  GyroBiasSettings s_{};
  float alphaDev_ = 0.2f;   // low-pass of the rate, for the deviation test
  float alphaBias_ = 0.0f;  // EMA of the estimate while at rest
  float bias_[3] = {0, 0, 0};
  float lpRate_[3] = {0, 0, 0};
  float lpAccelMag_ = 1.0f;
  bool seeded_ = false;
  bool atRest_ = false;
  bool adapting_ = false;
  uint32_t restSinceMs_ = 0;
};

}  // namespace gaia
