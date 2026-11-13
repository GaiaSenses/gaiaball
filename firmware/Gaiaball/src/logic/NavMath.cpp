#include "NavMath.h"

#include <math.h>

namespace gaia {

namespace {
const float kDegToRad = 0.017453292519943295f;
const float kRadToDeg = 57.295779513082320876798f;
const float kMaxJoyDtS = 0.05f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Rotation matrix (acting on column vectors) -> quaternion, branching on the
// largest of trace / diagonal so the square root argument stays well away from 0.
Quat quatFromMatrix(const float m[3][3]) {
  const float tr = m[0][0] + m[1][1] + m[2][2];
  Quat q;
  if (tr > 0.f) {
    const float s = 2.f * sqrtf(1.f + tr);  // s = 4w
    q = Quat{0.25f * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s,
             (m[1][0] - m[0][1]) / s};
  } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
    const float s = 2.f * sqrtf(1.f + m[0][0] - m[1][1] - m[2][2]);  // s = 4x
    q = Quat{(m[2][1] - m[1][2]) / s, 0.25f * s, (m[0][1] + m[1][0]) / s,
             (m[0][2] + m[2][0]) / s};
  } else if (m[1][1] > m[2][2]) {
    const float s = 2.f * sqrtf(1.f + m[1][1] - m[0][0] - m[2][2]);  // s = 4y
    q = Quat{(m[0][2] - m[2][0]) / s, (m[0][1] + m[1][0]) / s, 0.25f * s,
             (m[1][2] + m[2][1]) / s};
  } else {
    const float s = 2.f * sqrtf(1.f + m[2][2] - m[0][0] - m[1][1]);  // s = 4z
    q = Quat{(m[1][0] - m[0][1]) / s, (m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s,
             0.25f * s};
  }
  return quatNormalize(q);
}

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
}  // namespace

Quat navRefRotation(float latDeg, float lonDeg) {
  const float f = latDeg * kDegToRad, l = lonDeg * kDegToRad;
  const float sf = sinf(f), cf = cosf(f), sl = sinf(l), cl = cosf(l);
  // Rows: p, east, north. Row i dotted with a vector gives its component i, so
  // M p = +X, M east = +Y, M north = +Z.
  const float m[3][3] = {{cf * cl, cf * sl, sf}, {-sl, cl, 0.f}, {-sf * cl, -sf * sl, cf}};
  return quatFromMatrix(m);
}

NavPose navPoseFromGlobe(const Quat &G, const Quat &R0) {
  const Quat cInv = quatConjugate(quatNormalize(quatMultiply(G, R0)));
  const float kX[3] = {1.f, 0.f, 0.f}, kZ[3] = {0.f, 0.f, 1.f};
  float p[3], u[3];
  quatRotateVector(cInv, kX, p);  // screen centre, in the globe frame
  quatRotateVector(cInv, kZ, u);  // screen up, in the globe frame

  const float f = asinf(clampf(p[2], -1.f, 1.f));
  const float l = atan2f(p[1], p[0]);
  const float sf = sinf(f), cf = cosf(f), sl = sinf(l), cl = cosf(l);
  const float east[3] = {-sl, cl, 0.f};
  const float north[3] = {-sf * cl, -sf * sl, cf};

  float b = atan2f(dot3(u, east), dot3(u, north)) * kRadToDeg;
  // atan2 is in [-pi, pi]: -180 folds to +180, and a float overshoot past 180
  // is rounding of +pi.
  if (b > 180.f || b <= -180.f) b = 180.f;

  NavPose pose;
  pose.latDeg = f * kRadToDeg;
  pose.lonDeg = l * kRadToDeg;
  pose.bearingDeg = b;
  return pose;
}

float joySpeedDegS(float thetaDeg, const JoyParams &p) {
  if (!(thetaDeg > 0.f)) return 0.f;
  float r = 1.f;
  if (p.fullAngleDeg > 0.f && thetaDeg < p.fullAngleDeg) r = thetaDeg / p.fullAngleDeg;
  return p.maxSpeedDegS * powf(r, p.curve);
}

Quat joystickStep(const Quat &G, const Quat &qDev, float dtS, const JoyParams &p) {
  float axis[3], thetaRad;
  quatToAxisAngle(qDev, axis, thetaRad);
  if (!(thetaRad > 0.f)) return G;
  const float dt = (dtS > 0.f) ? (dtS < kMaxJoyDtS ? dtS : kMaxJoyDtS) : 0.f;
  const float stepRad = joySpeedDegS(thetaRad * kRadToDeg, p) * dt * kDegToRad;
  if (!(stepRad > 0.f)) return G;
  return quatNormalize(quatMultiply(quatFromAxisAngle(axis, stepRad), G));
}

float angularSpeedDegS(const Quat &a, const Quat &b, float dtS) {
  if (!(dtS > 0.f)) return 0.f;
  return quatAngleDeg(a, b) / dtS;
}

}  // namespace gaia
