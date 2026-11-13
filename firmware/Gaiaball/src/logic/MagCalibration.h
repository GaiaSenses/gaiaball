#pragma once
#include <math.h>
#include <stdint.h>

// Magnetometer hard/soft-iron calibration.
//
// WHY: the AK8963 inside the GY-91 only gets its factory sensitivity adjustment (ASA)
// from the driver. Everything else on the board — the speaker magnet, the battery, the
// LED and motor currents, any ferrous screw — adds a CONSTANT field in the sensor frame
// (hard iron) and distorts the sphere traced by the earth field into an ellipsoid (soft
// iron). Uncorrected, both errors move the heading by tens of degrees, so an
// uncalibrated 9-axis estimate can be worse than the 6-axis one.
//
// MODEL (same as x-io Fusion's FusionModelMagnetic):   m_c = S * (m_u - h)
//   h = hard-iron offset vector (uT), S = soft-iron matrix (dimensionless, row-major).
// The ellipsoid fit that produces h and S runs in the config-app (browser CPU); the
// firmware only applies the result, which costs 9 multiply-adds per sample.
namespace gaia {

// Wire/NVS fixed point for the 24-byte TLV record (id 0x1E).
constexpr int16_t kMagSoftIronOne = 4096;  // int16 LSB that means 1.0
constexpr float kMagHardIronLsbUt = 0.1f;  // int16 LSB in uT  (+-3276.7 uT)
constexpr float kMagSoftIronLsb = 1.0f / static_cast<float>(kMagSoftIronOne);

struct MagCal {
  float hardIronUt[3];
  float softIron[9];  // row-major 3x3

  // True when applying this calibration is a no-op (zero offset, identity matrix),
  // which lets the IMU pipeline skip the multiply and report "not calibrated".
  bool isIdentity() const {
    for (int i = 0; i < 3; i++)
      if (hardIronUt[i] != 0.0f) return false;
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++)
        if (softIron[3 * r + c] != (r == c ? 1.0f : 0.0f)) return false;
    return true;
  }
};

inline MagCal magCalIdentity() {
  return MagCal{{0, 0, 0}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};
}

// m_c = S (m_u - h). `out` may alias `in`.
inline void magApply(const MagCal &c, const float in[3], float out[3]) {
  const float d0 = in[0] - c.hardIronUt[0];
  const float d1 = in[1] - c.hardIronUt[1];
  const float d2 = in[2] - c.hardIronUt[2];
  out[0] = c.softIron[0] * d0 + c.softIron[1] * d1 + c.softIron[2] * d2;
  out[1] = c.softIron[3] * d0 + c.softIron[4] * d1 + c.softIron[5] * d2;
  out[2] = c.softIron[6] * d0 + c.softIron[7] * d1 + c.softIron[8] * d2;
}

inline MagCal magCalFromFixed(const int16_t hard[3], const int16_t soft[9]) {
  MagCal c{};
  for (int i = 0; i < 3; i++) c.hardIronUt[i] = static_cast<float>(hard[i]) * kMagHardIronLsbUt;
  for (int i = 0; i < 9; i++) c.softIron[i] = static_cast<float>(soft[i]) * kMagSoftIronLsb;
  return c;
}

namespace detail {
inline int16_t satRound(float v) {
  if (!isfinite(v)) return 0;
  if (v >= 32767.0f) return 32767;
  if (v <= -32768.0f) return -32768;
  return static_cast<int16_t>(lroundf(v));
}
}  // namespace detail

inline void magCalToFixed(const MagCal &c, int16_t hard[3], int16_t soft[9]) {
  for (int i = 0; i < 3; i++)
    hard[i] = detail::satRound(c.hardIronUt[i] / kMagHardIronLsbUt);
  for (int i = 0; i < 9; i++)
    soft[i] = detail::satRound(c.softIron[i] * static_cast<float>(kMagSoftIronOne));
}

}  // namespace gaia
