#include <unity.h>

#include <cmath>

#include "../../Gaiaball/src/logic/Ahrs.h"
#include "../../Gaiaball/src/logic/GyroBias.h"
#include "../../Gaiaball/src/logic/QuatMath.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static constexpr float kDt = 0.005f;  // 200 Hz
static constexpr float kRate = 200.0f;

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

// Settings with every optional feature off: the plain complementary filter.
static AhrsSettings plain(float gain) {
  AhrsSettings s;
  s.gain = gain;
  s.sampleRateHz = kRate;
  s.gyroRangeDps = 0.0f;
  s.accelRejectionDeg = 0.0f;
  s.magRejectionDeg = 0.0f;
  s.rejectionTimeoutS = 0.0f;
  return s;
}

static void feed6(Ahrs &f, const float g[3], const float a[3], int n) {
  for (int i = 0; i < n; i++) f.update6(g, a, kDt);
}

// ---------------------------------------------------------------- AHRS basics

void test_level_rest_stays_identity() {
  Ahrs f;
  f.configure(plain(0.5f));
  const float g[3] = {0, 0, 0}, a[3] = {0, 0, 1.0f};
  feed6(f, g, a, 2000);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, f.quat().w);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 1.0f, norm(f.quat()));
}

void test_tilt_converges_gravity_to_earth_z() {
  Ahrs f;
  f.configure(plain(0.5f));
  // Ball resting on its side: gravity measured along body +X.
  const float g[3] = {0, 0, 0}, a[3] = {1.0f, 0.0f, 0.0f};
  feed6(f, g, a, 6000);
  float e[3];
  rotate(f.quat(), a, e);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, e[2]);
}

void test_yaw_integration_90deg() {
  Ahrs f;
  f.configure(plain(0.0f));  // gain 0 = pure gyro integration (and no rejection)
  const float g[3] = {0, 0, 90.0f}, a[3] = {0, 0, 1.0f};
  feed6(f, g, a, 200);  // 200 * 5 ms * 90 dps = 90 deg
  float e[3];
  const float bodyX[3] = {1, 0, 0};
  rotate(f.quat(), bodyX, e);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.0f, e[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, e[2]);
}

void test_norm_stays_unit_with_noise() {
  Ahrs f;
  f.configure(plain(0.5f));
  uint32_t seed = 1;
  auto rnd = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return ((float)(seed >> 8) / 8388608.0f) - 1.0f;  // -1..1
  };
  for (int i = 0; i < 20000; i++) {
    const float g[3] = {6.0f * rnd(), 6.0f * rnd(), 6.0f * rnd()};
    const float a[3] = {0.05f * rnd(), 0.05f * rnd(), 1.0f + 0.05f * rnd()};
    f.update6(g, a, kDt);
    if ((i & 0xFFF) == 0) TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
  }
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
}

void test_update9_zero_mag_falls_back_to_6d() {
  Ahrs f;
  f.configure(plain(0.5f));
  const float g[3] = {0, 0, 0}, a[3] = {0, 0, 1.0f}, m[3] = {0, 0, 0};
  for (int i = 0; i < 500; i++) f.update9(g, a, m, kDt);
  TEST_ASSERT_TRUE(std::isfinite(f.quat().w));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
}

void test_update9_heading_locks_to_mag() {
  Ahrs f;
  f.configure(plain(0.5f));
  // Level ball, earth field pointing to body +X (north aligned with body X).
  const float g[3] = {0, 0, 0}, a[3] = {0, 0, 1.0f}, m[3] = {30.0f, 0.0f, -40.0f};
  for (int i = 0; i < 6000; i++) f.update9(g, a, m, kDt);
  const Quat a1 = f.quat();
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(a1));
  for (int i = 0; i < 2000; i++) f.update9(g, a, m, kDt);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, a1.w, f.quat().w);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, fabsf(a1.z), fabsf(f.quat().z));
}

