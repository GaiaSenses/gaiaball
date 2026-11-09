#pragma once
#include <stdint.h>

#include "QuatMath.h"

namespace gaia {

enum class QuatSmoothMode : uint8_t {
  Off = 0,         // publish the fused quaternion as it comes out
  SlerpFixed = 1,  // legacy: one-pole SLERP with a FIXED coefficient
  OneEuro = 2,     // 1-euro filter: cutoff follows the angular speed
};

// Output smoothing of the orientation.
//
// WHY: the fused quaternion still carries the gyroscope's noise floor, which looks like
// a fine tremble in a 3D view and, in a sound patch, like a dithering control signal.
//
// WHY NOT only the legacy fixed SLERP: a fixed coefficient buys that quiet by delaying
// everything. At 50 Hz the heaviest setting (amount 100 -> t = 0.1) is a time constant
// near 190 ms, so a flick of the ball arrives a fifth of a second late — audible as
// rubbery response.
//
// HOW the 1-euro filter fixes that (Casiez, Roussel & Vogel, CHI 2012 — the filter used
// by Chrome, Unreal and VRPN for exactly this problem): the cutoff is not fixed but
//     fc = fcMin + beta * |angular speed|
// so a ball held still is filtered hard (jitter disappears) while a ball being moved is
// barely filtered (little lag). Two parameters with a clear meaning: lower `fcMin` to
// remove more jitter at rest, raise `beta` to cut lag during fast motion.
//
// The interpolation itself is a SLERP, so the output is always a unit quaternion and
// always takes the shortest path.
class QuatSmoother {
 public:
  // `slerpAmount` is the legacy 0..100 knob, used only by SlerpFixed.
  // `fcMinHz` > 0 and `betaHzPerDps` >= 0 are used only by OneEuro.
  void configure(QuatSmoothMode mode, uint8_t slerpAmount, float fcMinHz,
                 float betaHzPerDps);
  // Drops the history: the next sample is published unchanged (used when the reference
  // frame jumps, e.g. after zeroing, so the output does not glide into place).
  void reset();
  Quat update(const Quat &q, float dt);

  QuatSmoothMode mode() const { return mode_; }
  // Low-passed angular speed in deg/s (diagnostics; drives the adaptive cutoff).
  float speedDps() const { return speedLp_; }

 private:
  QuatSmoothMode mode_ = QuatSmoothMode::Off;
  float slerpT_ = 1.0f;
  float fcMinHz_ = 1.0f;
  float beta_ = 0.0f;
  Quat prev_{1, 0, 0, 0};
  bool seeded_ = false;
  float speedLp_ = 0.0f;
  bool speedSeeded_ = false;
};

}  // namespace gaia
