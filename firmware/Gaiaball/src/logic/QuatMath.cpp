#include "QuatMath.h"
#include <cmath>

namespace gaia {

namespace {
constexpr double kRadToDeg = 57.295779513082320876798;
constexpr double kMinNorm = 1e-6;
const Quat kIdentity{1.f, 0.f, 0.f, 0.f};

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

bool sanitizeQuat(double q1, double q2, double q3, Quat &out) {
  if (!std::isfinite(q1) || !std::isfinite(q2) || !std::isfinite(q3)) {
    out = kIdentity;
    return false;
  }
  const double v2 = q1 * q1 + q2 * q2 + q3 * q3;
  const double w = std::sqrt(v2 < 1.0 ? 1.0 - v2 : 0.0);
  const double n = std::sqrt(w * w + v2);
  if (!(n > kMinNorm)) {
    out = kIdentity;
    return false;
  }
  out = Quat{(float)(w / n), (float)(q1 / n), (float)(q2 / n), (float)(q3 / n)};
  return true;
}

Quat quatMultiply(const Quat &a, const Quat &b) {
  return Quat{a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
              a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
              a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quat quatConjugate(const Quat &q) { return Quat{q.w, -q.x, -q.y, -q.z}; }

Quat quatSlerp(const Quat &a, const Quat &b, float t) {
  t = clampf(t, 0.f, 1.f);
  float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
  Quat bb = b;
  if (dot < 0.f) {  // take the short way around
    dot = -dot;
    bb = Quat{-b.w, -b.x, -b.y, -b.z};
  }
  float s0, s1;
  if (dot > 0.9995f) {  // nearly parallel: lerp avoids dividing by ~0
    s0 = 1.f - t;
    s1 = t;
  } else {
    const float theta = std::acos(dot);
    const float sinTheta = std::sin(theta);
    s0 = std::sin((1.f - t) * theta) / sinTheta;
    s1 = std::sin(t * theta) / sinTheta;
  }
  Quat r{s0 * a.w + s1 * bb.w, s0 * a.x + s1 * bb.x, s0 * a.y + s1 * bb.y,
         s0 * a.z + s1 * bb.z};
  const float n = std::sqrt(r.w * r.w + r.x * r.x + r.y * r.y + r.z * r.z);
  if (!(n > 1e-6f)) return a;
  return Quat{r.w / n, r.x / n, r.y / n, r.z / n};
}

float quatAngleDeg(const Quat &a, const Quat &b) {
  float dot = std::fabs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z);
  dot = clampf(dot, 0.f, 1.f);
  return (float)(2.0 * std::acos((double)dot) * kRadToDeg);
}

void quatToEulerLegacy(const Quat &q, float &rollDeg, float &pitchDeg, float &yawDeg) {
  const double qw = q.w, qx = q.y, qy = q.x, qz = -q.z;
  double sinp = 2.0 * (qw * qy - qx * qz);
  sinp = sinp < -1.0 ? -1.0 : (sinp > 1.0 ? 1.0 : sinp);
  rollDeg = (float)(std::atan2(2.0 * (qw * qx + qy * qz), 1.0 - 2.0 * (qx * qx + qy * qy)) * kRadToDeg);
  pitchDeg = (float)(std::asin(sinp) * kRadToDeg);
  yawDeg = (float)(std::atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qy * qy + qz * qz)) * kRadToDeg);
}

}  // namespace gaia
