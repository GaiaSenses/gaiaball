#include <unity.h>

#include <cmath>

#include "../../Gaiaball/src/logic/QuatMath.h"
#include "../../Gaiaball/src/logic/QuatSmoother.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static constexpr float kDt = 0.02f;  // 50 Hz

// Rotation of `deg` about body Z.
static Quat yaw(float deg) {
  const float h = deg * 0.5f * (float)M_PI / 180.0f;
  return Quat{cosf(h), 0.0f, 0.0f, sinf(h)};
}

void test_off_passes_through() {
  QuatSmoother s;
  s.configure(QuatSmoothMode::Off, 0, 1.0f, 0.0f);
  const Quat in = yaw(30.0f);
  const Quat out = s.update(in, kDt);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, in.z, out.z);
}

void test_first_sample_seeds_without_glide() {
  QuatSmoother s;
  s.configure(QuatSmoothMode::OneEuro, 0, 1.0f, 0.0f);
  const Quat in = yaw(90.0f);
  const Quat out = s.update(in, kDt);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, in.z, out.z);  // no ramp from identity
}

void test_slerp_fixed_lags_by_configured_amount() {
  QuatSmoother s;
  s.configure(QuatSmoothMode::SlerpFixed, 100, 1.0f, 0.0f);  // heaviest legacy setting
  s.update(yaw(0.0f), kDt);
  const Quat out = s.update(yaw(10.0f), kDt);
  // amount 100 -> slerp t = 0.1, so one step covers about a tenth of the gap.
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 1.0f, quatAngleDeg(yaw(0.0f), out));
}

void test_one_euro_attenuates_jitter_at_rest() {
  QuatSmoother s;
  s.configure(QuatSmoothMode::OneEuro, 0, 1.0f, 0.0f);
  Quat out{1, 0, 0, 0};
  for (int i = 0; i < 200; i++) out = s.update(yaw((i & 1) ? 0.6f : -0.6f), kDt);
  // 1.2 deg peak-to-peak of jitter must come out much smaller.
  TEST_ASSERT_TRUE(quatAngleDeg(yaw(0.0f), out) < 0.3f);
}

void test_one_euro_follows_fast_motion_better_than_fixed_cutoff() {
  QuatSmoother adaptive, fixed;
  adaptive.configure(QuatSmoothMode::OneEuro, 0, 1.0f, 0.02f);
  fixed.configure(QuatSmoothMode::OneEuro, 0, 1.0f, 0.0f);  // beta 0 = plain low-pass
  Quat la{1, 0, 0, 0}, lf{1, 0, 0, 0};
  float truth = 0.0f;
  for (int i = 0; i < 100; i++) {  // 180 deg/s for 2 s
    truth += 180.0f * kDt;
    la = adaptive.update(yaw(truth), kDt);
    lf = fixed.update(yaw(truth), kDt);
  }
  const float lagAdaptive = quatAngleDeg(yaw(truth), la);
  const float lagFixed = quatAngleDeg(yaw(truth), lf);
  TEST_ASSERT_TRUE(lagAdaptive < lagFixed);
  TEST_ASSERT_TRUE(lagAdaptive < 10.0f);
}

void test_reset_drops_history() {
  QuatSmoother s;
  s.configure(QuatSmoothMode::OneEuro, 0, 1.0f, 0.0f);
  s.update(yaw(0.0f), kDt);
  s.reset();
  const Quat in = yaw(45.0f);
  const Quat out = s.update(in, kDt);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, in.z, out.z);  // jumps straight to the new reference
}

void test_zero_dt_is_safe() {
  QuatSmoother s;
  s.configure(QuatSmoothMode::OneEuro, 0, 1.0f, 0.02f);
  s.update(yaw(0.0f), kDt);
  const Quat out = s.update(yaw(5.0f), 0.0f);
  TEST_ASSERT_TRUE(std::isfinite(out.w));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f,
                           sqrtf(out.w * out.w + out.x * out.x + out.y * out.y +
                                 out.z * out.z));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_off_passes_through);
  RUN_TEST(test_first_sample_seeds_without_glide);
  RUN_TEST(test_slerp_fixed_lags_by_configured_amount);
  RUN_TEST(test_one_euro_attenuates_jitter_at_rest);
  RUN_TEST(test_one_euro_follows_fast_motion_better_than_fixed_cutoff);
  RUN_TEST(test_reset_drops_history);
  RUN_TEST(test_zero_dt_is_safe);
  return UNITY_END();
}
