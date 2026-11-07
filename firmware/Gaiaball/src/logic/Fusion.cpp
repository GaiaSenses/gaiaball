#include "Fusion.h"

#include <math.h>

namespace gaia {

namespace {
inline float invSqrt(float x) { return 1.0f / sqrtf(x); }
}  // namespace

void MadgwickFusion::update6(float gx, float gy, float gz, float ax, float ay, float az,
                             float dt) {
  float q0 = q_.w, q1 = q_.x, q2 = q_.y, q3 = q_.z;

  // Rate of change of quaternion from gyroscope.
  float qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
  float qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
  float qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
  float qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

  const float an = ax * ax + ay * ay + az * az;
  if (an > 0.0f) {
    const float rn = invSqrt(an);
    ax *= rn;
    ay *= rn;
    az *= rn;

    // Gradient descent step (x-io MadgwickAHRS updateIMU).
    const float _2q0 = 2.0f * q0, _2q1 = 2.0f * q1, _2q2 = 2.0f * q2, _2q3 = 2.0f * q3;
    const float _4q0 = 4.0f * q0, _4q1 = 4.0f * q1, _4q2 = 4.0f * q2;
    const float _8q1 = 8.0f * q1, _8q2 = 8.0f * q2;
    const float q0q0 = q0 * q0, q1q1 = q1 * q1, q2q2 = q2 * q2, q3q3 = q3 * q3;

    float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
    float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 +
               _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
    float s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 +
               _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
    float s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;
    const float sn = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
    if (sn > 0.0f) {
      const float rsn = invSqrt(sn);
      qDot1 -= beta_ * s0 * rsn;
      qDot2 -= beta_ * s1 * rsn;
      qDot3 -= beta_ * s2 * rsn;
      qDot4 -= beta_ * s3 * rsn;
    }
  }

  q0 += qDot1 * dt;
  q1 += qDot2 * dt;
  q2 += qDot3 * dt;
  q3 += qDot4 * dt;
  const float rq = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q_ = Quat{q0 * rq, q1 * rq, q2 * rq, q3 * rq};
}

void MadgwickFusion::update9(float gx, float gy, float gz, float ax, float ay, float az,
                             float mx, float my, float mz, float dt) {
  const float mn = mx * mx + my * my + mz * mz;
  if (mn <= 0.0f) {
    update6(gx, gy, gz, ax, ay, az, dt);
    return;
  }
  float q0 = q_.w, q1 = q_.x, q2 = q_.y, q3 = q_.z;

  float qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
  float qDot2 = 0.5f * (q0 * gx + q2 * gz - q3 * gy);
  float qDot3 = 0.5f * (q0 * gy - q1 * gz + q3 * gx);
  float qDot4 = 0.5f * (q0 * gz + q1 * gy - q2 * gx);

  const float an = ax * ax + ay * ay + az * az;
  if (an > 0.0f) {
    float rn = invSqrt(an);
    ax *= rn;
    ay *= rn;
    az *= rn;
    rn = invSqrt(mn);
    mx *= rn;
    my *= rn;
    mz *= rn;

    // x-io MadgwickAHRS update (full AHRS gradient step).
    const float _2q0mx = 2.0f * q0 * mx, _2q0my = 2.0f * q0 * my, _2q0mz = 2.0f * q0 * mz;
    const float _2q1mx = 2.0f * q1 * mx;
    const float _2q0 = 2.0f * q0, _2q1 = 2.0f * q1, _2q2 = 2.0f * q2, _2q3 = 2.0f * q3;
    const float _2q0q2 = 2.0f * q0 * q2, _2q2q3 = 2.0f * q2 * q3;
    const float q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
    const float q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3;
    const float q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;

    const float hx = mx * q0q0 - _2q0my * q3 + _2q0mz * q2 + mx * q1q1 +
                     _2q1 * my * q2 + _2q1 * mz * q3 - mx * q2q2 - mx * q3q3;
    const float hy = _2q0mx * q3 + my * q0q0 - _2q0mz * q1 + _2q1mx * q2 - my * q1q1 +
                     my * q2q2 + _2q2 * mz * q3 - my * q3q3;
    const float _2bx = sqrtf(hx * hx + hy * hy);
    const float _2bz = -_2q0mx * q2 + _2q0my * q1 + mz * q0q0 + _2q1mx * q3 -
                       mz * q1q1 + _2q2 * my * q3 - mz * q2q2 + mz * q3q3;
    const float _4bx = 2.0f * _2bx, _4bz = 2.0f * _2bz;

    float s0 = -_2q2 * (2.0f * q1q3 - _2q0q2 - ax) + _2q1 * (2.0f * q0q1 + _2q2q3 - ay) -
               _2bz * q2 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
               (-_2bx * q3 + _2bz * q1) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
               _2bx * q2 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    float s1 = _2q3 * (2.0f * q1q3 - _2q0q2 - ax) + _2q0 * (2.0f * q0q1 + _2q2q3 - ay) -
               4.0f * q1 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) +
               _2bz * q3 * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
               (_2bx * q2 + _2bz * q0) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
               (_2bx * q3 - _4bz * q1) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    float s2 = -_2q0 * (2.0f * q1q3 - _2q0q2 - ax) + _2q3 * (2.0f * q0q1 + _2q2q3 - ay) -
               4.0f * q2 * (1.0f - 2.0f * q1q1 - 2.0f * q2q2 - az) +
               (-_4bx * q2 - _2bz * q0) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
               (_2bx * q1 + _2bz * q3) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
               (_2bx * q0 - _4bz * q2) * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    float s3 = _2q1 * (2.0f * q1q3 - _2q0q2 - ax) + _2q2 * (2.0f * q0q1 + _2q2q3 - ay) +
               (-_4bx * q3 + _2bz * q1) * (_2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx) +
               (-_2bx * q0 + _2bz * q2) * (_2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my) +
               _2bx * q1 * (_2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz);
    const float sn = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3;
    if (sn > 0.0f) {
      const float rsn = invSqrt(sn);
      qDot1 -= beta_ * s0 * rsn;
      qDot2 -= beta_ * s1 * rsn;
      qDot3 -= beta_ * s2 * rsn;
      qDot4 -= beta_ * s3 * rsn;
    }
  }

  q0 += qDot1 * dt;
  q1 += qDot2 * dt;
  q2 += qDot3 * dt;
  q3 += qDot4 * dt;
  const float rq = invSqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
  q_ = Quat{q0 * rq, q1 * rq, q2 * rq, q3 * rq};
}

void GyroBiasTracker::correct(float g[3], uint32_t nowMs) {
  float corrected[3];
  bool still = true;
  for (int i = 0; i < 3; i++) {
    corrected[i] = g[i] - bias_[i];
    if (fabsf(corrected[i]) >= kStillDps) still = false;
  }
  if (still) {
    if (!wasStill_) {
      wasStill_ = true;
      stillSinceMs_ = nowMs;
    }
    adapting_ = (uint32_t)(nowMs - stillSinceMs_) >= kStillHoldMs;
    if (adapting_) {
      for (int i = 0; i < 3; i++) bias_[i] += kAlpha * (g[i] - bias_[i]);
    }
  } else {
    wasStill_ = false;
    adapting_ = false;
  }
  for (int i = 0; i < 3; i++) g[i] = g[i] - bias_[i];
}

}  // namespace gaia
