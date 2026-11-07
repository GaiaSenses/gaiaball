#pragma once
#include <cstdint>

namespace gaia {

// Limits notifications to rateHz per second, evenly spaced (interval =
// 1e6/rate us). Schedule advances by whole intervals so the long-run rate is
// exact even when calls are not aligned to the interval. Wrap-safe on uint32_t
// microsecond timestamps.
class NotifyThrottle {
 public:
  void setRateHz(uint8_t hz) {
    intervalUs_ = hz ? 1000000u / hz : 0;
    started_ = false;
  }

  bool shouldSend(uint32_t nowUs) {
    if (intervalUs_ == 0) return false;
    if (!started_) {
      started_ = true;
      nextUs_ = nowUs + intervalUs_;
      return true;
    }
    int32_t late = (int32_t)(nowUs - nextUs_);
    if (late < 0) return false;
    // After a long idle gap resync instead of bursting to catch up.
    nextUs_ = ((uint32_t)late >= intervalUs_) ? nowUs + intervalUs_ : nextUs_ + intervalUs_;
    return true;
  }

 private:
  uint32_t intervalUs_ = 0;
  uint32_t nextUs_ = 0;
  bool started_ = false;
};

}  // namespace gaia
