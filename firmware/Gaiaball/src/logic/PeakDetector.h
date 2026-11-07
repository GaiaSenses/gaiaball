#pragma once
#include <stdint.h>

namespace gaia {

// Adaptive transient detector on a stream of block RMS values.
//  fast: tracks the signal,   fast += fastAlpha * (rms - fast)          (alpha = weight of new sample)
//  slow: noise baseline,      slow = slowAlpha * slow + (1-slowAlpha) * rms   (alpha near 1 = slow)
// Both are seeded by the first update. Fires when fast > slow + threshold and the
// debounce elapsed (wrap-safe). After a fire it stays disarmed until fast falls below
// slow + 0.7 * threshold, so one sustained sound yields one event.
// Not thread-safe: use from a single task (reconfigure from that same task).
class PeakDetector {
 public:
  void configure(float fastAlpha, float slowAlpha, uint32_t debounceMs);
  bool update(float rms, float threshold, uint32_t nowMs);
  float fast() const { return fast_; }
  float slow() const { return slow_; }

 private:
  float fastAlpha_ = 0.25f;
  float slowAlpha_ = 0.995f;
  uint32_t debounceMs_ = 300;
  float fast_ = 0.0f;
  float slow_ = 0.0f;
  bool seeded_ = false;
  bool armed_ = true;
  bool hasFired_ = false;
  uint32_t lastFireMs_ = 0;
};

}  // namespace gaia
