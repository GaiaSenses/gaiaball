#include <unity.h>
#include <cmath>
#include "../../Gaiaball/src/logic/Fusion.h"
#include "../../Gaiaball/src/logic/QuatMath.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static constexpr float kDt = 0.005f;  // 200 Hz
static constexpr float kDeg2Rad = (float)M_PI / 180.0f;

// Rotates a body-frame vector into the earth frame with q (body->earth).
static void rotate(const Quat &q, const float v[3], float out[3]) {
  const Quat p{0, v[0], v[1], v[2]};
  const Quat r = quatMultiply(quatMultiply(q, p), quatConjugate(q));
  out[0] = r.x;
  out[1] = r.y;
  out[2] = r.z;
}

static float norm(const Quat &q) {
  return sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

void test_level_rest_stays_identity() {
  MadgwickFusion f;
  f.configure(0.1f);
  for (int i = 0; i < 2000; i++) f.update6(0, 0, 0, 0, 0, 1.0f, kDt);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, f.quat().w);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, norm(f.quat()));
}

void test_tilt_converges_gravity_to_earth_z() {
  MadgwickFusion f;
  f.configure(0.5f);  // fast gain: converge within the test budget
  // Ball resting on its side: gravity measured along body +X.
  const float aBody[3] = {1.0f, 0.0f, 0.0f};
  for (int i = 0; i < 6000; i++) f.update6(0, 0, 0, aBody[0], aBody[1], aBody[2], kDt);
  float e[3];
  rotate(f.quat(), aBody, e);
  // body->earth must map the measured gravity onto earth +Z.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, e[2]);
}

void test_yaw_integration_90deg() {
  MadgwickFusion f;
  f.configure(0.0f);  // pure gyro integration for an exact check
  const float rate = 90.0f * kDeg2Rad;  // +90 deg/s about body Z
  for (int i = 0; i < 200; i++) f.update6(0, 0, rate, 0, 0, 1.0f, kDt);
  float e[3];
  const float bodyX[3] = {1, 0, 0};
  rotate(f.quat(), bodyX, e);
  // Body X should have swung 90 degrees in the earth XY plane (onto +Y for a
  // right-handed +Z rotation), staying level.
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, e[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[2]);
}

void test_norm_stays_unit_with_noise() {
  MadgwickFusion f;
  f.configure(0.1f);
  uint32_t seed = 1;
  auto rnd = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return ((float)(seed >> 8) / 8388608.0f) - 1.0f;  // -1..1
  };
  for (int i = 0; i < 20000; i++) {
    f.update6(0.1f * rnd(), 0.1f * rnd(), 0.1f * rnd(), 0.05f * rnd(), 0.05f * rnd(),
              1.0f + 0.05f * rnd(), kDt);
    if ((i & 0xFFF) == 0) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
}

void test_update9_zero_mag_falls_back() {
  MadgwickFusion f;
  f.configure(0.1f);
  for (int i = 0; i < 500; i++) f.update9(0, 0, 0, 0, 0, 1.0f, 0, 0, 0, kDt);
  TEST_ASSERT_TRUE(std::isfinite(f.quat().w));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
}

void test_update9_heading_locks_to_mag() {
  MadgwickFusion f;
  f.configure(0.5f);
  // Level ball, earth field pointing to body +X (north aligned with body X).
  for (int i = 0; i < 6000; i++) f.update9(0, 0, 0, 0, 0, 1.0f, 0.3f, 0.0f, -0.4f, kDt);
  const Quat a = f.quat();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(a));
  // Keep feeding the same field: the heading must be stable (no drift).
  for (int i = 0; i < 2000; i++) f.update9(0, 0, 0, 0, 0, 1.0f, 0.3f, 0.0f, -0.4f, kDt);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, a.w, f.quat().w);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, fabsf(a.z), fabsf(f.quat().z));
}

void test_bias_tracker_learns_offset_at_rest() {
  GyroBiasTracker b;
  const float offset[3] = {0.8f, -0.5f, 0.3f};
  float g[3];
  uint32_t tMs = 0;
  for (int i = 0; i < 5000; i++) {
    tMs += 5;
    g[0] = offset[0];
    g[1] = offset[1];
    g[2] = offset[2];
    b.correct(g, tMs);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.05f, offset[0], b.bias()[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, offset[1], b.bias()[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, offset[2], b.bias()[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, g[0]);  // corrected output ~0 at rest
}

void test_bias_tracker_frozen_while_moving() {
  GyroBiasTracker b;
  float g[3];
  uint32_t tMs = 0;
  for (int i = 0; i < 2000; i++) {  // spinning: 50 dps
    tMs += 5;
    g[0] = 50.0f;
    g[1] = 0.0f;
    g[2] = 0.0f;
    b.correct(g, tMs);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.bias()[0]);  // never absorbed the motion
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, g[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_level_rest_stays_identity);
  RUN_TEST(test_tilt_converges_gravity_to_earth_z);
  RUN_TEST(test_yaw_integration_90deg);
  RUN_TEST(test_norm_stays_unit_with_noise);
  RUN_TEST(test_update9_zero_mag_falls_back);
  RUN_TEST(test_update9_heading_locks_to_mag);
  RUN_TEST(test_bias_tracker_learns_offset_at_rest);
  RUN_TEST(test_bias_tracker_frozen_while_moving);
  return UNITY_END();
}