// ------------------------------------------------------- startup gain ramp

void test_startup_ramp_converges_within_one_second() {
  Ahrs ramped, cold;
  AhrsSettings s = plain(0.5f);
  ramped.configure(s);
  cold.configure(s);
  cold.skipStartup();
  const float g[3] = {0, 0, 0}, a[3] = {1.0f, 0.0f, 0.0f};  // on its side
  feed6(ramped, g, a, 50);  // 0.25 s at 200 Hz
  feed6(cold, g, a, 50);
  float er[3], ec[3];
  rotate(ramped.quat(), a, er);
  rotate(cold.quat(), a, ec);
  // The startup ramp (gain 10 -> 0.5 over 3 s) converges from a 90 deg error inside a
  // quarter of a second; the steady-state gain alone is nowhere near.
  TEST_ASSERT_TRUE(er[2] > 0.9f);
  TEST_ASSERT_TRUE(ec[2] < 0.5f);
}

// --------------------------------------------------- acceleration rejection

void test_acceleration_rejection_ignores_lateral_acceleration() {
  AhrsSettings s = plain(0.5f);
  s.accelRejectionDeg = 10.0f;
  s.rejectionTimeoutS = 5.0f;
  Ahrs guarded;
  guarded.configure(s);
  guarded.skipStartup();
  Ahrs naive;
  naive.configure(plain(0.5f));
  naive.skipStartup();

  const float g[3] = {0, 0, 0};
  const float level[3] = {0, 0, 1.0f};
  feed6(guarded, g, level, 1000);  // settle level
  feed6(naive, g, level, 1000);

  // 1 g of lateral acceleration: the measured vector is 45 deg off vertical.
  const float pushed[3] = {1.0f, 0.0f, 1.0f};
  feed6(guarded, g, pushed, 100);  // 0.5 s, well inside the 5 s recovery timeout
  feed6(naive, g, pushed, 100);

  TEST_ASSERT_TRUE(guarded.accelIgnored());
  TEST_ASSERT_FALSE(naive.accelIgnored());
  float eg[3], en[3];
  rotate(guarded.quat(), level, eg);
  rotate(naive.quat(), level, en);
  // Guarded stays level (earth +Z); naive is dragged towards the resultant.
  TEST_ASSERT_TRUE(eg[2] > 0.999f);
  TEST_ASSERT_TRUE(en[2] < 0.99f);
}

void test_acceleration_recovery_reenables_accelerometer() {
  AhrsSettings s = plain(0.5f);
  s.accelRejectionDeg = 10.0f;
  s.rejectionTimeoutS = 1.0f;  // 200 samples at 200 Hz
  Ahrs f;
  f.configure(s);
  f.skipStartup();
  const float g[3] = {0, 0, 0}, level[3] = {0, 0, 1.0f}, pushed[3] = {1.0f, 0, 1.0f};
  feed6(f, g, level, 400);
  feed6(f, g, pushed, 50);
  TEST_ASSERT_TRUE(f.accelIgnored());  // short disturbance: rejected
  feed6(f, g, pushed, 400);            // prolonged: recovery must take over
  TEST_ASSERT_FALSE(f.accelIgnored());
}

void test_acceleration_error_reported_in_degrees() {
  Ahrs f;
  f.configure(plain(0.5f));
  f.skipStartup();
  const float g[3] = {0, 0, 0}, level[3] = {0, 0, 1.0f};
  feed6(f, g, level, 2000);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, f.accelErrorDeg());
  const float pushed[3] = {1.0f, 0.0f, 1.0f};  // 45 deg off
  f.update6(g, pushed, kDt);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, 45.0f, f.accelErrorDeg());
}

// ------------------------------------------------------- overrange recovery

