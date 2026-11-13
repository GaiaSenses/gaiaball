#pragma once
#include <math.h>
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


// Hampel spike filter over a 5-sample TRAILING window.
//
// WHY: a dropped or corrupted I2C read shows up as one wild sample. An EMA does not
// remove it — it smears it over the next samples; a plain median removes it but also
// rounds off genuine fast motion.
//
// HOW: the newest sample is compared with the median of the last five. If it deviates
// by more than k * 1.4826 * MAD (the MAD-based robust estimate of sigma), it is replaced
// by that median; otherwise it passes through untouched. k = 0 disables the filter.
//
// The window always stores the RAW sample, never the corrected one, so a real step (a
// kick, a new resting face) becomes the new normal within three samples instead of
// being suppressed forever.
//
// Trailing, not centred: it judges the newest sample, so it adds ZERO latency. A centred
// window would be slightly better at rejecting spikes at the cost of two samples of
// delay — not a trade worth making in a real-time control signal.
template <typename T>
class Hampel5 {
 public:
  static constexpr int kN = 5;

  void setK(float k) { k_ = k; }

  T update(T x) {
    if (k_ <= 0.0f) return x;
    win_[head_] = x;
    head_ = static_cast<uint8_t>((head_ + 1) % kN);
    if (count_ < kN) {
      count_++;
      return x;  // not enough history to judge anything yet
    }
    float v[kN];
    for (int i = 0; i < kN; i++) v[i] = static_cast<float>(win_[i]);
    const float med = median5(v);
    for (int i = 0; i < kN; i++) v[i] = fabsf(v[i] - med);
    const float sigma = 1.4826f * median5(v);
    if (fabsf(static_cast<float>(x) - med) > k_ * sigma) return static_cast<T>(med);
    return x;
  }

 private:
  // Sorting network for 5 elements: 9 compares, no branching on data size.
  static float median5(float a[kN]) {
    static const uint8_t pairs[9][2] = {{0, 1}, {3, 4}, {2, 4}, {2, 3}, {1, 4},
                                        {0, 3}, {0, 2}, {1, 3}, {1, 2}};
    for (const auto &p : pairs) {
      if (a[p[0]] > a[p[1]]) {
        const float t = a[p[0]];
        a[p[0]] = a[p[1]];
        a[p[1]] = t;
      }
    }
    return a[2];
  }

  float k_ = 0.0f;
  T win_[kN]{};
  uint8_t count_ = 0;
  uint8_t head_ = 0;
};

// NOTE: the site pipeline (use-sensor-smoothing.ts) ALSO runs a median-of-5 plus an EMA
// of its own. Keep the on-device filters light: filtering the same signal twice doubles
// the latency for no extra noise rejection.
// NOTE: the legacy comment below is kept because it still describes the division of
// labour between device and site:
// the median-of-5 spike filter lives in the site pipeline
// (use-sensor-smoothing.ts), never here — filtering twice would double the latency.

}  // namespace gaia
