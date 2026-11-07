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
  TEST_ASSERT_EQUAL(0, c.accelLpAlphaX1000);   // accel filter off = legacy raw
  TEST_ASSERT_EQUAL(1, c.audioBpfEnabled);     // breath band-pass on by default
  TEST_ASSERT_EQUAL(10, c.audioHpHz);
  TEST_ASSERT_EQUAL(500, c.audioHpQX100);      // Q = 5.00 (requested "high Q")
  TEST_ASSERT_EQUAL(200, c.audioLpHz);
  TEST_ASSERT_EQUAL(500, c.audioLpQX100);
  TEST_ASSERT_EQUAL(300, c.potEmaAlphaX1000);  // legacy 0.3
  TEST_ASSERT_EQUAL(1000, c.co2EmaAlphaX1000); // 1000 = no smoothing (legacy)
  TEST_ASSERT_EQUAL(0, c.serialLogEnabled);    // serial log OFF by default
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

void test_reserved_ids_rejected() {
  // Reserved (removed) registers are unknown ids and must be rejected atomically:
  // 0x21 (ex-co2ChecksumVariant), 0x27 (ex-co2Protocol), 0x33 (ex-statusLedPin).
  const uint8_t exChecksum[] = {0x21, 1, 1};
  assertRejected(exChecksum, sizeof exChecksum);
  const uint8_t exProtocol[] = {0x27, 1, 0};
  assertRejected(exProtocol, sizeof exProtocol);
  const uint8_t exStatusLed[] = {0x33, 1, 18};
  assertRejected(exStatusLed, sizeof exStatusLed);
}

void test_filter_registers_and_band_rule() {
  GaiaConfig c;
  const uint8_t buf[] = {0x04, 2, 0x2C, 0x01,   // accelLpAlphaX1000 = 300
                         0x16, 1, 0,            // band-pass off
                         0x17, 2, 30, 0,        // hp 30 Hz
                         0x18, 2, 0x47, 0x00,   // hp Q 0.71
                         0x19, 2, 0xF4, 0x01,   // lp 500 Hz
                         0x1A, 2, 0x47, 0x00,   // lp Q 0.71
                         0x1B, 2, 0x64, 0x00,   // pot alpha 100
                         0x28, 2, 0xF4, 0x01,   // co2 ema 500
                         0x50, 1, 1,            // serial log on
                         0x02, 1, 200};         // imu rate 200 Hz
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(300, c.accelLpAlphaX1000);
  TEST_ASSERT_EQUAL(0, c.audioBpfEnabled);
  TEST_ASSERT_EQUAL(30, c.audioHpHz);
  TEST_ASSERT_EQUAL(500, c.audioLpHz);
  TEST_ASSERT_EQUAL(100, c.potEmaAlphaX1000);
  TEST_ASSERT_EQUAL(500, c.co2EmaAlphaX1000);
  TEST_ASSERT_EQUAL(1, c.serialLogEnabled);
  TEST_ASSERT_EQUAL(200, c.imuNotifyHz);
  // Band rule on the FINAL state: lp must stay above hp, whether the collision
  // comes in one write or against the stored value.
  const uint8_t inverted[] = {0x17, 2, 0xF4, 0x01, 0x19, 2, 100, 0};  // hp 500 > lp 100
  assertRejected(inverted, sizeof inverted);
  const uint8_t hpOverStoredLp[] = {0x17, 2, 0xC8, 0};  // hp 200 == default lp 200
  assertRejected(hpOverStoredLp, sizeof hpOverStoredLp);
  const uint8_t rate201[] = {0x02, 1, 201};  // above the 200 Hz cap
  assertRejected(rate201, sizeof rate201);
  const uint8_t rate9[] = {0x02, 1, 9};
  assertRejected(rate9, sizeof rate9);
}

void test_reject_duplicate_led_pins() {
  // Final-state check: a write that lands both LED functions on one GPIO must fail
  // atomically, whether the collision is with a default or inside the same buffer.
  const uint8_t soundOnCo2[] = {0x30, 1, 4};  // sound -> co2's default pin
  assertRejected(soundOnCo2, sizeof soundOnCo2);
  const uint8_t co2OnSound[] = {0x31, 1, 2};  // co2 -> sound's default pin
  assertRejected(co2OnSound, sizeof co2OnSound);
  const uint8_t inBuffer[] = {0x30, 1, 18, 0x31, 1, 18};  // collision within buffer
  assertRejected(inBuffer, sizeof inBuffer);
  GaiaConfig c;
  const uint8_t distinct[] = {0x30, 1, 18, 0x31, 1, 19};
  TEST_ASSERT_TRUE(tlvApply(c, distinct, sizeof distinct));
  TEST_ASSERT_EQUAL(18, c.soundLedPin);
  TEST_ASSERT_EQUAL(19, c.co2LedPin);
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
  RUN_TEST(test_reserved_ids_rejected);
  RUN_TEST(test_filter_registers_and_band_rule);
  RUN_TEST(test_reject_duplicate_led_pins);
  RUN_TEST(test_shorter_name_clears_old_tail);
  RUN_TEST(test_serialize_cap_too_small);
  RUN_TEST(test_pin_not_serialized);
  return UNITY_END();
}
