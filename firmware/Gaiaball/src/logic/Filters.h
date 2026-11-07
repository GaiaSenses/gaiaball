#pragma once
#include <stdint.h>

namespace gaia {

// Exponential moving average. The first sample seeds the state (no ramp from zero).
template <typename T>
class Ema {
 public:
  void setAlpha(float a) { alpha_ = a; }
  T update(T x) {
    if (!seeded_) {
      y_ = x;
      seeded_ = true;
    } else {
      y_ = y_ + (T)(alpha_ * (float)(x - y_));
    }
    return y_;
  }

 private:
  float alpha_ = 0.2f;
  T y_{};
  bool seeded_ = false;
};

// NOTE: the median-of-5 spike filter lives in the site pipeline
// (use-sensor-smoothing.ts), never here — filtering twice would double the latency.

}  // namespace gaia
