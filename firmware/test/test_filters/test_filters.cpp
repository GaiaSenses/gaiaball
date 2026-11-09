#include <unity.h>

#include <cmath>

#include "../../Gaiaball/src/logic/Filters.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

void test_ema_converges() {
  Ema<float> e;
  e.setAlpha(0.2f);
  float out = 0.f;
  for (int i = 0; i < 100; i++) out = e.update(5.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.f, out);
}

void test_ema_first_sample_seeds() {
  Ema<float> e;
  e.setAlpha(0.1f);
  TEST_ASSERT_EQUAL_FLOAT(42.f, e.update(42.f));
}

// ------------------------------------------------------------- Hampel spike

void test_hampel_passes_through_until_window_fills() {
  Hampel5<int16_t> h;
  h.setK(3.0f);
  TEST_ASSERT_EQUAL_INT16(100, h.update(100));
  TEST_ASSERT_EQUAL_INT16(900, h.update(900));  // no window yet: nothing to judge
}

void test_hampel_replaces_single_spike() {
  Hampel5<int16_t> h;
  h.setK(3.0f);
  for (int i = 0; i < 10; i++) h.update(static_cast<int16_t>(100 + (i & 1)));
  const int16_t out = h.update(5000);  // one-sample spike
  TEST_ASSERT_INT16_WITHIN(2, 100, out);
}

void test_hampel_keeps_genuine_ramp() {
  Hampel5<int16_t> h;
  h.setK(3.0f);
  int16_t out = 0;
  for (int16_t v = 0; v < 2000; v = static_cast<int16_t>(v + 50)) out = h.update(v);
  TEST_ASSERT_EQUAL_INT16(1950, out);  // steady motion must survive untouched
}

void test_hampel_recovers_after_step() {
  Hampel5<int16_t> h;
  h.setK(3.0f);
  for (int i = 0; i < 10; i++) h.update(100);
  h.update(4000);  // first sample of a sustained step: rejected as a spike
  int16_t out = 0;
  for (int i = 0; i < 5; i++) out = h.update(4000);
  TEST_ASSERT_EQUAL_INT16(4000, out);  // the step wins once it is the new normal
}

void test_hampel_k_zero_disables_rejection() {
  Hampel5<int16_t> h;
  h.setK(0.0f);
  for (int i = 0; i < 10; i++) h.update(100);
  TEST_ASSERT_EQUAL_INT16(5000, h.update(5000));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ema_converges);
  RUN_TEST(test_ema_first_sample_seeds);
  RUN_TEST(test_hampel_passes_through_until_window_fills);
  RUN_TEST(test_hampel_replaces_single_spike);
  RUN_TEST(test_hampel_keeps_genuine_ramp);
  RUN_TEST(test_hampel_recovers_after_step);
  RUN_TEST(test_hampel_k_zero_disables_rejection);
  return UNITY_END();
}
