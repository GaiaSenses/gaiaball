#pragma once
#include <stdint.h>

#include "../logic/Filters.h"
#include "../logic/GaiaConfig.h"

namespace gaia {

// Threshold potentiometer on GPIO34 (ADC1_CHANNEL_6, 12-bit, 11 dB). No own task:
// the App task calls poll() at ~10 Hz and reads threshold(); both on that one task,
// so no locking. Raw is EMA-filtered (alpha = potEmaAlphaX1000/1000, TLV 0x1B,
// default 0.3) and mapped linearly to
// [kThresholdMin, kThresholdMax]; the output only moves when the new mapped value
// differs from the current output by >= 2 %.
// With potControlsThreshold == 0, threshold() returns config.thresholdFixed.
class PotService {
 public:
  static constexpr float kThresholdMin = 10000.0f;
  static constexpr float kThresholdMax = 500000.0f;

  void begin(const GaiaConfig &cfg);  // adc1_config once, applies cfg
  void applyConfig(const GaiaConfig &cfg);
  void poll(uint32_t nowMs);
  float threshold() const { return potControls_ ? output_ : fixed_; }
  float filteredRaw() const { return filtered_; }

 private:
  Ema<float> ema_;
  float filtered_ = 0.0f;
  float output_ = kThresholdMin;
  bool haveOutput_ = false;
  bool potControls_ = true;
  float fixed_ = 100000.0f;
};

}  // namespace gaia
