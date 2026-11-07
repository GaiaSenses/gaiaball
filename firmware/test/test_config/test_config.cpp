#include <unity.h>
#include <cstring>
#include "../../Gaiaball/src/logic/GaiaConfig.h"
#include "../../Gaiaball/src/logic/TlvCodec.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

// Applies `buf`, asserts rejection and that the full struct is byte-identical.
static void assertRejected(const uint8_t *buf, size_t len) {
  GaiaConfig c;
  c.imuNotifyHz = 33;  // non-default so a partial write would be visible
  GaiaConfig snapshot;
  std::memcpy(&snapshot, &c, sizeof c);
  TEST_ASSERT_FALSE(tlvApply(c, buf, len));
  TEST_ASSERT_TRUE(std::memcmp(&snapshot, &c, sizeof c) == 0);
}

void test_defaults_match_legacy() {
  GaiaConfig c;
  TEST_ASSERT_EQUAL(50, c.imuNotifyHz);
  TEST_ASSERT_EQUAL(250, c.audioFastAlphaX1000);
  TEST_ASSERT_EQUAL(995, c.audioSlowAlphaX1000);
  TEST_ASSERT_EQUAL(300, c.debounceMs);
  TEST_ASSERT_EQUAL_UINT32(100000, c.thresholdFixed);
  TEST_ASSERT_EQUAL(17000, c.virtualHoldMs);
  TEST_ASSERT_EQUAL(2, c.soundLedPin);
  TEST_ASSERT_EQUAL(4, c.co2LedPin);
  TEST_ASSERT_EQUAL(23, c.statusLedPin);  // new status LED (board change, not legacy)
  TEST_ASSERT_EQUAL(0, c.co2Protocol);    // S8 Modbus: the board's actual sensor
  TEST_ASSERT_EQUAL_STRING("Globo_1", c.bleName);
  TEST_ASSERT_EQUAL_STRING("1234", c.pin);
}

void test_roundtrip() {
  GaiaConfig a;
  a.orientationMode = 1;
  a.imuNotifyHz = 30;
  a.slerpAmount = 40;
  a.audioFastAlphaX1000 = 123;
  a.thresholdFixed = 0xA1B2C3D4u;
  a.virtualHoldMs = 60000;
  a.soundLedPin = 5;
  std::strcpy(a.bleName, "MyBall");
  std::strcpy(a.pin, "9876");

  uint8_t buf[128];
  size_t n = tlvSerialize(a, buf, sizeof buf);
  TEST_ASSERT_GREATER_THAN(0, n);

  GaiaConfig b;
  TEST_ASSERT_TRUE(tlvApply(b, buf, n));
  TEST_ASSERT_EQUAL(1, b.orientationMode);
  TEST_ASSERT_EQUAL(30, b.imuNotifyHz);
  TEST_ASSERT_EQUAL(40, b.slerpAmount);
  TEST_ASSERT_EQUAL(123, b.audioFastAlphaX1000);
  TEST_ASSERT_EQUAL_UINT32(0xA1B2C3D4u, b.thresholdFixed);
  TEST_ASSERT_EQUAL(60000, b.virtualHoldMs);
  TEST_ASSERT_EQUAL(5, b.soundLedPin);
  TEST_ASSERT_EQUAL_STRING("MyBall", b.bleName);
  TEST_ASSERT_EQUAL_STRING("1234", b.pin);  // PIN excluded: stays default
  // Re-serialize must be byte-identical (full non-PIN equality).
  uint8_t buf2[128];
  size_t n2 = tlvSerialize(b, buf2, sizeof buf2);
  TEST_ASSERT_EQUAL(n, n2);
  TEST_ASSERT_EQUAL_MEMORY(buf, buf2, n);
}

void test_reject_unknown_id() {
  const uint8_t ok_then_bad[] = {0x02, 1, 20, 0x77, 1, 0};  // valid rec first
  assertRejected(ok_then_bad, sizeof ok_then_bad);
}

