#include "PotService.h"

#include <Arduino.h>
#include <driver/adc.h>
#include <math.h>

namespace gaia {

void PotService::begin(const GaiaConfig &cfg) {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);  // GPIO34
  ema_.setAlpha(0.3f);
  applyConfig(cfg);
  poll(0);  // seed so threshold() is valid immediately
}

void PotService::applyConfig(const GaiaConfig &cfg) {
  potControls_ = cfg.potControlsThreshold != 0;
  fixed_ = (float)cfg.thresholdFixed;
}

void PotService::poll(uint32_t /*nowMs*/) {
  const int raw = adc1_get_raw(ADC1_CHANNEL_6);  // 0..4095
  if (raw < 0) return;
  filtered_ = ema_.update((float)raw);
  const float mapped = kThresholdMin + (filtered_ / 4095.0f) * (kThresholdMax - kThresholdMin);
  if (!haveOutput_ || fabsf(mapped - output_) >= 0.02f * output_) {
    output_ = mapped;
    haveOutput_ = true;
  }
}

}  // namespace gaia
