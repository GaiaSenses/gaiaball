#include <unity.h>
#include <cmath>
#include "../../Gaiaball/src/logic/QuatMath.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static float norm(const Quat &q) {
  return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

void test_sanitize_overflow() {
  // q1^2+q2^2+q3^2 = 1.02 > 1: w would be sqrt(negative) -> must clamp to 0.
  const double s = std::sqrt(1.02 / 3.0);
  Quat q{};
  TEST_ASSERT_TRUE(sanitizeQuat(s, s, s, q));
  TEST_ASSERT_TRUE(std::isfinite(q.w));
  TEST_ASSERT_EQUAL_FLOAT(0.f, q.w);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, norm(q));
}

void test_sanitize_nan() {
  Quat q{0.5f, 0.5f, 0.5f, 0.5f};
  TEST_ASSERT_FALSE(sanitizeQuat(NAN, 0.1, 0.2, q));
  TEST_ASSERT_EQUAL_FLOAT(1.f, q.w);
  TEST_ASSERT_EQUAL_FLOAT(0.f, q.x);
  TEST_ASSERT_EQUAL_FLOAT(0.f, q.y);
  TEST_ASSERT_EQUAL_FLOAT(0.f, q.z);
}

void test_sanitize_normal() {
  Quat q{};
  TEST_ASSERT_TRUE(sanitizeQuat(0.0, 0.0, 0.0, q));
  TEST_ASSERT_EQUAL_FLOAT(1.f, q.w);
  TEST_ASSERT_TRUE(sanitizeQuat(0.5, 0.5, 0.5, q));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, q.w);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, norm(q));
}

void test_zero_ref() {
  Quat ref{};
  sanitizeQuat(0.2, -0.3, 0.4, ref);
  Quat r = quatMultiply(quatConjugate(ref), ref);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, r.w);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, r.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, r.y);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.f, r.z);
}

void test_slerp_endpoints() {
  Quat a{}, b{};
  sanitizeQuat(0.1, 0.2, 0.3, a);
  sanitizeQuat(-0.4, 0.1, 0.5, b);
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

void test_euler_legacy_golden() {
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
  Quat g{};
  sanitizeQuat(0.2, 0.3, 0.4, g);
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
  RUN_TEST(test_sanitize_overflow);
  RUN_TEST(test_sanitize_nan);
  RUN_TEST(test_sanitize_normal);
  RUN_TEST(test_zero_ref);
  RUN_TEST(test_slerp_endpoints);
  RUN_TEST(test_angle_deg);
  RUN_TEST(test_euler_legacy_golden);
  return UNITY_END();
}
