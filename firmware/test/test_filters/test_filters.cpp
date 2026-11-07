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

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ema_converges);
  RUN_TEST(test_ema_first_sample_seeds);
  return UNITY_END();
}