void test_overrange_restarts_filter() {
  AhrsSettings s = plain(0.5f);
  s.gyroRangeDps = 1000.0f;  // threshold = 980 dps
  Ahrs f;
  f.configure(s);
  f.skipStartup();
  const float slow[3] = {0, 0, 100.0f}, level[3] = {0, 0, 1.0f};
  feed6(f, slow, level, 200);
  TEST_ASSERT_FALSE(f.overrangeRecovery());
  const float clipped[3] = {0, 0, 1500.0f};
  f.update6(clipped, level, kDt);
  TEST_ASSERT_TRUE(f.overrangeRecovery());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
  // Recovery re-runs the startup ramp, so it reconverges and clears the flag.
  feed6(f, slow, level, 2000);
  TEST_ASSERT_FALSE(f.overrangeRecovery());
}

void test_non_finite_input_does_not_poison_state() {
  Ahrs f;
  f.configure(plain(0.5f));
  const float g[3] = {0, 0, 0}, level[3] = {0, 0, 1.0f};
  feed6(f, g, level, 200);
  const float bad[3] = {NAN, 0, 0};
  f.update6(bad, level, kDt);
  f.update6(g, level, kDt);
  TEST_ASSERT_TRUE(std::isfinite(f.quat().w));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, norm(f.quat()));
}

// ----------------------------------------------------------- gyro bias

static GyroBiasSettings biasDefaults() { return GyroBiasSettings{}; }

static void feedBias(GyroBiasTracker &b, const float raw[3], const float *accel,
                     uint32_t &tMs, int n, float outLast[3] = nullptr) {
  float g[3];
  for (int i = 0; i < n; i++) {
    tMs += 5;
    g[0] = raw[0];
    g[1] = raw[1];
    g[2] = raw[2];
    b.correct(g, accel, tMs);
  }
  if (outLast != nullptr) {
    outLast[0] = g[0];
    outLast[1] = g[1];
    outLast[2] = g[2];
  }
}

void test_bias_learns_small_offset_at_rest() {
  GyroBiasTracker b;
  b.configure(biasDefaults(), kRate);
  const float raw[3] = {0.8f, -0.5f, 0.3f};
  const float accel[3] = {0, 0, 1.0f};
  uint32_t tMs = 0;
  float out[3];
  feedBias(b, raw, accel, tMs, 5000, out);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, raw[0], b.bias()[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, raw[1], b.bias()[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, raw[2], b.bias()[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, out[0]);
}

// The old tracker tested stillness on the ALREADY CORRECTED rate against a 2 dps
// threshold, so a unit whose zero-rate offset exceeded that threshold never looked
// still and never adapted (MPU-9250 spec: +-5 dps initial ZRO). This pins the fix.
void test_bias_learns_offset_larger_than_rest_threshold() {
  GyroBiasTracker b;
  b.configure(biasDefaults(), kRate);
  const float raw[3] = {5.0f, -4.0f, 4.5f};  // plausible ZRO, far above 2 dps
  const float accel[3] = {0, 0, 1.0f};
  uint32_t tMs = 0;
  float out[3];
  feedBias(b, raw, accel, tMs, 8000, out);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, raw[0], b.bias()[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, raw[1], b.bias()[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, raw[2], b.bias()[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, 0.0f, out[2]);
}

void test_bias_frozen_while_spinning() {
  GyroBiasTracker b;
  b.configure(biasDefaults(), kRate);
  const float raw[3] = {50.0f, 0.0f, 0.0f};  // steady 50 dps spin
  const float accel[3] = {0, 0, 1.0f};
  uint32_t tMs = 0;
  float out[3];
  feedBias(b, raw, accel, tMs, 4000, out);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.bias()[0]);  // motion never absorbed
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, out[0]);
}

void test_bias_frozen_while_shaken() {
  GyroBiasTracker b;
  b.configure(biasDefaults(), kRate);
  const float accel[3] = {0, 0, 1.0f};
  uint32_t tMs = 0;
  float g[3];
  for (int i = 0; i < 4000; i++) {  // alternating +-6 dps: mean 0, big deviation
    tMs += 5;
    const float s = (i & 1) ? 6.0f : -6.0f;
    g[0] = 1.0f + s;
    g[1] = 0;
    g[2] = 0;
    b.correct(g, accel, tMs);
  }
  TEST_ASSERT_FALSE(b.atRest());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.bias()[0]);
}

void test_bias_frozen_while_accelerating() {
  GyroBiasTracker b;
  b.configure(biasDefaults(), kRate);
  const float raw[3] = {1.0f, 0.0f, 0.0f};
  const float accel[3] = {0.0f, 0.0f, 1.8f};  // 1.8 g: not resting on a table
  uint32_t tMs = 0;
  feedBias(b, raw, accel, tMs, 4000);
  TEST_ASSERT_FALSE(b.atRest());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.bias()[0]);
}

