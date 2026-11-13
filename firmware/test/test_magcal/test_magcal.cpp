#include <unity.h>

#include <cmath>

#include "../../Gaiaball/src/logic/MagCalibration.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

void test_identity_calibration_is_passthrough() {
  const MagCal c = magCalIdentity();
  TEST_ASSERT_TRUE(c.isIdentity());
  const float in[3] = {12.0f, -30.0f, 45.0f};
  float out[3];
  magApply(c, in, out);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, in[0], out[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, in[1], out[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, in[2], out[2]);
}

void test_hard_iron_offset_is_subtracted() {
  MagCal c = magCalIdentity();
  c.hardIronUt[0] = 20.0f;
  c.hardIronUt[1] = -10.0f;
  c.hardIronUt[2] = 5.0f;
  TEST_ASSERT_FALSE(c.isIdentity());
  const float in[3] = {30.0f, -30.0f, 15.0f};
  float out[3];
  magApply(c, in, out);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, out[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -20.0f, out[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, out[2]);
}

void test_soft_iron_matrix_is_applied_after_the_offset() {
  MagCal c = magCalIdentity();
  c.hardIronUt[0] = 10.0f;
  c.softIron[0] = 2.0f;  // scale X by 2
  c.softIron[4] = 0.5f;  // scale Y by 1/2
  const float in[3] = {20.0f, 40.0f, 0.0f};
  float out[3];
  magApply(c, in, out);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, out[0]);  // (20-10)*2
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, out[1]);  // 40*0.5
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, out[2]);
}

void test_calibration_maps_an_offset_sphere_back_onto_a_sphere() {
  // A 50 uT earth field seen through a +25/-15/+10 uT hard-iron offset and a
  // 1.2x stretch on Y: after calibration every direction must read 50 uT.
  MagCal c = magCalIdentity();
  c.hardIronUt[0] = 25.0f;
  c.hardIronUt[1] = -15.0f;
  c.hardIronUt[2] = 10.0f;
  c.softIron[4] = 1.0f / 1.2f;
  const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                            {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (const auto &d : dirs) {
    const float raw[3] = {50.0f * d[0] + 25.0f, 50.0f * d[1] * 1.2f - 15.0f,
                          50.0f * d[2] + 10.0f};
    float out[3];
    magApply(c, raw, out);
    const float n = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, n);
  }
}

void test_fixed_point_roundtrip() {
  MagCal c = magCalIdentity();
  c.hardIronUt[0] = 12.3f;
  c.hardIronUt[1] = -45.6f;
  c.hardIronUt[2] = 0.7f;
  c.softIron[0] = 1.25f;
  c.softIron[1] = -0.125f;
  int16_t hard[3], soft[9];
  magCalToFixed(c, hard, soft);
  const MagCal back = magCalFromFixed(hard, soft);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 12.3f, back.hardIronUt[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -45.6f, back.hardIronUt[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.7f, back.hardIronUt[2]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.25f, back.softIron[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -0.125f, back.softIron[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, back.softIron[8]);
}

void test_fixed_point_identity_defaults() {
  int16_t hard[3], soft[9];
  magCalToFixed(magCalIdentity(), hard, soft);
  TEST_ASSERT_EQUAL_INT16(0, hard[0]);
  TEST_ASSERT_EQUAL_INT16(kMagSoftIronOne, soft[0]);
  TEST_ASSERT_EQUAL_INT16(0, soft[1]);
  TEST_ASSERT_EQUAL_INT16(kMagSoftIronOne, soft[4]);
  TEST_ASSERT_EQUAL_INT16(kMagSoftIronOne, soft[8]);
  TEST_ASSERT_TRUE(magCalFromFixed(hard, soft).isIdentity());
}

void test_fixed_point_saturates_instead_of_wrapping() {
  MagCal c = magCalIdentity();
  c.hardIronUt[0] = 1e6f;
  c.softIron[0] = 1e6f;
  int16_t hard[3], soft[9];
  magCalToFixed(c, hard, soft);
  TEST_ASSERT_EQUAL_INT16(32767, hard[0]);
  TEST_ASSERT_EQUAL_INT16(32767, soft[0]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_identity_calibration_is_passthrough);
  RUN_TEST(test_hard_iron_offset_is_subtracted);
  RUN_TEST(test_soft_iron_matrix_is_applied_after_the_offset);
  RUN_TEST(test_calibration_maps_an_offset_sphere_back_onto_a_sphere);
  RUN_TEST(test_fixed_point_roundtrip);
  RUN_TEST(test_fixed_point_identity_defaults);
  RUN_TEST(test_fixed_point_saturates_instead_of_wrapping);
  return UNITY_END();
}
