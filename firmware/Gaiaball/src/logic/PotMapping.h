#pragma once
// Potentiometer -> breath-threshold mapping (pure logic, host-testable).
#include <cmath>

namespace gaia {

constexpr float kPotDbMin = -60.0f;
constexpr float kPotDbMax = 0.0f;
constexpr float kPotDbHysteresis = 0.5f;

// 12-bit ADC (filtered, 0..4095) -> dBFS, linear, clamped to [kPotDbMin, kPotDbMax].
inline float potRawToDbfs(float filteredRaw) {
  float t = filteredRaw / 4095.0f;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return kPotDbMin + t * (kPotDbMax - kPotDbMin);
}

// True when the new level moved at least the hysteresis away from the current one.
inline bool potDbChanged(float newDb, float currentDb) {
  return std::fabs(newDb - currentDb) >= kPotDbHysteresis - 1e-6f;
}

}  // namespace gaia
