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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_ref);
  RUN_TEST(test_slerp_endpoints);
  RUN_TEST(test_angle_deg);
  RUN_TEST(test_euler_legacy_contract);
  return UNITY_END();
}
