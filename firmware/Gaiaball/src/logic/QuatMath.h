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

// Unit-length copy of q; a zero-length (or non-finite) q returns identity.
Quat quatNormalize(const Quat &q);
// Rotates v by unit q (q v q*). Inputs are read before writing, so out may alias v.
void quatRotateVector(const Quat &q, const float v[3], float out[3]);
// Right-handed rotation of angleRad about axis (normalised inside). A zero axis
// returns identity.
Quat quatFromAxisAngle(const float axis[3], float angleRad);
// Shortest-path axis-angle of q: angleRad in [0, pi] (q and -q give the same
// result). A rotation of ~0 returns axis (1,0,0) and angle 0. q need not be unit.
void quatToAxisAngle(const Quat &q, float axis[3], float &angleRad);

// Executable specification of the legacy Euler convention (axis remap qw=w, qx=y,
// qy=x, qz=-z, then aerospace formulas), kept on purpose with no production caller:
// the firmware never sends Euler. The TS decoders (site ble-decode.ts and the
// config-app euler.ts) must produce this exact output for the same quaternion —
// that parity is what keeps the Pd patch contract intact, and test_quat pins it.
void quatToEulerLegacy(const Quat &q, float &rollDeg, float &pitchDeg, float &yawDeg);

}  // namespace gaia
