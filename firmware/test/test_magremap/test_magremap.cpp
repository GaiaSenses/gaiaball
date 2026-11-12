#include <unity.h>

#include <cstring>

#include "../../Gaiaball/src/logic/MagAxisRemap.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

namespace {
// Determinant of the 3x3 matrix whose column j is the remap of the unit vector e_j.
float remapDeterminant(uint8_t index) {
  float m[3][3];
  for (int j = 0; j < 3; ++j) {
    float e[3] = {0, 0, 0}, o[3];
    e[j] = 1.0f;
    applyMagRemap(index, e, o);
    for (int i = 0; i < 3; ++i) m[i][j] = o[i];
  }
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
         m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}
}  // namespace

void test_default_reproduces_current_behaviour(void) {
  // The literal ImuService used before the table existed: {uT[1], uT[0], -uT[2]}.
  const float in[3] = {1.0f, 2.0f, 3.0f}, want[3] = {2.0f, 1.0f, -3.0f};
  float out[3];
  applyMagRemap(kMagRemapDefault, in, out);
  TEST_ASSERT_EQUAL_FLOAT_ARRAY(want, out, 3);
  TEST_ASSERT_EQUAL_STRING("+Y +X -Z", magRemapLabel(kMagRemapDefault));
}

void test_index_zero_is_identity(void) {
  const float in[3] = {1.0f, 2.0f, 3.0f};
  float out[3];
  applyMagRemap(0, in, out);
  TEST_ASSERT_EQUAL_FLOAT_ARRAY(in, out, 3);
  TEST_ASSERT_EQUAL_STRING("+X +Y +Z", magRemapLabel(0));
}

void test_all_24_are_right_handed(void) {  // determinant +1: no mirroring
  for (uint8_t i = 0; i < kMagRemapCount; ++i) TEST_ASSERT_EQUAL_FLOAT(1.0f, remapDeterminant(i));
}

void test_all_24_are_distinct(void) {
  const float in[3] = {1.0f, 2.0f, 3.0f};  // distinct magnitudes: every perm/sign gives a unique vector
  float outs[kMagRemapCount][3];
  for (uint8_t i = 0; i < kMagRemapCount; ++i) applyMagRemap(i, in, outs[i]);
  for (uint8_t i = 0; i < kMagRemapCount; ++i)
    for (uint8_t j = i + 1; j < kMagRemapCount; ++j)
      TEST_ASSERT_TRUE_MESSAGE(std::memcmp(outs[i], outs[j], sizeof outs[i]) != 0, "duplicate remap");
}

void test_labels_are_unique_and_describe_the_mapping(void) {
  const float in[3] = {1.0f, 2.0f, 3.0f};
  for (uint8_t i = 0; i < kMagRemapCount; ++i) {
    const char *l = magRemapLabel(i);
    TEST_ASSERT_EQUAL_UINT(8, std::strlen(l));
    // Parse "<s><A> <s><B> <s><C>" and check it predicts applyMagRemap.
    float want[3], out[3];
    for (int k = 0; k < 3; ++k) {
      const char s = l[k * 3], a = l[k * 3 + 1];
      TEST_ASSERT_TRUE(s == '+' || s == '-');
      TEST_ASSERT_TRUE(a == 'X' || a == 'Y' || a == 'Z');
      want[k] = (s == '-' ? -1.0f : 1.0f) * in[a - 'X'];
    }
    applyMagRemap(i, in, out);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(want, out, 3);
    for (uint8_t j = i + 1; j < kMagRemapCount; ++j) TEST_ASSERT_TRUE(std::strcmp(l, magRemapLabel(j)) != 0);
  }
}

void test_out_of_range_falls_back_to_default(void) {
  float a[3], b[3];
  const float in[3] = {1, 2, 3};
  applyMagRemap(99, in, a);
  applyMagRemap(kMagRemapDefault, in, b);
  TEST_ASSERT_EQUAL_FLOAT_ARRAY(b, a, 3);
  TEST_ASSERT_EQUAL_STRING(magRemapLabel(kMagRemapDefault), magRemapLabel(kMagRemapCount));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_default_reproduces_current_behaviour);
  RUN_TEST(test_index_zero_is_identity);
  RUN_TEST(test_all_24_are_right_handed);
  RUN_TEST(test_all_24_are_distinct);
  RUN_TEST(test_labels_are_unique_and_describe_the_mapping);
  RUN_TEST(test_out_of_range_falls_back_to_default);
  return UNITY_END();
}
