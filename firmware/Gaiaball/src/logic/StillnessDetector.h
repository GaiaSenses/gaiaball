#pragma once
#include <math.h>
#include <stdint.h>

namespace gaia {

// "Is the ball still?" from the IMU, for the breath detector (v10, TLVs 0x3D-0x3F, 0x42,
// 0x43). Artists blow on the ball only while holding it still; turning it is navigating
// the globe, and the hands that turn it are loud in the breath band (fingers on the shell
// and over the microphone ports, the rubbing of a turn, knocks). With breathStillOnly on,
// a breath fires only while this says "still".
//
// Still = the NET angular speed below maxDps AND the raw accelerometer magnitude within
// maxAccelMg of 1 g (at rest it reads 1 g in any orientation; a knock or a shake does not),
// for holdMs since the last sample that broke either.
//
// WHY the net speed and not |gyro|: a hand holding a ball still trembles (physiological
// tremor, 8-12 Hz), and the instantaneous angular speed of that tremor can reach several
// deg/s. It oscillates back and forth, so averaging the angular-velocity VECTOR cancels it,
// while a real turn adds up. The average is a first-order low-pass with time constant
// windowMs (default 100 ms, like the globe's speed window): a 10 Hz tremor comes out ~6x
// smaller, a sustained turn shows at 63 % after windowMs. The accelerometer is tested on
// each raw sample, unaveraged, so a knock is not smoothed away.
//
// Host-testable; one task only (the App task, fed by every IMU sample).
struct StillnessParams {
  float maxDps = 20.0f;
  float maxAccelMg = 300.0f;
  uint32_t holdMs = 300;
  uint32_t windowMs = 100;
};

class StillnessDetector {
 public:
  void configure(const StillnessParams &p) { p_ = p; }

  // gyroDps: bias-corrected angular velocity (deg/s, 3 axes); accelG: |acceleration| in g;
  // tUs: sample time (for the filter); nowMs: for the hold. Returns moving().
  bool update(const float gyroDps[3], float accelG, uint32_t tUs, uint32_t nowMs) {
    if (!seeded_) {
      for (int i = 0; i < 3; i++) avg_[i] = gyroDps[i];
      seeded_ = true;
    } else {
      float dt = (uint32_t)(tUs - lastUs_) * 1e-6f;
      if (!(dt > 0.0f) || dt > 0.5f) dt = 0.02f;  // a gap or a wrapped clock: one 50 Hz step
      const float tau = p_.windowMs * 1e-3f;
      const float a = tau > 0.0f ? 1.0f - expf(-dt / tau) : 1.0f;
      for (int i = 0; i < 3; i++) avg_[i] += a * (gyroDps[i] - avg_[i]);
    }
    lastUs_ = tUs;
    netDps_ = sqrtf(avg_[0] * avg_[0] + avg_[1] * avg_[1] + avg_[2] * avg_[2]);
    const float dev = (accelG > 1.0f ? accelG - 1.0f : 1.0f - accelG) * 1000.0f;
    if (netDps_ > p_.maxDps || dev > p_.maxAccelMg) {
      lastMoveMs_ = nowMs;
      everMoved_ = true;
    }
    moving_ = everMoved_ && (uint32_t)(nowMs - lastMoveMs_) <= p_.holdMs;
    return moving_;
  }
  bool moving() const { return moving_; }
  // The averaged (net) angular speed, deg/s: what is compared with maxDps.
  float netDps() const { return netDps_; }

 private:
  StillnessParams p_;
  float avg_[3] = {0.0f, 0.0f, 0.0f};
  float netDps_ = 0.0f;
  uint32_t lastUs_ = 0, lastMoveMs_ = 0;
  bool seeded_ = false, everMoved_ = false, moving_ = false;
};

}  // namespace gaia
