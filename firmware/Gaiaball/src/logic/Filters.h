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

// Sliding-window median of the last 5 samples (fewer during warm-up). Fixed storage.
class Median5 {
 public:
  float update(float x) {
    buf_[head_] = x;
    head_ = (head_ + 1) % 5;
    if (count_ < 5) count_++;
    float s[5];
    for (uint8_t i = 0; i < count_; i++) s[i] = buf_[i];
    for (uint8_t i = 1; i < count_; i++) {  // insertion sort
      float v = s[i];
      int8_t j = (int8_t)i - 1;
      while (j >= 0 && s[j] > v) {
        s[j + 1] = s[j];
        j--;
      }
      s[j + 1] = v;
    }
    return s[count_ / 2];
  }

 private:
  float buf_[5] = {0, 0, 0, 0, 0};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
};

}  // namespace gaia
