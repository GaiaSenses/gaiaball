#pragma once
#include <stdint.h>

namespace gaia {

// "Is the ball being handled?" from the IMU, for the breath detector (v10, TLV 0x3E,
// 0x3F, 0x42).
//
// WHY: the ball is held and turned to steer the globe, and handling is loud in the breath
// band: fingers on the shell and over the microphone ports, knocks, the rubbing of a turn.
// While it lasts the breath detector raises its fire line (BreathParams::handlingBoostDb),
// so a breath still fires but handling noise alone does not.
//
// Handling = the bias-corrected angular speed above gyroDps, or the accelerometer
// magnitude more than accelMg away from 1 g (a knock or a shake; at rest it reads 1 g in
// any orientation). It stays on for holdMs after the last sample that met either, so the
// detector is not exposed in the gaps of a gesture. Host-testable; one task only.
struct HandlingParams {
  float gyroDps = 30.0f;
  float accelMg = 300.0f;
  uint32_t holdMs = 300;
};

class HandlingDetector {
 public:
  void configure(const HandlingParams &p) { p_ = p; }

  // gyroDps: |angular speed| in deg/s; accelG: |acceleration| in g. Returns active().
  bool update(float gyroDps, float accelG, uint32_t nowMs) {
    const float dev = (accelG > 1.0f ? accelG - 1.0f : 1.0f - accelG) * 1000.0f;
    if (gyroDps > p_.gyroDps || dev > p_.accelMg) {
      lastMs_ = nowMs;
      seen_ = true;
    }
    active_ = seen_ && (uint32_t)(nowMs - lastMs_) <= p_.holdMs;
    return active_;
  }
  bool active() const { return active_; }

 private:
  HandlingParams p_;
  uint32_t lastMs_ = 0;
  bool seen_ = false;
  bool active_ = false;
};

}  // namespace gaia
