#pragma once
#include <stdint.h>

namespace gaia {

enum class BreathMode : uint8_t { Absolute = 0, Adaptive = 1 };

struct BreathParams {
  BreathMode mode = BreathMode::Absolute;
  // Absolute: the line. Adaptive: how far above the slow baseline the fast envelope must rise.
  float thresholdCounts = 0.0f;
  uint8_t rearmDb = 3;  // 0..20 whole dB below the line before the detector re-arms
  uint32_t debounceMs = 300;
  float fastAlpha = 0.25f;   // adaptive only: weight of the new sample
  float slowAlpha = 0.995f;  // adaptive only: near 1 = slow baseline
};

// Breath ("sopro") detector on linear level values (counts) of the two microphones.
//  Absolute: fires when EITHER level exceeds the line.
//  Adaptive: on max(L, R), fires when fast - slow exceeds the threshold
//            (fast/slow EMAs seeded by the first update).
// After a fire it stays disarmed until the level (absolute) or fast - slow (adaptive)
// falls below threshold * 10^(-rearmDb/20), so one sustained breath yields one event.
// Debounce is wrap-safe. Not thread-safe: use from a single task.
class BreathDetector {
 public:
  // Resets the state when the mode changes; otherwise keeps it so live re-tuning does
  // not cause a spurious fire.
  void configure(const BreathParams &p);
  // True on the firing edge only.
  bool update(const float level[2], uint32_t nowMs);
  // 1 from the fire until the re-arm.
  bool recognized() const { return !armed_; }
  // Absolute: thresholdCounts. Adaptive: slow baseline + thresholdCounts.
  float triggerLevel() const;

 private:
  BreathParams p_;
  float rearmRatio_ = 0.70794576f;  // 10^(-3/20)
  float fast_ = 0.0f;
  float slow_ = 0.0f;
  bool seeded_ = false;
  bool armed_ = true;
  bool hasFired_ = false;
  uint32_t lastFireMs_ = 0;
};

}  // namespace gaia