void test_bias_requires_hold_before_adapting() {
  GyroBiasTracker b;
  GyroBiasSettings s = biasDefaults();
  s.restHoldMs = 1000;
  b.configure(s, kRate);
  const float raw[3] = {2.0f, 0.0f, 0.0f};
  const float accel[3] = {0, 0, 1.0f};
  uint32_t tMs = 0;
  feedBias(b, raw, accel, tMs, 100);  // 500 ms: below the hold
  TEST_ASSERT_FALSE(b.adapting());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.bias()[0]);
  feedBias(b, raw, accel, tMs, 200);  // past the hold
  TEST_ASSERT_TRUE(b.adapting());
}

void test_bias_clipped_to_plausible_range() {
  GyroBiasTracker b;
  GyroBiasSettings s = biasDefaults();
  s.maxBiasDps = 6.0f;
  b.configure(s, kRate);
  // A rate above the plausibility bound must never be taken for a bias.
  const float raw[3] = {9.0f, 0.0f, 0.0f};
  const float accel[3] = {0, 0, 1.0f};
  uint32_t tMs = 0;
  feedBias(b, raw, accel, tMs, 8000);
  TEST_ASSERT_TRUE(fabsf(b.bias()[0]) <= 6.0f);
}

void test_bias_accel_optional() {
  GyroBiasTracker b;
  b.configure(biasDefaults(), kRate);
  const float raw[3] = {1.5f, 0.0f, 0.0f};
  uint32_t tMs = 0;
  feedBias(b, raw, nullptr, tMs, 5000);  // no accelerometer gate
  TEST_ASSERT_FLOAT_WITHIN(0.1f, raw[0], b.bias()[0]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_level_rest_stays_identity);
  RUN_TEST(test_tilt_converges_gravity_to_earth_z);
  RUN_TEST(test_yaw_integration_90deg);
  RUN_TEST(test_norm_stays_unit_with_noise);
  RUN_TEST(test_update9_zero_mag_falls_back_to_6d);
  RUN_TEST(test_update9_heading_locks_to_mag);
  RUN_TEST(test_startup_ramp_converges_within_one_second);
  RUN_TEST(test_acceleration_rejection_ignores_lateral_acceleration);
  RUN_TEST(test_acceleration_recovery_reenables_accelerometer);
  RUN_TEST(test_acceleration_error_reported_in_degrees);
  RUN_TEST(test_overrange_restarts_filter);
  RUN_TEST(test_non_finite_input_does_not_poison_state);
  RUN_TEST(test_bias_learns_small_offset_at_rest);
  RUN_TEST(test_bias_learns_offset_larger_than_rest_threshold);
  RUN_TEST(test_bias_frozen_while_spinning);
  RUN_TEST(test_bias_frozen_while_shaken);
  RUN_TEST(test_bias_frozen_while_accelerating);
  RUN_TEST(test_bias_requires_hold_before_adapting);
  RUN_TEST(test_bias_clipped_to_plausible_range);
  RUN_TEST(test_bias_accel_optional);
  return UNITY_END();
}
