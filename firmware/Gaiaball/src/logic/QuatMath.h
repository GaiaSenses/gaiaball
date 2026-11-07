#pragma once

namespace gaia {

struct Quat {
  float w, x, y, z;
};

// Rebuilds a unit quaternion from the DMP's vector part (q1..q3). w is derived as
// sqrt(max(0, 1 - |v|^2)) so rounding overshoot can never produce NaN, then the result
// is normalized. On non-finite input or a degenerate norm, writes identity and returns false.
bool sanitizeQuat(double q1, double q2, double q3, Quat &out);

Quat quatMultiply(const Quat &a, const Quat &b);
Quat quatConjugate(const Quat &q);
// Shortest-path spherical interpolation; t is clamped to [0,1].
Quat quatSlerp(const Quat &a, const Quat &b, float t);
// Smallest rotation angle between two unit quaternions, in degrees [0,180].
float quatAngleDeg(const Quat &a, const Quat &b);

// Reference implementation of the legacy firmware's Euler output (axis remap
// qw=w, qx=y, qy=x, qz=-z, then aerospace formulas). Used by decoder tests only;
// never sent on the wire.
void quatToEulerLegacy(const Quat &q, float &rollDeg, float &pitchDeg, float &yawDeg);

}  // namespace gaia
