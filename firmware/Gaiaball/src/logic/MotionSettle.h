#pragma once
#include <stdint.h>

namespace gaia {

// Moving/settled detector with hysteresis and a hold time. Starts settled.
// Settled -> moving when speed >= start. While moving, speed <= stop accumulates
// low time (reset whenever speed > stop); stopMs of continuous low time ->
// settled. Timestamps are millis(); the low timer is wrap-safe.
class MotionSettle {
 public:
  void configure(float startDegS, float stopDegS, uint32_t stopMs) {
    startDegS_ = startDegS;
    stopDegS_ = stopDegS;
    stopMs_ = stopMs;
  }

  void reset() {
    settled_ = true;
    lowActive_ = false;
  }

  void update(float speedDegS, uint32_t nowMs) {
    if (settled_) {
      if (speedDegS >= startDegS_) {
        settled_ = false;
        lowActive_ = false;
      }
      return;
    }
    if (!(speedDegS <= stopDegS_)) {
      lowActive_ = false;
      return;
    }
    if (!lowActive_) {
      lowActive_ = true;
      lowSinceMs_ = nowMs;
    }
    if ((uint32_t)(nowMs - lowSinceMs_) >= stopMs_) reset();
  }

  bool settled() const { return settled_; }
  bool moving() const { return !settled_; }

 private:
  float startDegS_ = 20.f;
  float stopDegS_ = 5.f;
  uint32_t stopMs_ = 420;
  bool settled_ = true;
  bool lowActive_ = false;
  uint32_t lowSinceMs_ = 0;
};

}  // namespace gaia