void test_reject_bad_length() {
  const uint8_t u16_as_1[] = {0x02, 1, 20, 0x11, 1, 5};
  assertRejected(u16_as_1, sizeof u16_as_1);
  const uint8_t u8_as_2[] = {0x01, 2, 0, 0};
  assertRejected(u8_as_2, sizeof u8_as_2);
  const uint8_t pin3[] = {0x41, 3, '1', '2', '3'};
  assertRejected(pin3, sizeof pin3);
  const uint8_t name17[] = {0x40, 17, 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a', 'a',
                            'a',  'a', 'a', 'a', 'a', 'a', 'a', 'a'};
  assertRejected(name17, sizeof name17);
  const uint8_t name0[] = {0x40, 0};
  assertRejected(name0, sizeof name0);
}

void test_reject_truncated() {
  const uint8_t trunc_value[] = {0x02, 1, 20, 0x15, 4, 1, 2};  // u32 cut short
  assertRejected(trunc_value, sizeof trunc_value);
  const uint8_t trunc_header[] = {0x02, 1, 20, 0x03};  // id without len
  assertRejected(trunc_header, sizeof trunc_header);
}

void test_reject_out_of_range() {
  const uint8_t hz9[] = {0x02, 1, 9};
  assertRejected(hz9, sizeof hz9);
  const uint8_t hz61[] = {0x02, 1, 61};
  assertRejected(hz61, sizeof hz61);
  const uint8_t slerp101[] = {0x03, 1, 101};
  assertRejected(slerp101, sizeof slerp101);
  const uint8_t fast0[] = {0x11, 2, 0, 0};
  assertRejected(fast0, sizeof fast0);
  const uint8_t slow1001[] = {0x12, 2, 0xE9, 0x03};
  assertRejected(slow1001, sizeof slow1001);
  const uint8_t pin34[] = {0x30, 1, 34};
  assertRejected(pin34, sizeof pin34);
  const uint8_t ledpin[] = {0x31, 1, 255};
  assertRejected(ledpin, sizeof ledpin);
  const uint8_t flash6[] = {0x30, 1, 6};
  assertRejected(flash6, sizeof flash6);
  const uint8_t flash11[] = {0x31, 1, 11};
  assertRejected(flash11, sizeof flash11);
  const uint8_t nopin20[] = {0x31, 1, 20};
  assertRejected(nopin20, sizeof nopin20);
  const uint8_t nonprint[] = {0x40, 3, 'a', 0x01, 'b'};
  assertRejected(nonprint, sizeof nonprint);
  const uint8_t pinalpha[] = {0x41, 4, '1', '2', 'a', '4'};
  assertRejected(pinalpha, sizeof pinalpha);
}

void test_accept_boundaries_and_pin_write() {
  GaiaConfig c;
  const uint8_t buf[] = {0x02, 1, 10, 0x03, 1, 100, 0x11, 2, 1, 0, 0x12, 2, 0xE8, 0x03,
                         0x30, 1, 0,  0x31, 1, 33,  0x41, 4, '0', '9', '0', '9'};
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(10, c.imuNotifyHz);
  TEST_ASSERT_EQUAL(100, c.slerpAmount);
  TEST_ASSERT_EQUAL(1, c.audioFastAlphaX1000);
  TEST_ASSERT_EQUAL(1000, c.audioSlowAlphaX1000);
  TEST_ASSERT_EQUAL(33, c.co2LedPin);
  TEST_ASSERT_EQUAL_STRING("0909", c.pin);
  TEST_ASSERT_TRUE(tlvApply(c, buf, 0));  // empty buffer is a valid no-op
}

void test_accept_valid_output_gpio() {
  GaiaConfig c;
  const uint8_t buf[] = {0x30, 1, 13};
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(13, c.soundLedPin);
}

