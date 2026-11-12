#include <unity.h>

#include <initializer_list>

#include "../../Gaiaball/src/logic/ImuFrontEnd.h"

using namespace gaia;

void setUp(void) {}
void tearDown(void) {}

void test_mpu_auto_dlpf_matches_legacy(void) {  // preserves the shipped behaviour
  const FrontEndPlan p = planFrontEnd(ImuChip::Mpu9250, 50, 0);
  TEST_ASSERT_EQUAL_UINT8(4, p.dlpfMode);  // 20 Hz
  TEST_ASSERT_EQUAL_FLOAT(20.0f, p.cutoffHz);
  TEST_ASSERT_EQUAL_UINT16(19, p.gyroDiv);  // 1000/50 - 1
  TEST_ASSERT_EQUAL_UINT16(p.gyroDiv, p.accelDiv);
  TEST_ASSERT_EQUAL_FLOAT(50.0f, p.actualHz);
}

void test_mpu_legacy_extremes(void) {
  // Rates below 4 Hz are clamped; below ~11 Hz the narrowest filter (5 Hz) is used.
  const FrontEndPlan lo = planFrontEnd(ImuChip::Mpu9250, 1, 0);
  TEST_ASSERT_EQUAL_UINT16(249, lo.gyroDiv);  // 4 Hz -> 1000/4 - 1
  TEST_ASSERT_EQUAL_UINT8(6, lo.dlpfMode);
  const FrontEndPlan hi = planFrontEnd(ImuChip::Mpu9250, 200, 0);
  TEST_ASSERT_EQUAL_UINT8(2, hi.dlpfMode);  // limit 94 Hz: 184 too wide, 92 Hz fits
}

void test_icm_divisors_at_rate_extremes(void) {
  for (uint8_t hz : {10, 200}) {
    const FrontEndPlan p = planFrontEnd(ImuChip::Icm20948, hz, 0);
    TEST_ASSERT_TRUE(p.gyroDiv <= 0xFF);    // GYRO_SMPLRT_DIV is u8
    TEST_ASSERT_TRUE(p.accelDiv <= 0x0FFF);  // ACCEL_SMPLRT_DIV: 12 bits
    TEST_ASSERT_TRUE(p.actualHz > 0.0f);
  }
  const FrontEndPlan p200 = planFrontEnd(ImuChip::Icm20948, 200, 0);
  TEST_ASSERT_TRUE(p200.cutoffHz <= 0.5f * p200.actualHz);  // never above Nyquist
  TEST_ASSERT_EQUAL_UINT16(5, p200.gyroDiv);                // (1125+100)/200 - 1
  TEST_ASSERT_EQUAL_UINT16(p200.gyroDiv, p200.accelDiv);
}

void test_icm_lowest_rate_uses_narrowest_filter(void) {
  // At 10 Hz even the narrowest ICM filter (5.7 Hz) is above Nyquist (~5 Hz): the chip
  // offers nothing narrower, so the plan must fall back to it rather than invent a value.
  const FrontEndPlan p = planFrontEnd(ImuChip::Icm20948, 10, 0);
  TEST_ASSERT_EQUAL_UINT8(6, p.dlpfMode);
  TEST_ASSERT_EQUAL_FLOAT(5.7f, p.cutoffHz);
  TEST_ASSERT_EQUAL_UINT16(112, p.gyroDiv);
}

void test_cutoff_never_above_nyquist_from_20hz_up(void) {
  // Holds for both chips from 20 Hz up. Below ~12 Hz it cannot (see ImuFrontEnd.h).
  for (ImuChip chip : {ImuChip::Mpu9250, ImuChip::Icm20948}) {
    for (uint8_t hz : {20, 50, 100, 200}) {
      const FrontEndPlan p = planFrontEnd(chip, hz, 0);
      TEST_ASSERT_TRUE(p.cutoffHz <= 0.5f * p.actualHz);
    }
  }
}

void test_forced_dlpf_is_respected(void) {
  TEST_ASSERT_EQUAL_UINT8(2, planFrontEnd(ImuChip::Icm20948, 50, 2).dlpfMode);
  TEST_ASSERT_EQUAL_UINT8(7, planFrontEnd(ImuChip::Mpu9250, 50, 7).dlpfMode);
}

void test_cutoff_tables_differ_between_chips(void) {
  TEST_ASSERT_NOT_EQUAL(dlpfCutoffHzFor(ImuChip::Mpu9250, 3),
                        dlpfCutoffHzFor(ImuChip::Icm20948, 3));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_mpu_auto_dlpf_matches_legacy);
  RUN_TEST(test_mpu_legacy_extremes);
  RUN_TEST(test_icm_divisors_at_rate_extremes);
  RUN_TEST(test_icm_lowest_rate_uses_narrowest_filter);
  RUN_TEST(test_cutoff_never_above_nyquist_from_20hz_up);
  RUN_TEST(test_forced_dlpf_is_respected);
  RUN_TEST(test_cutoff_tables_differ_between_chips);
  return UNITY_END();
}
