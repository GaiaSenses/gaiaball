#pragma once
#include <stdint.h>

namespace gaia {

// CO2 trigger with hysteresis. Off -> on when ppm > trigger; on -> off when
// ppm <= trigger - hyst (clamped at 0). A stale reading forces it off unless the
// virtual CO2 pulse is active (the pulse then supplies ppm).
class Co2Trigger {
 public:
  void configure(uint16_t triggerPpm, uint16_t hystPpm) {
    triggerPpm_ = triggerPpm;
    hystPpm_ = hystPpm;
  }

  bool update(uint16_t ppm, bool stale, bool pulseActive) {
    if (stale && !pulseActive) {
      on_ = false;
    } else if (!on_) {
      on_ = ppm > triggerPpm_;
    } else {
      const uint16_t offPpm = hystPpm_ >= triggerPpm_ ? 0 : (uint16_t)(triggerPpm_ - hystPpm_);
      on_ = ppm > offPpm;
    }
    return on_;
  }

  bool on() const { return on_; }
  void reset() { on_ = false; }

 private:
  uint16_t triggerPpm_ = 1200;
  uint16_t hystPpm_ = 50;
  bool on_ = false;
};

}  // namespace gaia