void test_status_led_and_protocol_registers() {
  GaiaConfig c;
  const uint8_t buf[] = {0x33, 1, 18, 0x27, 1, 1};
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(18, c.statusLedPin);
  TEST_ASSERT_EQUAL(1, c.co2Protocol);
  const uint8_t flash7[] = {0x33, 1, 7};
  assertRejected(flash7, sizeof flash7);
  const uint8_t proto2[] = {0x27, 1, 2};
  assertRejected(proto2, sizeof proto2);
}

void test_reject_duplicate_led_pins() {
  // Final-state check: a write that lands two LED functions on one GPIO must fail
  // atomically, whether the collision is with a default or inside the same buffer.
  const uint8_t soundOnCo2[] = {0x30, 1, 4};  // sound -> co2's default pin
  assertRejected(soundOnCo2, sizeof soundOnCo2);
  const uint8_t statusOnSound[] = {0x33, 1, 2};  // status -> sound's default pin
  assertRejected(statusOnSound, sizeof statusOnSound);
  const uint8_t inBuffer[] = {0x30, 1, 18, 0x31, 1, 18};  // collision within buffer
  assertRejected(inBuffer, sizeof inBuffer);
  GaiaConfig c;
  const uint8_t allDistinct[] = {0x30, 1, 18, 0x31, 1, 19, 0x33, 1, 27};
  TEST_ASSERT_TRUE(tlvApply(c, allDistinct, sizeof allDistinct));
  TEST_ASSERT_EQUAL(18, c.soundLedPin);
  TEST_ASSERT_EQUAL(19, c.co2LedPin);
  TEST_ASSERT_EQUAL(27, c.statusLedPin);
  // Last-one-wins still applies before the distinctness check.
  const uint8_t lastWins[] = {0x30, 1, 4, 0x30, 1, 5};
  GaiaConfig d;
  TEST_ASSERT_TRUE(tlvApply(d, lastWins, sizeof lastWins));
  TEST_ASSERT_EQUAL(5, d.soundLedPin);
}

void test_shorter_name_clears_old_tail() {
  GaiaConfig c;  // "Globo_1"
  const uint8_t buf[] = {0x40, 2, 'H', 'i'};
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("Hi", c.bleName);
  TEST_ASSERT_EQUAL(0, c.bleName[3]);
}

void test_serialize_cap_too_small() {
  GaiaConfig c;
  uint8_t buf[128];
  size_t full = tlvSerialize(c, buf, sizeof buf);
  TEST_ASSERT_GREATER_THAN(0, full);
  TEST_ASSERT_EQUAL(0, tlvSerialize(c, buf, full - 1));
}

void test_pin_not_serialized() {
  GaiaConfig c;
  std::strcpy(c.pin, "5678");
  uint8_t buf[128];
  size_t n = tlvSerialize(c, buf, sizeof buf);
  size_t i = 0;
  while (i < n) {
    TEST_ASSERT_NOT_EQUAL(0x41, buf[i]);
    i += 2 + buf[i + 1];
  }
  TEST_ASSERT_EQUAL(n, i);
  for (size_t k = 0; k + 3 < n; k++)  // PIN bytes absent anywhere
    TEST_ASSERT_TRUE(std::memcmp(buf + k, "5678", 4) != 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_match_legacy);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_reject_unknown_id);
  RUN_TEST(test_reject_bad_length);
  RUN_TEST(test_reject_truncated);
  RUN_TEST(test_reject_out_of_range);
  RUN_TEST(test_accept_boundaries_and_pin_write);
  RUN_TEST(test_accept_valid_output_gpio);
  RUN_TEST(test_status_led_and_protocol_registers);
  RUN_TEST(test_reject_duplicate_led_pins);
  RUN_TEST(test_shorter_name_clears_old_tail);
  RUN_TEST(test_serialize_cap_too_small);
  RUN_TEST(test_pin_not_serialized);
  return UNITY_END();
}
