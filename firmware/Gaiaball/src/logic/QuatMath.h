#pragma once

namespace gaia {

struct Quat {
  float w, x, y, z;
};

Quat quatMultiply(const Quat &a, const Quat &b);
Quat quatConjugate(const Quat &q);
// Shortest-path spherical interpolation; t is clamped to [0,1].
Quat quatSlerp(const Quat &a, const Quat &b, float t);
// Smallest rotation angle between two unit quaternions, in degrees [0,180].
float quatAngleDeg(const Quat &a, const Quat &b);

// Executable specification of the legacy Euler convention (axis remap qw=w, qx=y,
// qy=x, qz=-z, then aerospace formulas), kept on purpose with no production caller:
// the firmware never sends Euler. The TS decoders (site ble-decode.ts and the
// config-app euler.ts) must produce this exact output for the same quaternion —
// that parity is what keeps the Pd patch contract intact, and test_quat pins it.
void quatToEulerLegacy(const Quat &q, float &rollDeg, float &pitchDeg, float &yawDeg);

}  // namespace gaia
