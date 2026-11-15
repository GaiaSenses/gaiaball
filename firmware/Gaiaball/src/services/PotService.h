#pragma once
#include <stdint.h>

#include "../logic/Filters.h"
#include "../logic/GaiaConfig.h"
#include "../logic/PotMapping.h"

namespace gaia {

// Threshold potentiometer on GPIO34 (ADC1_CHANNEL_6, 12-bit, 11 dB). No own task:
// the App task calls poll() at ~10 Hz and reads thresholdDbfs(); both on that one
// task, so no locking. Raw is EMA-filtered (alpha = potEmaAlphaX1000/1000, TLV 0x1B,
// default 0.3) and mapped linearly to the breath threshold in dBFS,
// [kPotDbMin, kPotDbMax] = -60..0 dBFS (logic/PotMapping.h); the output only moves
// when the new mapped value is at least kPotDbHysteresis (0.5 dB) away from it.
// With potControlsThreshold == 0, thresholdDbfs() returns the fixed breath threshold
// (TLV 0x2B: -breathThresholdNegDbX100 / 100 dBFS). The audio task turns the dBFS
// value into counts of the active metric (RMS or peak).
class PotService {
 public:
  void begin(const GaiaConfig &cfg);  // adc1_config once, applies cfg
  void applyConfig(const GaiaConfig &cfg);
  void poll(uint32_t nowMs);
  float thresholdDbfs() const { return potControls_ ? outputDbfs_ : fixedDbfs_; }
  float filteredRaw() const { return filtered_; }

 private:
  Ema<float> ema_;
  float filtered_ = 0.0f;
  float outputDbfs_ = kPotDbMin;
  bool haveOutput_ = false;
  bool potControls_ = true;
  float fixedDbfs_ = -35.5f;
};

}  // namespace gaia
