#include <unity.h>
#include "../../Gaiaball/src/logic/PotMapping.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

void test_pot_raw_to_dbfs() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -60.0f, potRawToDbfs(0));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, potRawToDbfs(4095));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -30.0f, potRawToDbfs(2047.5f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -60.0f, potRawToDbfs(-5));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, potRawToDbfs(5000));
}

void test_pot_db_changed() {
  TEST_ASSERT_FALSE(potDbChanged(-30.4f, -30.0f));
  TEST_ASSERT_TRUE(potDbChanged(-30.5f, -30.0f));
  TEST_ASSERT_TRUE(potDbChanged(-29.5f, -30.0f));
  TEST_ASSERT_FALSE(potDbChanged(-30.0f, -30.0f));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_pot_raw_to_dbfs);
  RUN_TEST(test_pot_db_changed);
  return UNITY_END();
}
