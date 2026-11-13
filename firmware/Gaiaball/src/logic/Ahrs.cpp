#include "Ahrs.h"

#include <math.h>

namespace gaia {
namespace {

constexpr float kRad2Deg = 57.29577951308232f;
constexpr float kDeg2Rad = 0.017453292519943295f;

inline void vecZero(float v[3]) { v[0] = v[1] = v[2] = 0.0f; }

inline bool vecIsZero(const float v[3]) {
  return v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f;
}

inline float vecNormSq(const float v[3]) {
  return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

inline bool vecFinite(const float v[3]) {
  return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

inline void vecCross(const float a[3], const float b[3], float out[3]) {
  const float x = a[1] * b[2] - a[2] * b[1];
  const float y = a[2] * b[0] - a[0] * b[2];
  const float z = a[0] * b[1] - a[1] * b[0];
  out[0] = x;
  out[1] = y;
  out[2] = z;
}

inline bool vecNormalise(float v[3]) {
  const float n2 = vecNormSq(v);
  if (n2 <= 0.0f || !isfinite(n2)) return false;
  const float inv = 1.0f / sqrtf(n2);
  v[0] *= inv;
  v[1] *= inv;
  v[2] *= inv;
  return true;
}

inline float vecDot(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Residual between a measured unit vector and the reference direction the filter
// believes in. Below 90 deg the cross product is the (sine-scaled) error; beyond that
// it is normalised so a nearly-inverted estimate still turns the shortest way instead
// of stalling at the antipode.
void residual(const float sensor[3], const float reference[3], float out[3]) {
  vecCross(sensor, reference, out);
  if (vecDot(sensor, reference) > 0.0f) return;
  if (!vecNormalise(out)) vecZero(out);
}

// Error angle in degrees from a half-residual: |r| = 0.5 * sin(error).
float residualDeg(const float r[3]) {
  float s = 2.0f * sqrtf(vecNormSq(r));
  if (!isfinite(s)) return 0.0f;
  if (s > 1.0f) s = 1.0f;
  return asinf(s) * kRad2Deg;
}

int32_t clampI32(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Threshold on the squared half-residual for a given error angle in degrees.
float rejectionThreshold(float deg) {
  const float h = 0.5f * sinf(deg * kDeg2Rad);
  return h * h;
}

}  // namespace

void Ahrs::configure(const AhrsSettings &settings) {
  settings_ = settings;
  if (settings_.sampleRateHz <= 0.0f) settings_.sampleRateHz = 50.0f;
  const float samplePeriod = 1.0f / settings_.sampleRateHz;

  startupGainRate_ = ((kAhrsStartupGain - settings_.gain) / kAhrsStartupPeriodS) * samplePeriod;
  overrangeThreshold_ = 0.98f * settings_.gyroRangeDps;
  rejectionTimeout_ = (int32_t)(settings_.sampleRateHz * settings_.rejectionTimeoutS);

  // The rejection features need both a threshold and a recovery timeout to be safe:
  // without the timeout a long disturbance would leave the filter on the gyroscope
  // forever, so either value being zero disables them (as upstream does).
  const bool rejectionUsable =
      settings_.gain > 0.0f && settings_.rejectionTimeoutS > 0.0f;
  accelRejection_ = (rejectionUsable && settings_.accelRejectionDeg > 0.0f)
                        ? rejectionThreshold(settings_.accelRejectionDeg)
                        : 0.0f;
  magRejection_ = (rejectionUsable && settings_.magRejectionDeg > 0.0f)
                      ? rejectionThreshold(settings_.magRejectionDeg)
                      : 0.0f;
  accelRecoveryThreshold_ = rejectionTimeout_;
  magRecoveryThreshold_ = rejectionTimeout_;
}

void Ahrs::restart() {
  q_ = Quat{1, 0, 0, 0};
  softRestart();
}

void Ahrs::softRestart() {
  startup_ = true;
  startupGain_ = kAhrsStartupGain;
  overrangeRecovery_ = false;
  vecZero(halfAccelResidual_);
  accelRecoveryTrigger_ = 0;
  accelRecoveryThreshold_ = rejectionTimeout_;
  accelIgnored_ = false;
  vecZero(halfMagResidual_);
  magRecoveryTrigger_ = 0;
  magRecoveryThreshold_ = rejectionTimeout_;
  magIgnored_ = false;
}

void Ahrs::skipStartup() {
  startup_ = false;
  overrangeRecovery_ = false;
}

float Ahrs::accelErrorDeg() const { return residualDeg(halfAccelResidual_); }
float Ahrs::magErrorDeg() const { return residualDeg(halfMagResidual_); }

float Ahrs::rampedGain() {
  if (!startup_) return settings_.gain;
  startupGain_ -= startupGainRate_;
  if (startupGain_ > settings_.gain) return startupGain_;
  startup_ = false;
  overrangeRecovery_ = false;
  return settings_.gain;
}

void Ahrs::inclinationFeedback(const float halfGravity[3], const float accelG[3],
                               float out[3]) {
  vecZero(out);
  accelIgnored_ = true;
  if (vecIsZero(accelG)) return;

  float a[3] = {accelG[0], accelG[1], accelG[2]};
  if (!vecNormalise(a)) return;
  residual(a, halfGravity, halfAccelResidual_);

  if (accelRejection_ == 0.0f || startup_ ||
      vecNormSq(halfAccelResidual_) <= accelRejection_) {
    accelIgnored_ = false;
    accelRecoveryTrigger_ -= 9;  // 9:1 decay, i.e. recovery needs 90% of updates to fail
  } else {
    accelRecoveryTrigger_ += 1;
  }
  if (accelRecoveryTrigger_ > accelRecoveryThreshold_) {
    accelRecoveryThreshold_ = 0;  // stay in recovery until the trigger decays to zero
    accelIgnored_ = false;
  } else {
    accelRecoveryThreshold_ = rejectionTimeout_;
  }
  accelRecoveryTrigger_ = clampI32(accelRecoveryTrigger_, 0, rejectionTimeout_);

  if (!accelIgnored_) {
    out[0] = halfAccelResidual_[0];
    out[1] = halfAccelResidual_[1];
    out[2] = halfAccelResidual_[2];
  }
}

void Ahrs::headingFeedback(const float halfGravity[3], const float magUt[3],
                           float out[3]) {
  vecZero(out);
  magIgnored_ = true;
  if (magUt == nullptr || vecIsZero(magUt)) return;

  // The cross product of gravity and the magnetic field is west, which is a heading
  // reference that does not depend on the field's inclination (dip) or strength.
  const float halfWest[3] = {
      q_.x * q_.y + q_.w * q_.z,
      q_.w * q_.w - 0.5f + q_.y * q_.y,
      q_.y * q_.z - q_.w * q_.x,
  };
  float west[3];
  vecCross(halfGravity, magUt, west);
  if (!vecNormalise(west)) return;
  residual(west, halfWest, halfMagResidual_);

  if (magRejection_ == 0.0f || startup_ ||
      vecNormSq(halfMagResidual_) <= magRejection_) {
    magIgnored_ = false;
    magRecoveryTrigger_ -= 9;
  } else {
    magRecoveryTrigger_ += 1;
  }
  if (magRecoveryTrigger_ > magRecoveryThreshold_) {
    magRecoveryThreshold_ = 0;
    magIgnored_ = false;
  } else {
    magRecoveryThreshold_ = rejectionTimeout_;
  }
  magRecoveryTrigger_ = clampI32(magRecoveryTrigger_, 0, rejectionTimeout_);

  if (!magIgnored_) {
    out[0] = halfMagResidual_[0];
    out[1] = halfMagResidual_[1];
    out[2] = halfMagResidual_[2];
  }
}

void Ahrs::updateInternal(const float gyroDps[3], const float accelG[3],
                          const float magUt[3], float dt) {
  // One bad I2C read (or a non-finite dt) must not be integrated: hold the estimate.
  if (!vecFinite(gyroDps) || !vecFinite(accelG) || !isfinite(dt) || dt <= 0.0f) return;
  if (magUt != nullptr && !vecFinite(magUt)) return;

  if (settings_.gyroRangeDps > 0.0f &&
      (fabsf(gyroDps[0]) > overrangeThreshold_ || fabsf(gyroDps[1]) > overrangeThreshold_ ||
       fabsf(gyroDps[2]) > overrangeThreshold_)) {
    softRestart();  // keeps the last orientation, drops the corrupt rejection state
    overrangeRecovery_ = true;
  }

  const float gain = rampedGain();

  // Direction of gravity in the sensor frame, scaled by 0.5: the third column of the
  // transposed rotation matrix (no trigonometry, 5 multiplies).
  const float halfGravity[3] = {
      q_.x * q_.z - q_.w * q_.y,
      q_.y * q_.z + q_.w * q_.x,
      q_.w * q_.w - 0.5f + q_.z * q_.z,
  };

  float fbAccel[3], fbMag[3];
  inclinationFeedback(halfGravity, accelG, fbAccel);
  headingFeedback(halfGravity, magUt, fbMag);

  // Half the corrected angular rate, in rad/s.
  const float half = 0.5f * kDeg2Rad;
  const float w[3] = {
      gyroDps[0] * half + gain * (fbAccel[0] + fbMag[0]),
      gyroDps[1] * half + gain * (fbAccel[1] + fbMag[1]),
      gyroDps[2] * half + gain * (fbAccel[2] + fbMag[2]),
  };
  const float d[3] = {w[0] * dt, w[1] * dt, w[2] * dt};

  // q += q (x) (0, d): quaternion times a pure vector.
  const float qw = q_.w, qx = q_.x, qy = q_.y, qz = q_.z;
  Quat n{
      qw + (-qx * d[0] - qy * d[1] - qz * d[2]),
      qx + (qw * d[0] + qy * d[2] - qz * d[1]),
      qy + (qw * d[1] - qx * d[2] + qz * d[0]),
      qz + (qw * d[2] + qx * d[1] - qy * d[0]),
  };
  const float n2 = n.w * n.w + n.x * n.x + n.y * n.y + n.z * n.z;
  if (!isfinite(n2) || n2 <= 0.0f) {
    restart();
    return;
  }
  const float inv = 1.0f / sqrtf(n2);
  q_ = Quat{n.w * inv, n.x * inv, n.y * inv, n.z * inv};
}

void Ahrs::update6(const float gyroDps[3], const float accelG[3], float dt) {
  updateInternal(gyroDps, accelG, nullptr, dt);
}

void Ahrs::update9(const float gyroDps[3], const float accelG[3], const float magUt[3],
                   float dt) {
  updateInternal(gyroDps, accelG, magUt, dt);
}

}  // namespace gaia
