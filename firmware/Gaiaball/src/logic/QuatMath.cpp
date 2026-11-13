#include "QuatMath.h"
#include <math.h>
#include <cmath>

namespace gaia {

namespace {
constexpr double kRadToDeg = 57.295779513082320876798;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

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

Quat quatNormalize(const Quat &q) {
  const float n = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  if (!(n > 1e-12f) || !std::isfinite(n)) return Quat{1.f, 0.f, 0.f, 0.f};
  const float inv = 1.f / n;
  return Quat{q.w * inv, q.x * inv, q.y * inv, q.z * inv};
}

void quatRotateVector(const Quat &q, const float v[3], float out[3]) {
  // v' = v + w t + u x t, with u = (x, y, z) and t = 2 (u x v).
  const float vx = v[0], vy = v[1], vz = v[2];
  const float tx = 2.f * (q.y * vz - q.z * vy);
  const float ty = 2.f * (q.z * vx - q.x * vz);
  const float tz = 2.f * (q.x * vy - q.y * vx);
  out[0] = vx + q.w * tx + (q.y * tz - q.z * ty);
  out[1] = vy + q.w * ty + (q.z * tx - q.x * tz);
  out[2] = vz + q.w * tz + (q.x * ty - q.y * tx);
}

Quat quatFromAxisAngle(const float axis[3], float angleRad) {
  const float n = sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
  if (!(n > 1e-12f)) return Quat{1.f, 0.f, 0.f, 0.f};
  const float s = sinf(0.5f * angleRad) / n;
  return Quat{cosf(0.5f * angleRad), axis[0] * s, axis[1] * s, axis[2] * s};
}

void quatToAxisAngle(const Quat &q, float axis[3], float &angleRad) {
  const float sgn = q.w < 0.f ? -1.f : 1.f;  // shortest path: q and -q agree
  const float w = sgn * q.w, x = sgn * q.x, y = sgn * q.y, z = sgn * q.z;
  const float n = sqrtf(x * x + y * y + z * z);
  if (!(n > 1e-12f)) {
    axis[0] = 1.f;
    axis[1] = 0.f;
    axis[2] = 0.f;
    angleRad = 0.f;
    return;
  }
  axis[0] = x / n;
  axis[1] = y / n;
  axis[2] = z / n;
  angleRad = 2.f * atan2f(n, w);
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
