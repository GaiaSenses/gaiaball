#pragma once
#include <stdint.h>

#include "QuatMath.h"

namespace gaia {

// Madgwick sensor-fusion filter (gradient descent, x-io formulation) for the GY-91
// module's MPU-9250. Replaces the ICM-20948 DMP: the MPU-9250's on-chip DMP is not
// used — fusion runs on the ESP32 (a few µs per update), host-testable.
//
// Convention: quat() is the BODY->EARTH rotation built from chip-frame gyro/accel
// (and, in update9, magnetometer already remapped to the chip frame). rotate(quat,
// accel_body) converges to earth +Z — pinned by the unit tests.
class MadgwickFusion {
 public:
  // beta: algorithm gain. Larger = faster accel/mag correction, more noise;
  // smaller = smoother, slower drift correction. 0.1 is the usual default.
  void configure(float beta) { beta_ = beta; }
  void reset() { q_ = Quat{1, 0, 0, 0}; }
  const Quat &quat() const { return q_; }

  // 6-axis (gyro + accel). gyro in rad/s, accel in any consistent unit (normalized
  // internally; all-zero accel skips the correction step), dt in seconds.
  void update6(float gx, float gy, float gz, float ax, float ay, float az, float dt);

  // 9-axis (gyro + accel + mag). mag in any consistent unit, ALREADY remapped to the
  // gyro/accel chip frame. All-zero mag falls back to update6.
  void update9(float gx, float gy, float gz, float ax, float ay, float az, float mx,
               float my, float mz, float dt);

 private:
  Quat q_{1, 0, 0, 0};
  float beta_ = 0.1f;
};

// Gyro bias estimator: adapts only while the ball rests (all corrected axes below a
// small threshold for a sustained time), so a constant gyro offset stops turning
// into yaw drift ("the globe slowly spinning on its own").
class GyroBiasTracker {
 public:
  static constexpr float kStillDps = 2.0f;        // |g - bias| below this = still
  static constexpr uint32_t kStillHoldMs = 1000;  // stillness required before adapting
  static constexpr float kAlpha = 0.005f;         // EMA rate while adapting

  // In/out in dps: replaces g[] with the bias-corrected values.
  void correct(float g[3], uint32_t nowMs);
  const float *bias() const { return bias_; }
  bool adapting() const { return adapting_; }

 private:
  float bias_[3] = {0, 0, 0};
  uint32_t stillSinceMs_ = 0;
  bool wasStill_ = false;
  bool adapting_ = false;
};

}  // namespace gaia
