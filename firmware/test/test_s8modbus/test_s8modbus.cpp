#include <unity.h>
#include <cstring>
#include "../../Gaiaball/src/logic/S8Modbus.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

// Golden vector from the Senseair SE-0119 Modbus document: the example request
// FE 04 00 00 00 04 carries CRC E5 C6 (0xC6E5).
void test_crc16_matches_senseair_doc_example() {
  const uint8_t frame[] = {0xFE, 0x04, 0x00, 0x00, 0x00, 0x04};
  TEST_ASSERT_EQUAL_HEX16(0xC6E5, modbusCrc16(frame, sizeof frame));
}

void test_read_request_bytes() {
  const uint8_t expected[] = {0xFE, 0x04, 0x00, 0x03, 0x00, 0x01, 0xD5, 0xC5};
  TEST_ASSERT_EQUAL(sizeof expected, kS8RequestLen);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, kS8ReadCo2Request, sizeof expected);
  // The request must carry its own valid CRC.
  const uint16_t crc = modbusCrc16(kS8ReadCo2Request, 6);
  TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, kS8ReadCo2Request[6]);
  TEST_ASSERT_EQUAL_HEX8(crc >> 8, kS8ReadCo2Request[7]);
}

static S8ResponseParser::Result feedAll(S8ResponseParser &p, const uint8_t *d, size_t n) {
  S8ResponseParser::Result last = S8ResponseParser::Result::NeedMore;
  for (size_t i = 0; i < n; i++) last = p.feed(d[i]);
  return last;
}

void test_parse_response_400ppm() {
  // CRC computed with the doc-verified implementation above.
  const uint8_t resp[] = {0xFE, 0x04, 0x02, 0x01, 0x90, 0xAC, 0xD8};
  S8ResponseParser p;
  TEST_ASSERT_EQUAL(S8ResponseParser::Result::Frame, feedAll(p, resp, sizeof resp));
  TEST_ASSERT_EQUAL(400, p.ppm());
  TEST_ASSERT_EQUAL_UINT32(1, p.frameCount());
  TEST_ASSERT_EQUAL_UINT32(0, p.errorCount());
}

void test_parse_response_1226ppm() {
  const uint8_t resp[] = {0xFE, 0x04, 0x02, 0x04, 0xCA, 0x2F, 0xB3};
  S8ResponseParser p;
  TEST_ASSERT_EQUAL(S8ResponseParser::Result::Frame, feedAll(p, resp, sizeof resp));
  TEST_ASSERT_EQUAL(1226, p.ppm());
}

void test_bad_crc_counts_error_not_frame() {
  const uint8_t resp[] = {0xFE, 0x04, 0x02, 0x01, 0x90, 0xAC, 0xD9};  // last byte off
  S8ResponseParser p;
  TEST_ASSERT_EQUAL(S8ResponseParser::Result::Error, feedAll(p, resp, sizeof resp));
  TEST_ASSERT_EQUAL_UINT32(0, p.frameCount());
  TEST_ASSERT_EQUAL_UINT32(1, p.errorCount());
}

void test_resync_through_leading_noise() {
  const uint8_t stream[] = {0x00, 0xFF, 0xFE, 0x99,                          // noise + false start
                            0xFE, 0x04, 0x02, 0x01, 0x90, 0xAC, 0xD8};      // real frame
  S8ResponseParser p;
  TEST_ASSERT_EQUAL(S8ResponseParser::Result::Frame, feedAll(p, stream, sizeof stream));
  TEST_ASSERT_EQUAL(400, p.ppm());
  TEST_ASSERT_EQUAL_UINT32(1, p.frameCount());
}

void test_truncated_response_then_full_frame() {
  const uint8_t stream[] = {0xFE, 0x04, 0x02, 0x01,                          // cut short
                            0xFE, 0x04, 0x02, 0x04, 0xCA, 0x2F, 0xB3};      // full frame
  S8ResponseParser p;
  // Positions 3..6 are raw payload/CRC, so the second frame's first bytes are absorbed
  // into the truncated candidate, which then fails CRC (computed: 0x342D != 0x0204).
  feedAll(p, stream, sizeof stream);
  TEST_ASSERT_EQUAL_UINT32(0, p.frameCount());
  // After reset() (what the service does before each poll), the next response parses.
  p.reset();
  const uint8_t resp[] = {0xFE, 0x04, 0x02, 0x04, 0xCA, 0x2F, 0xB3};
  TEST_ASSERT_EQUAL(S8ResponseParser::Result::Frame, feedAll(p, resp, sizeof resp));
  TEST_ASSERT_EQUAL(1226, p.ppm());
}

void test_zero_ppm_frame() {
  uint8_t resp[] = {0xFE, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00};
  const uint16_t crc = modbusCrc16(resp, 5);
  resp[5] = static_cast<uint8_t>(crc & 0xFF);
  resp[6] = static_cast<uint8_t>(crc >> 8);
  S8ResponseParser p;
  TEST_ASSERT_EQUAL(S8ResponseParser::Result::Frame, feedAll(p, resp, sizeof resp));
  TEST_ASSERT_EQUAL(0, p.ppm());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_matches_senseair_doc_example);
  RUN_TEST(test_read_request_bytes);
  RUN_TEST(test_parse_response_400ppm);
  RUN_TEST(test_parse_response_1226ppm);
  RUN_TEST(test_bad_crc_counts_error_not_frame);
  RUN_TEST(test_resync_through_leading_noise);
  RUN_TEST(test_truncated_response_then_full_frame);
  RUN_TEST(test_zero_ppm_frame);
  return UNITY_END();
}
