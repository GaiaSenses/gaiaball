#include <unity.h>
#include <cmath>
#include "../../Gaiaball/src/logic/QuatMath.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static float norm(const Quat &q) {
  return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

// Builds a unit quaternion from a vector part (w derived as sqrt(1 - |v|^2)).
// Test fixture helper only; callers must keep |v| <= 1.
static Quat makeQuat(double q1, double q2, double q3) {
  const double v2 = q1 * q1 + q2 * q2 + q3 * q3;
  const double w = std::sqrt(v2 < 1.0 ? 1.0 - v2 : 0.0);
  const double n = std::sqrt(w * w + v2);
  return Quat{(float)(w / n), (float)(q1 / n), (float)(q2 / n), (float)(q3 / n)};
}

void test_zero_ref() {
  const Quat ref = makeQuat(0.2, -0.3, 0.4);
  Quat r = quatMultiply(quatConjugate(ref), ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, r.w);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, r.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, r.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, r.z);
}

void test_slerp_endpoints() {
  const Quat a = makeQuat(0.1, 0.2, 0.3);
  const Quat b = makeQuat(-0.4, 0.1, 0.5);
  Quat s0 = quatSlerp(a, b, 0.f);
  Quat s1 = quatSlerp(a, b, 1.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, quatAngleDeg(a, s0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, quatAngleDeg(b, s1));
  Quat mid = quatSlerp(a, b, 0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, norm(mid));
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, quatAngleDeg(a, b) / 2.f, quatAngleDeg(a, mid));
}

void test_angle_deg() {
  const Quat id{1.f, 0.f, 0.f, 0.f};
  const float h = std::sqrt(0.5f);
  const Quat yaw90{h, 0.f, 0.f, h};
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.f, quatAngleDeg(id, yaw90));
  // q and -q are the same rotation.
  const Quat neg{-1.f, 0.f, 0.f, 0.f};
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, quatAngleDeg(id, neg));
}

// CONTRACT TEST: quatToEulerLegacy is the executable spec of the legacy Euler
// convention. The TS decoders (site ble-decode.ts, config-app euler.ts) must match
// this output exactly — it is what the Pd patch has always received.
void test_euler_legacy_contract() {
  // 90 deg about the sensor Z axis: (w,x,y,z) = (cos45, 0, 0, sin45).
  // Legacy remap: qw=w, qx=y=0, qy=x=0, qz=-z=-0.7071.
  // roll = atan2(0, 1) = 0; pitch = asin(0) = 0;
  // yaw = atan2(2*(0.7071*-0.7071), 1-2*(0.5)) = atan2(-1, 0) = -90.
  const float h = std::sqrt(0.5f);
  const Quat q{h, 0.f, 0.f, h};
  float r = 99.f, p = 99.f, y = 99.f;
  quatToEulerLegacy(q, r, p, y);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -90.f, y);

  // Non-trivial quat, expected computed independently with the legacy formula.
  const Quat g = makeQuat(0.2, 0.3, 0.4);
  const double qw = g.w, qx = g.y, qy = g.x, qz = -g.z;
  const double kRad = 180.0 / M_PI;
  const double er = std::atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy)) * kRad;
  const double ep = std::asin(2 * (qw * qy - qx * qz)) * kRad;
  const double ey = std::atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) * kRad;
  quatToEulerLegacy(g, r, p, y);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)er, r);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)ep, p);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, (float)ey, y);
}

static void assertVec(const float exp[3], const float got[3], float tol) {
  TEST_ASSERT_FLOAT_WITHIN(tol, exp[0], got[0]);
  TEST_ASSERT_FLOAT_WITHIN(tol, exp[1], got[1]);
  TEST_ASSERT_FLOAT_WITHIN(tol, exp[2], got[2]);
}

// Half-angle quaternion about a principal axis, built by hand so these tests do
// not depend on quatFromAxisAngle.
static Quat rotZ(double deg) {
  const double h = deg * M_PI / 360.0;
  return Quat{(float)std::cos(h), 0.f, 0.f, (float)std::sin(h)};
}
static Quat rotY(double deg) {
  const double h = deg * M_PI / 360.0;
  return Quat{(float)std::cos(h), 0.f, (float)std::sin(h), 0.f};
}
static Quat rotX(double deg) {
  const double h = deg * M_PI / 360.0;
  return Quat{(float)std::cos(h), (float)std::sin(h), 0.f, 0.f};
}

void test_normalize() {
  Quat n = quatNormalize(Quat{2.f, 0.f, 0.f, 0.f});
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.f, n.w);
  n = quatNormalize(Quat{1.f, 1.f, 1.f, 1.f});
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, n.w);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, n.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, n.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, n.z);
  // Zero length has no direction: identity, never NaN.
  n = quatNormalize(Quat{0.f, 0.f, 0.f, 0.f});
  TEST_ASSERT_EQUAL_FLOAT(1.f, n.w);
  TEST_ASSERT_EQUAL_FLOAT(0.f, n.x);
  TEST_ASSERT_EQUAL_FLOAT(0.f, n.y);
  TEST_ASSERT_EQUAL_FLOAT(0.f, n.z);
}

