#include <unity.h>
#include <cstdint>
#include <cstring>
#include "../../Gaiaball/src/logic/AnchorCommand.h"
#include "../../Gaiaball/src/logic/CommandGate.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

// Golden vectors (binding: the GaiaSenses site builds the same bytes). Full command
// writes: opcode 0x17, then the argument the parser sees.
static const uint8_t kSaoPaulo[11] = {0x17, 0xc8, 0xa5, 0x98, 0xfe, 0xa4,
                                      0x6e, 0x38, 0xfd, 0xd2, 0x04};
static const uint8_t kParis[11] = {0x17, 0x18, 0x7e, 0xe9, 0x02, 0x48,
                                   0xe4, 0x23, 0x00, 0xd8, 0xdc};

static void putLe(uint8_t *dst, uint32_t v, int n) {
  for (int i = 0; i < n; i++) dst[i] = (uint8_t)(v >> (8 * i));
}

// Builds the 10-byte argument from the three fields.
static void makeArg(uint8_t out[10], int32_t latE6, int32_t lonE6, int16_t bearingX100) {
  putLe(out, (uint32_t)latE6, 4);
  putLe(out + 4, (uint32_t)lonE6, 4);
  putLe(out + 8, (uint16_t)bearingX100, 2);
}

static bool parses(int32_t latE6, int32_t lonE6, int16_t bearingX100) {
  uint8_t arg[10];
  makeArg(arg, latE6, lonE6, bearingX100);
  NavAnchor a;
  return parseAnchorArgs(arg, sizeof arg, &a);
}

void test_opcode_value() { TEST_ASSERT_EQUAL_HEX8(0x17, kCmdAnchor); }

void test_golden_sao_paulo() {
  TEST_ASSERT_EQUAL_HEX8(kCmdAnchor, kSaoPaulo[0]);
  NavAnchor a{0, 0, 0};
  TEST_ASSERT_TRUE(parseAnchorArgs(kSaoPaulo + 1, 10, &a));
  TEST_ASSERT_EQUAL_INT32(-23550520, a.latE6);
  TEST_ASSERT_EQUAL_INT32(-46633308, a.lonE6);
  TEST_ASSERT_EQUAL_INT16(1234, a.bearingX100);
}

void test_golden_paris() {
  TEST_ASSERT_EQUAL_HEX8(kCmdAnchor, kParis[0]);
  NavAnchor a{0, 0, 0};
  TEST_ASSERT_TRUE(parseAnchorArgs(kParis + 1, 10, &a));
  TEST_ASSERT_EQUAL_INT32(48856600, a.latE6);
  TEST_ASSERT_EQUAL_INT32(2352200, a.lonE6);
  TEST_ASSERT_EQUAL_INT16(-9000, a.bearingX100);
}

void test_eight_byte_form_has_bearing_zero() {
  NavAnchor a{1, 2, 3};
  TEST_ASSERT_TRUE(parseAnchorArgs(kSaoPaulo + 1, 8, &a));
  TEST_ASSERT_EQUAL_INT32(-23550520, a.latE6);
  TEST_ASSERT_EQUAL_INT32(-46633308, a.lonE6);
  TEST_ASSERT_EQUAL_INT16(0, a.bearingX100);
}

void test_other_lengths_rejected_and_output_untouched() {
  uint8_t buf[16];
  std::memset(buf, 0, sizeof buf);
  std::memcpy(buf, kSaoPaulo + 1, 10);
  const size_t bad[] = {0, 1, 4, 7, 9, 11, 12, 16};
  for (size_t len : bad) {
    NavAnchor a{11, 22, 33};
    TEST_ASSERT_FALSE_MESSAGE(parseAnchorArgs(buf, len, &a), "length");
    TEST_ASSERT_EQUAL_INT32(11, a.latE6);
    TEST_ASSERT_EQUAL_INT32(22, a.lonE6);
    TEST_ASSERT_EQUAL_INT16(33, a.bearingX100);
  }
}

void test_null_pointers_rejected() {
  NavAnchor a{11, 22, 33};
  TEST_ASSERT_FALSE(parseAnchorArgs(nullptr, 10, &a));
  TEST_ASSERT_EQUAL_INT32(11, a.latE6);
  TEST_ASSERT_FALSE(parseAnchorArgs(kSaoPaulo + 1, 10, nullptr));
}

void test_range_limits_accepted() {
  TEST_ASSERT_TRUE(parses(90000000, 180000000, 18000));
  TEST_ASSERT_TRUE(parses(-90000000, -180000000, -18000));
  TEST_ASSERT_TRUE(parses(0, 0, 0));
}

void test_out_of_range_rejected() {
  TEST_ASSERT_FALSE(parses(90000001, 0, 0));
  TEST_ASSERT_FALSE(parses(-90000001, 0, 0));
  TEST_ASSERT_FALSE(parses(0, 180000001, 0));
  TEST_ASSERT_FALSE(parses(0, -180000001, 0));
  TEST_ASSERT_FALSE(parses(0, 0, 18001));
  TEST_ASSERT_FALSE(parses(0, 0, -18001));
  TEST_ASSERT_FALSE(parses(INT32_MAX, 0, 0));
  TEST_ASSERT_FALSE(parses(0, INT32_MIN, 0));
  TEST_ASSERT_FALSE(parses(0, 0, INT16_MIN));
  // The 8-byte form checks the same ranges.
  uint8_t arg[10];
  makeArg(arg, 0, 180000001, 0);
  NavAnchor a{11, 22, 33};
  TEST_ASSERT_FALSE(parseAnchorArgs(arg, 8, &a));
  TEST_ASSERT_EQUAL_INT32(11, a.latE6);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_opcode_value);
  RUN_TEST(test_golden_sao_paulo);
  RUN_TEST(test_golden_paris);
  RUN_TEST(test_eight_byte_form_has_bearing_zero);
  RUN_TEST(test_other_lengths_rejected_and_output_untouched);
  RUN_TEST(test_null_pointers_rejected);
  RUN_TEST(test_range_limits_accepted);
  RUN_TEST(test_out_of_range_rejected);
  return UNITY_END();
}
