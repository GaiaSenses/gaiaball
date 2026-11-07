#include "PeakDetector.h"

namespace gaia {

void PeakDetector::configure(float fastAlpha, float slowAlpha, uint32_t debounceMs) {
  fastAlpha_ = fastAlpha;
  slowAlpha_ = slowAlpha;
  debounceMs_ = debounceMs;
}

bool PeakDetector::update(float rms, float threshold, uint32_t nowMs) {
  if (!seeded_) {
    fast_ = slow_ = rms;
    seeded_ = true;
  } else {
    fast_ += fastAlpha_ * (rms - fast_);
    slow_ = slowAlpha_ * slow_ + (1.0f - slowAlpha_) * rms;
  }
  const float level = fast_ - slow_;
  if (!armed_) {
    if (level < threshold * 0.7f) armed_ = true;
    return false;
  }
  if (level > threshold && (!hasFired_ || (uint32_t)(nowMs - lastFireMs_) >= debounceMs_)) {
    armed_ = false;
    hasFired_ = true;
    lastFireMs_ = nowMs;
    return true;
  }
  return false;
}

}  // namespace gaia
