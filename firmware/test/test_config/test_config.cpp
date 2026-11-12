#include <unity.h>
#include <cstring>
#include "../../Gaiaball/src/logic/GaiaConfig.h"
#include "../../Gaiaball/src/logic/ImuStatus.h"
#include "../../Gaiaball/src/logic/MagAxisRemap.h"
#include "../../Gaiaball/src/logic/MagCalibration.h"
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

  uint8_t buf[384];
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
  uint8_t buf2[384];
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
  uint8_t buf[384];
  size_t full = tlvSerialize(c, buf, sizeof buf);
  TEST_ASSERT_GREATER_THAN(0, full);
  TEST_ASSERT_EQUAL(0, tlvSerialize(c, buf, full - 1));
}

void test_pin_not_serialized() {
  GaiaConfig c;
  std::strcpy(c.pin, "5678");
  uint8_t buf[384];
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

// --- tlvApplyLenient: the NVS persistence path (unknown ids skipped, invariants
// --- restored) so a config saved by one firmware version loads on another.

void test_lenient_roundtrip_from_serialize() {
  GaiaConfig a;
  a.orientationMode = 1;
  a.imuNotifyHz = 30;
  a.virtualHoldMs = 60000;
  a.soundLedPin = 5;
  std::strcpy(a.bleName, "MyBall");
  std::strcpy(a.pin, "9876");
  uint8_t buf[384];
  const size_t n = tlvSerialize(a, buf, sizeof buf);
  TEST_ASSERT_GREATER_THAN(0, n);
  GaiaConfig b;
  TEST_ASSERT_GREATER_THAN(0, tlvApplyLenient(b, buf, n));
  TEST_ASSERT_EQUAL(1, b.orientationMode);
  TEST_ASSERT_EQUAL(30, b.imuNotifyHz);
  TEST_ASSERT_EQUAL(60000, b.virtualHoldMs);
  TEST_ASSERT_EQUAL(5, b.soundLedPin);
  TEST_ASSERT_EQUAL_STRING("MyBall", b.bleName);
  TEST_ASSERT_EQUAL_STRING("1234", b.pin);  // PIN never applied by the lenient path
}

void test_lenient_skips_unknown_and_invalid() {
  GaiaConfig c;
  const uint8_t buf[] = {0x77, 1, 9,          // unknown id: skipped (future register)
                         0x02, 1, 30,         // valid: applied
                         0x03, 1, 101,        // out of range: skipped
                         0x41, 4, '0', '0', '0', '0',  // PIN record: never applied
                         0x32, 2, 0xB8, 0x0B};         // valid: applied (3000)
  TEST_ASSERT_EQUAL(2, tlvApplyLenient(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(30, c.imuNotifyHz);
  TEST_ASSERT_EQUAL(0, c.slerpAmount);  // untouched default
  TEST_ASSERT_EQUAL(3000, c.co2AlertPpm);
  TEST_ASSERT_EQUAL_STRING("1234", c.pin);
}

void test_lenient_stops_at_truncation() {
  GaiaConfig c;
  const uint8_t buf[] = {0x02, 1, 30, 0x15, 4, 1, 2};  // u32 cut short
  TEST_ASSERT_EQUAL(1, tlvApplyLenient(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(30, c.imuNotifyHz);
  TEST_ASSERT_EQUAL_UINT32(100000, c.thresholdFixed);  // untouched default
  TEST_ASSERT_EQUAL(0, tlvApplyLenient(c, nullptr, 0));  // empty is a no-op
}

void test_lenient_restores_cross_field_invariants() {
  // Individually valid records that break the invariants together: the lenient load
  // must end on a sane state (defaults restored for the broken pair).
  GaiaConfig c;
  const uint8_t samePin[] = {0x30, 1, 18, 0x31, 1, 18};
  tlvApplyLenient(c, samePin, sizeof samePin);
  TEST_ASSERT_EQUAL(2, c.soundLedPin);
  TEST_ASSERT_EQUAL(4, c.co2LedPin);
  GaiaConfig d;
  const uint8_t inverted[] = {0x17, 2, 0xF4, 0x01, 0x19, 2, 100, 0};  // hp 500, lp 100
  tlvApplyLenient(d, inverted, sizeof inverted);
  TEST_ASSERT_EQUAL(10, d.audioHpHz);
  TEST_ASSERT_EQUAL(200, d.audioLpHz);
}

// ------------------------------------------------ IMU / fusion registers (v7)

void test_imu_fusion_defaults() {
  GaiaConfig c;
  TEST_ASSERT_EQUAL(0, c.imuDlpfMode);          // 0 = auto (anti-alias from the rate)
  TEST_ASSERT_EQUAL(2, c.gyroFsSel);            // +-1000 dps: headroom for a spun ball
  TEST_ASSERT_EQUAL(50, c.fusionGainX100);      // 0.50, the x-io Fusion default
  TEST_ASSERT_EQUAL(10, c.accelRejectionDeg);   // degrees
  TEST_ASSERT_EQUAL(10, c.magRejectionDeg);     // degrees
  TEST_ASSERT_EQUAL(5, c.rejectionTimeoutS);
  TEST_ASSERT_EQUAL(200, c.biasRestThDpsX100);  // 2.00 dps of deviation
  TEST_ASSERT_EQUAL(1000, c.biasMaxDpsX100);    // 10.00 dps plausibility bound
  TEST_ASSERT_EQUAL(4, c.biasTauS);
  TEST_ASSERT_EQUAL(0, c.quatSmoothMode);       // off, as the legacy slerp default
  TEST_ASSERT_EQUAL(100, c.oneEuroFcMinX100);   // 1.00 Hz
  TEST_ASSERT_EQUAL(20, c.oneEuroBetaX1000);    // 0.020 Hz per dps
  TEST_ASSERT_EQUAL(0, c.accelHampelKX10);      // spike filter off
  TEST_ASSERT_EQUAL(0, c.magHardIron[0]);       // no magnetometer calibration
  TEST_ASSERT_EQUAL(kMagSoftIronOne, c.magSoftIron[0]);
  TEST_ASSERT_EQUAL(0, c.magSoftIron[1]);
  TEST_ASSERT_EQUAL(kMagSoftIronOne, c.magSoftIron[4]);
  TEST_ASSERT_EQUAL(kMagSoftIronOne, c.magSoftIron[8]);
}

// The raw 2 kHz audio stream is now opt-in: it is the single most expensive thing on
// the link (~45 ms/s of air time, 53 % of the total before this change), so it must
// default to off and survive a reboot once the user turns it on.
void test_raw_audio_stream_defaults_off_and_roundtrips() {
  GaiaConfig c;
  TEST_ASSERT_EQUAL(0, c.audioRawStream);
  const uint8_t on[] = {0x1F, 1, 1};
  TEST_ASSERT_TRUE(tlvApply(c, on, sizeof on));
  TEST_ASSERT_EQUAL(1, c.audioRawStream);
  const uint8_t bad[] = {0x1F, 1, 2};  // boolean register: only 0/1
  TEST_ASSERT_FALSE(tlvApply(c, bad, sizeof bad));
  TEST_ASSERT_EQUAL(1, c.audioRawStream);  // rejected write changes nothing
  const uint8_t off[] = {0x1F, 1, 0};
  TEST_ASSERT_TRUE(tlvApply(c, off, sizeof off));
  TEST_ASSERT_EQUAL(0, c.audioRawStream);
}

void test_imu_fusion_registers_roundtrip() {
  const uint8_t buf[] = {
      0x05, 1, 4,           // imuDlpfMode = 4 (20 Hz)
      0x06, 1, 1,           // gyroFsSel = 1 (+-500 dps)
      0x07, 2, 0x96, 0x00,  // fusionGainX100 = 150
      0x08, 1, 15,          // accelRejectionDeg
      0x09, 1, 20,          // magRejectionDeg
      0x0A, 1, 8,           // rejectionTimeoutS
      0x0B, 2, 0x2C, 0x01,  // biasRestThDpsX100 = 300
      0x0C, 2, 0xB8, 0x0B,  // biasMaxDpsX100 = 3000
      0x0D, 1, 10,          // biasTauS
      0x0E, 1, 2,           // quatSmoothMode = one euro
      0x0F, 2, 0xC8, 0x00,  // oneEuroFcMinX100 = 200
      0x1C, 2, 0x32, 0x00,  // oneEuroBetaX1000 = 50
      0x1D, 1, 30,          // accelHampelKX10 = 3.0
  };
  GaiaConfig c;
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  TEST_ASSERT_EQUAL(4, c.imuDlpfMode);
  TEST_ASSERT_EQUAL(1, c.gyroFsSel);
  TEST_ASSERT_EQUAL(150, c.fusionGainX100);
  TEST_ASSERT_EQUAL(15, c.accelRejectionDeg);
  TEST_ASSERT_EQUAL(20, c.magRejectionDeg);
  TEST_ASSERT_EQUAL(8, c.rejectionTimeoutS);
  TEST_ASSERT_EQUAL(300, c.biasRestThDpsX100);
  TEST_ASSERT_EQUAL(3000, c.biasMaxDpsX100);
  TEST_ASSERT_EQUAL(10, c.biasTauS);
  TEST_ASSERT_EQUAL(2, c.quatSmoothMode);
  TEST_ASSERT_EQUAL(200, c.oneEuroFcMinX100);
  TEST_ASSERT_EQUAL(50, c.oneEuroBetaX1000);
  TEST_ASSERT_EQUAL(30, c.accelHampelKX10);
}

void test_imu_fusion_registers_reject_out_of_range() {
  const uint8_t dlpf[] = {0x05, 1, 8};             // only 0..7
  const uint8_t fs[] = {0x06, 1, 4};               // only 0..3
  const uint8_t gain[] = {0x07, 2, 0xF5, 0x01};    // 501 > 500
  const uint8_t reject[] = {0x08, 1, 91};          // > 90 deg
  const uint8_t mode[] = {0x0E, 1, 3};             // only 0..2
  const uint8_t restTh[] = {0x0B, 2, 0x00, 0x00};  // 0 would freeze rest detection
  assertRejected(dlpf, sizeof dlpf);
  assertRejected(fs, sizeof fs);
  assertRejected(gain, sizeof gain);
  assertRejected(reject, sizeof reject);
  assertRejected(mode, sizeof mode);
  assertRejected(restTh, sizeof restTh);
}

// --------------------------------------------- magnetometer calibration (0x1E)

// 24-byte record: 3 x int16 hard iron (0.1 uT) + 9 x int16 soft iron (1/4096).
static void buildMagCalRecord(uint8_t *out, const int16_t hard[3], const int16_t soft[9]) {
  out[0] = 0x1E;
  out[1] = 24;
  for (int i = 0; i < 3; i++) {
    out[2 + 2 * i] = (uint8_t)(hard[i] & 0xFF);
    out[3 + 2 * i] = (uint8_t)((uint16_t)hard[i] >> 8);
  }
  for (int i = 0; i < 9; i++) {
    out[8 + 2 * i] = (uint8_t)(soft[i] & 0xFF);
    out[9 + 2 * i] = (uint8_t)((uint16_t)soft[i] >> 8);
  }
}

void test_mag_calibration_roundtrip() {
  const int16_t hard[3] = {250, -1200, 70};
  const int16_t soft[9] = {4300, -60, 12, -60, 3900, 5, 12, 5, 4150};
  uint8_t buf[26];
  buildMagCalRecord(buf, hard, soft);
  GaiaConfig c;
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof buf));
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_INT16(hard[i], c.magHardIron[i]);
  for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_INT16(soft[i], c.magSoftIron[i]);

  uint8_t ser[512];
  const size_t n = tlvSerialize(c, ser, sizeof ser);
  TEST_ASSERT_TRUE(n > 0);
  GaiaConfig back;
  TEST_ASSERT_TRUE(tlvApply(back, ser, n));
  for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_INT16(soft[i], back.magSoftIron[i]);
}

void test_mag_calibration_rejects_bad_length_and_singular_matrix() {
  uint8_t shortRec[] = {0x1E, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  assertRejected(shortRec, sizeof shortRec);
  // A zero diagonal would silently zero the magnetometer: refuse it.
  const int16_t hard[3] = {0, 0, 0};
  const int16_t soft[9] = {0, 0, 0, 0, 4096, 0, 0, 0, 4096};
  uint8_t buf[26];
  buildMagCalRecord(buf, hard, soft);
  assertRejected(buf, sizeof buf);
}

// --- dual-IMU registers: imuChip (0x34), magAxisRemap (0x36) ----------------------

// Re-serialises cfg but drops every record with the given id, to fake a blob written by
// firmware that did not know that register.
static size_t tlvSerializeWithout(const GaiaConfig &c, uint8_t *out, size_t cap, uint8_t skipId) {
  uint8_t full[384];
  const size_t n = tlvSerialize(c, full, sizeof full);
  size_t w = 0;
  for (size_t i = 0; i + 2 <= n;) {
    const size_t rec = 2u + full[i + 1];
    if (full[i] != skipId) {
      TEST_ASSERT_TRUE(w + rec <= cap);
      memcpy(out + w, full + i, rec);
      w += rec;
    }
    i += rec;
  }
  return w;
}

void test_default_config_has_no_imu_selected(void) {
  GaiaConfig c;
  TEST_ASSERT_EQUAL_UINT8(0, c.imuChip);
  TEST_ASSERT_EQUAL_UINT8(kMagRemapDefault, c.magAxisRemap);
}

void test_imu_chip_out_of_range_rejects_whole_write(void) {
  GaiaConfig c;
  c.imuChip = 1;
  TEST_ASSERT_EQUAL_UINT8(50, c.imuNotifyHz);
  // Invalid 0x34 FIRST, valid 0x02 (imuNotifyHz=100) AFTER: all-or-nothing means the
  // valid record must not have been applied either.
  const uint8_t buf[] = {0x34, 1, 3, 0x02, 1, 100};
  TEST_ASSERT_FALSE(tlvApply(c, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT8(1, c.imuChip);
  TEST_ASSERT_EQUAL_UINT8(50, c.imuNotifyHz);
}

void test_mag_axis_remap_out_of_range_rejects_whole_write(void) {
  GaiaConfig c;
  const uint8_t buf[] = {0x36, 1, 24, 0x02, 1, 100};
  TEST_ASSERT_FALSE(tlvApply(c, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT8(kMagRemapDefault, c.magAxisRemap);
  TEST_ASSERT_EQUAL_UINT8(50, c.imuNotifyHz);
}

void test_imu_registers_accept_boundaries(void) {
  GaiaConfig c;
  const uint8_t buf[] = {0x34, 1, 2, 0x36, 1, 23};
  TEST_ASSERT_TRUE(tlvApply(c, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT8(2, c.imuChip);
  TEST_ASSERT_EQUAL_UINT8(23, c.magAxisRemap);
}

void test_pre_v7_blob_loads_as_unconfigured(void) {
  GaiaConfig c;
  c.imuChip = 2;
  c.magAxisRemap = 5;
  uint8_t buf[384];
  const size_t n = tlvSerializeWithout(c, buf, sizeof(buf), 0x34);
  GaiaConfig loaded;
  tlvApplyLenient(loaded, buf, n);
  TEST_ASSERT_EQUAL_UINT8(0, loaded.imuChip);
  TEST_ASSERT_EQUAL_UINT8(5, loaded.magAxisRemap);  // helper really kept the rest
}

void test_roundtrip_preserves_both_new_registers(void) {
  GaiaConfig c;
  c.imuChip = 2;
  c.magAxisRemap = 17;
  uint8_t buf[384];
  const size_t n = tlvSerialize(c, buf, sizeof(buf));
  GaiaConfig out;
  TEST_ASSERT_TRUE(tlvApply(out, buf, n));
  TEST_ASSERT_EQUAL_UINT8(2, out.imuChip);
  TEST_ASSERT_EQUAL_UINT8(17, out.magAxisRemap);
}

// --- imuDetected (0x35): read-only, outside the codec table -------------------------

void test_status_byte_encodes_boot_selection_and_found(void) {
  TEST_ASSERT_EQUAL_UINT8(0x06, imuStatusByte(ImuChip::Mpu9250, true));  // 2 | kImuFound
  TEST_ASSERT_EQUAL_UINT8(0x05, imuStatusByte(ImuChip::Icm20948, true));
  TEST_ASSERT_EQUAL_UINT8(0x01, imuStatusByte(ImuChip::Icm20948, false));
  TEST_ASSERT_EQUAL_UINT8(0x00, imuStatusByte(ImuChip::None, false));
  TEST_ASSERT_EQUAL_UINT8(0x04, kImuFound);
  // Bit 3: magnetometer found; independent of the other bits.
  TEST_ASSERT_EQUAL_UINT8(0x08, kImuMagFound);
  TEST_ASSERT_EQUAL_UINT8(0x0D, imuStatusByte(ImuChip::Icm20948, true, true));
  TEST_ASSERT_EQUAL_UINT8(0x0E, imuStatusByte(ImuChip::Mpu9250, true, true));
  TEST_ASSERT_EQUAL_UINT8(0x05, imuStatusByte(ImuChip::Icm20948, true, false));
  TEST_ASSERT_EQUAL_UINT8(0x00, imuStatusByte(ImuChip::None, false, false));
}

void test_0x35_is_rejected_on_write(void) {
  GaiaConfig c;
  const uint8_t buf[] = {0x35, 1, 0x06};
  TEST_ASSERT_FALSE(tlvApply(c, buf, sizeof(buf)));
  // ...and it poisons the whole write, valid records included.
  const uint8_t mixed[] = {0x02, 1, 100, 0x35, 1, 0x06};
  TEST_ASSERT_FALSE(tlvApply(c, mixed, sizeof(mixed)));
  TEST_ASSERT_EQUAL_UINT8(50, c.imuNotifyHz);
}

void test_0x35_never_appears_in_serialized_config(void) {
  GaiaConfig c;
  c.imuChip = 2;
  uint8_t buf[384];
  const size_t n = tlvSerialize(c, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n > 0);
  for (size_t i = 0; i < n;) {
    TEST_ASSERT_NOT_EQUAL(0x35, buf[i]);
    i += 2u + buf[i + 1];
  }
}

void test_serialized_config_leaves_room_for_status_record(void) {
  GaiaConfig c;  // the 3-byte 0x35 record is appended after tlvSerialize in a 384 B buffer
  uint8_t buf[384];
  const size_t n = tlvSerialize(c, buf, sizeof(buf));
  TEST_ASSERT_TRUE(n + 3 <= sizeof(buf));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_status_byte_encodes_boot_selection_and_found);
  RUN_TEST(test_0x35_is_rejected_on_write);
  RUN_TEST(test_0x35_never_appears_in_serialized_config);
  RUN_TEST(test_serialized_config_leaves_room_for_status_record);
  RUN_TEST(test_default_config_has_no_imu_selected);
  RUN_TEST(test_imu_chip_out_of_range_rejects_whole_write);
  RUN_TEST(test_mag_axis_remap_out_of_range_rejects_whole_write);
  RUN_TEST(test_imu_registers_accept_boundaries);
  RUN_TEST(test_pre_v7_blob_loads_as_unconfigured);
  RUN_TEST(test_roundtrip_preserves_both_new_registers);
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
  RUN_TEST(test_lenient_roundtrip_from_serialize);
  RUN_TEST(test_lenient_skips_unknown_and_invalid);
  RUN_TEST(test_lenient_stops_at_truncation);
  RUN_TEST(test_lenient_restores_cross_field_invariants);
  RUN_TEST(test_imu_fusion_defaults);
  RUN_TEST(test_raw_audio_stream_defaults_off_and_roundtrips);
  RUN_TEST(test_imu_fusion_registers_roundtrip);
  RUN_TEST(test_imu_fusion_registers_reject_out_of_range);
  RUN_TEST(test_mag_calibration_roundtrip);
  RUN_TEST(test_mag_calibration_rejects_bad_length_and_singular_matrix);
  return UNITY_END();
}