void test_rotate_vector_principal_axes() {
  const float X[3] = {1.f, 0.f, 0.f}, Y[3] = {0.f, 1.f, 0.f}, Z[3] = {0.f, 0.f, 1.f};
  const float negZ[3] = {0.f, 0.f, -1.f};
  float out[3];
  quatRotateVector(rotZ(90), X, out);  // right-handed about +Z: X -> Y
  assertVec(Y, out, 1e-6f);
  quatRotateVector(rotY(90), X, out);  // about +Y: X -> -Z
  assertVec(negZ, out, 1e-6f);
  quatRotateVector(rotX(90), Y, out);  // about +X: Y -> Z
  assertVec(Z, out, 1e-6f);
}

void test_rotate_vector_matches_sandwich_and_alias_safe() {
  const Quat q = makeQuat(0.3, -0.2, 0.5);
  const float v[3] = {0.4f, -1.3f, 2.2f};
  const Quat pv{0.f, v[0], v[1], v[2]};
  const Quat s = quatMultiply(quatMultiply(q, pv), quatConjugate(q));
  const float exp[3] = {s.x, s.y, s.z};
  float out[3];
  quatRotateVector(q, v, out);
  assertVec(exp, out, 1e-5f);
  // out == v must work (inputs read before any write).
  float inout[3] = {v[0], v[1], v[2]};
  quatRotateVector(q, inout, inout);
  assertVec(exp, inout, 1e-5f);
}

void test_from_axis_angle() {
  const float axis[3] = {0.f, 0.f, 2.f};  // not unit: normalised inside
  const Quat q = quatFromAxisAngle(axis, (float)(M_PI / 2.0));
  const Quat e = rotZ(90);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, e.w, q.w);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, e.x, q.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, e.y, q.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, e.z, q.z);
  const float zero[3] = {0.f, 0.f, 0.f};
  const Quat id = quatFromAxisAngle(zero, 1.f);
  TEST_ASSERT_EQUAL_FLOAT(1.f, id.w);
  TEST_ASSERT_EQUAL_FLOAT(0.f, id.x);
  TEST_ASSERT_EQUAL_FLOAT(0.f, id.y);
  TEST_ASSERT_EQUAL_FLOAT(0.f, id.z);
}

void test_to_axis_angle_round_trip() {
  const double n = std::sqrt(14.0);
  const float axis[3] = {(float)(1.0 / n), (float)(-2.0 / n), (float)(3.0 / n)};
  const Quat q = quatFromAxisAngle(axis, 1.2f);
  float a[3];
  float ang = -1.f;
  quatToAxisAngle(q, a, ang);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.2f, ang);
  assertVec(axis, a, 1e-5f);
  // -q is the same rotation: same axis and angle.
  quatToAxisAngle(Quat{-q.w, -q.x, -q.y, -q.z}, a, ang);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.2f, ang);
  assertVec(axis, a, 1e-5f);
}

void test_to_axis_angle_shortest_path() {
  // 270 deg about +Z has w < 0; the shortest path is 90 deg about -Z.
  float a[3];
  float ang = -1.f;
  quatToAxisAngle(rotZ(270), a, ang);
  const float negZ[3] = {0.f, 0.f, -1.f};
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, (float)(M_PI / 2.0), ang);
  assertVec(negZ, a, 1e-5f);
  // Exactly half a turn: angle pi.
  quatToAxisAngle(Quat{0.f, 1.f, 0.f, 0.f}, a, ang);
  const float X[3] = {1.f, 0.f, 0.f};
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)M_PI, ang);
  assertVec(X, a, 1e-6f);
}

void test_to_axis_angle_identity() {
  float a[3] = {9.f, 9.f, 9.f};
  float ang = 9.f;
  quatToAxisAngle(Quat{1.f, 0.f, 0.f, 0.f}, a, ang);
  const float X[3] = {1.f, 0.f, 0.f};
  TEST_ASSERT_EQUAL_FLOAT(0.f, ang);
  assertVec(X, a, 0.f);
  quatToAxisAngle(Quat{-1.f, 0.f, 0.f, 0.f}, a, ang);
  TEST_ASSERT_EQUAL_FLOAT(0.f, ang);
  assertVec(X, a, 0.f);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_ref);
  RUN_TEST(test_slerp_endpoints);
  RUN_TEST(test_angle_deg);
  RUN_TEST(test_euler_legacy_contract);
  RUN_TEST(test_normalize);
  RUN_TEST(test_rotate_vector_principal_axes);
  RUN_TEST(test_rotate_vector_matches_sandwich_and_alias_safe);
  RUN_TEST(test_from_axis_angle);
  RUN_TEST(test_to_axis_angle_round_trip);
  RUN_TEST(test_to_axis_angle_shortest_path);
  RUN_TEST(test_to_axis_angle_identity);
  return UNITY_END();
}
