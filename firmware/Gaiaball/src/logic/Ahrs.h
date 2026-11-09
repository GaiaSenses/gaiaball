#pragma once
#include <stdint.h>

#include "QuatMath.h"

namespace gaia {

// Settings of the AHRS below. Zeros disable the optional features, which turns the
// filter back into a plain complementary filter (useful for tests and for A/B runs).
// The startup ramp: the gain starts here and falls to AhrsSettings::gain over
// kAhrsStartupPeriodS, so a filter that boots in an unknown orientation converges in
// about a second instead of tens of seconds.
constexpr float kAhrsStartupGain = 10.0f;
constexpr float kAhrsStartupPeriodS = 3.0f;

struct AhrsSettings {
  float gain = 0.5f;               // feedback gain; 0 = gyroscope only
  float sampleRateHz = 50.0f;      // nominal rate: sets the ramp and timeout clocks
  float gyroRangeDps = 0.0f;       // 0 = no overrange detection
  float accelRejectionDeg = 0.0f;  // 0 = never ignore the accelerometer
  float magRejectionDeg = 0.0f;    // 0 = never ignore the magnetometer
  float rejectionTimeoutS = 0.0f;  // 0 = disables BOTH rejection features
};

// Attitude and Heading Reference System: the REVISED Madgwick filter (chapter 7 of
// his PhD thesis, the algorithm published as the x-io "Fusion" library), replacing the
// better-known chapter-3 gradient-descent filter this firmware used until v7.
//
// Structure: orientation = integral of the gyroscope plus a feedback term equal to the
// error of the current estimate as seen by the other sensors, times a gain. It is
// therefore a complementary filter whose corner frequency is set by `gain`: high-pass
// gyroscope + low-pass accelerometer/magnetometer.
//
// WHY the change (measured on the public BROAD benchmark, tuned per algorithm):
//   * acceleration rejection: a thrown, rolled or shaken ball measures gravity PLUS its
//     own acceleration. The chapter-3 filter follows that resultant and tilts; here, an
//     inclination error above `accelRejectionDeg` makes the accelerometer be ignored for
//     that update (a dynamic gain), while a PROLONGED disagreement (longer than
//     `rejectionTimeoutS`) switches to recovery so the filter cannot drift away on the
//     gyroscope alone.
//   * magnetic rejection: same mechanism on the heading, which keeps a passing magnet
//     from dragging the estimate.
//   * startup ramp: the gain starts at 10 and ramps down to `gain` over 3 s, so the
//     estimate converges from an arbitrary initial orientation in about a second instead
//     of tens of seconds.
//   * overrange recovery: angular rates beyond the configured gyroscope range cannot be
//     integrated, so the filter restarts instead of keeping a corrupt state.
//
// Convention: NWU (X north, Y west, Z up). quat() is the BODY->EARTH rotation built from
// chip-frame gyro/accel (and, in update9, a magnetometer already remapped and calibrated
// in the chip frame); rotate(quat, accel_body) converges to earth +Z — pinned by tests.
// Units: gyroscope in DEGREES PER SECOND, accelerometer in g, magnetometer in uT.
class Ahrs {
 public:
  void configure(const AhrsSettings &settings);
  // Full restart: identity orientation, startup ramp from the beginning.
  void restart();
  // Keeps the current orientation but re-runs the ramp and clears the rejection state.
  void softRestart();
  // Declares the current orientation trustworthy, skipping the ramp.
  void skipStartup();

  // 6-axis update. `dt` is the MEASURED sample interval in seconds (compensates
  // sample-clock error); non-finite inputs are ignored so one bad read cannot poison
  // the state.
  void update6(const float gyroDps[3], const float accelG[3], float dt);
  // 9-axis update. An all-zero magnetometer behaves exactly like update6.
  void update9(const float gyroDps[3], const float accelG[3], const float magUt[3],
               float dt);

  const Quat &quat() const { return q_; }

  // --- internal state, for the diagnostics packet and the bench validation ---
  // Angle between the estimated inclination and the one the accelerometer indicates.
  float accelErrorDeg() const;
  // Angle between the estimated heading and the one the magnetometer indicates.
  float magErrorDeg() const;
  bool accelIgnored() const { return accelIgnored_; }
  bool magIgnored() const { return magIgnored_; }
  bool startup() const { return startup_; }
  bool overrangeRecovery() const { return overrangeRecovery_; }

 private:
  // Half of the feedback term: both callers need the residual AND its gating.
  void inclinationFeedback(const float halfGravity[3], const float accelG[3],
                           float out[3]);
  void headingFeedback(const float halfGravity[3], const float magUt[3], float out[3]);
  float rampedGain();
  void updateInternal(const float gyroDps[3], const float accelG[3], const float magUt[3],
                      float dt);

  AhrsSettings settings_{};
  // Thresholds are kept as the squared norm of the half-residual, which is what the
  // update computes: |halfResidual| = 0.5 * sin(error), so no trigonometry per sample.
  float accelRejection_ = 0.0f;  // 0 = disabled
  float magRejection_ = 0.0f;    // 0 = disabled
  int32_t rejectionTimeout_ = 0;
  float startupGainRate_ = 0.0f;

  Quat q_{1, 0, 0, 0};
  float startupGain_ = kAhrsStartupGain;
  bool startup_ = true;
  bool overrangeRecovery_ = false;
  float overrangeThreshold_ = 0.0f;

  float halfAccelResidual_[3] = {0, 0, 0};
  int32_t accelRecoveryTrigger_ = 0;
  int32_t accelRecoveryThreshold_ = 0;
  bool accelIgnored_ = false;

  float halfMagResidual_[3] = {0, 0, 0};
  int32_t magRecoveryTrigger_ = 0;
  int32_t magRecoveryThreshold_ = 0;
  bool magIgnored_ = false;
};

}  // namespace gaia
