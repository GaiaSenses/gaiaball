#include <unity.h>

void setUp() {}
void tearDown() {}

void test_arithmetic() { TEST_ASSERT_EQUAL(4, 2 + 2); }

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_arithmetic);
  return UNITY_END();
}
