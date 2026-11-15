#include <unity.h>
#include <cmath>
#include <cstring>
#include "../../Gaiaball/src/logic/Packets.h"
#include "../../Gaiaball/src/logic/PacketCodec.h"

using namespace gaia;

static_assert(sizeof(ImuPacket) == 31, "ImuPacket must be 31 bytes");
static_assert(sizeof(Co2Packet) == 12, "Co2Packet must be 12 bytes");
static_assert(sizeof(DiagPacket) == 34, "DiagPacket must be 34 bytes");
static_assert(sizeof(SensorsPacket) == 23, "SensorsPacket must be 23 bytes");

void setUp() {}
void tearDown() {}

void test_packet_sizes() {
  TEST_ASSERT_EQUAL(31, sizeof(ImuPacket));
  TEST_ASSERT_EQUAL(12, sizeof(Co2Packet));
  TEST_ASSERT_EQUAL(34, sizeof(DiagPacket));
  TEST_ASSERT_EQUAL(106, sizeof(MagChunkPacket));
  TEST_ASSERT_EQUAL(23, sizeof(SensorsPacket));
}

void test_sensors_encode_golden() {
  SensorsPacket p = makeSensorsPacket(7, 123456u, 12345.4f, 23456.0f, 100000.0f, 2048.0f,
                                      kSensAudioOk | kSensAudioEnabled | kSensPotControls);
  const uint8_t expected[23] = {
      0x01, 0x04,              // version, type = Sensors
      0x07, 0x00,              // seq = 7
      0x40, 0xE2, 0x01, 0x00,  // timestampMs = 123456
      0x39, 0x30, 0x00, 0x00,  // rmsL = 12345 (rounded)
      0xA0, 0x5B, 0x00, 0x00,  // rmsR = 23456
      0xA0, 0x86, 0x01, 0x00,  // threshold = 100000
      0x00, 0x08,              // potRaw = 2048
      0x07};                   // flags
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reinterpret_cast<const uint8_t *>(&p), 23);
}

void test_sensors_encode_saturation_and_nan() {
  SensorsPacket p = makeSensorsPacket(1, 2, NAN, -5.0f, 5e9f, 70000.0f, 0);
  TEST_ASSERT_EQUAL_UINT32(0, p.audioRmsL);          // NaN -> 0
  TEST_ASSERT_EQUAL_UINT32(0, p.audioRmsR);          // negative -> 0
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, p.audioThreshold);  // saturates
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, p.potRaw);        // saturates
}

void test_imu_encode_golden() {
  const float q[4] = {1.f, 0.f, 0.f, 0.f};
  const int16_t acc[3] = {1, -2, 3};
  AudioLevels lv;
  lv.rms[0] = kAudioFullScale / 1.41421356f;          //   0.00 dBFS (full-scale sine)
  lv.rms[1] = kAudioFullScale / 1.41421356f / 10.0f;  // -20.00 dBFS
  lv.peak[0] = kAudioFullScale;                       //   0.00 dBFS peak
  lv.peak[1] = kAudioFullScale / 2.0f;                //  -6.02 dBFS peak
  ImuPacket p = makeImuPacket(258, 0x0A0B0C0Du, q, acc, 0b1011, lv);
  const uint8_t expected[31] = {
      0x01, 0x01,                          // version, type
      0x02, 0x01,                          // seq = 258
      0x0D, 0x0C, 0x0B, 0x0A,              // timestampUs
      0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // quat W=16384, X, Y, Z = 0
      0x01, 0x00, 0xFE, 0xFF, 0x03, 0x00,  // acc 1, -2, 3
      0x0B,                                // flags (quat valid, 9-axis, audio valid)
      0x00, 0x00, 0x30, 0xF8,              // rms L = 0, rms R = -2000 (x100 dBFS)
      0x00, 0x00, 0xA6, 0xFD};             // peak L = 0, peak R = -602 (x100 dBFS)
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reinterpret_cast<const uint8_t *>(&p), 31);
}

void test_imu_encode_nan() {
  const float q[4] = {NAN, 0.f, 0.f, 0.f};
  const int16_t acc[3] = {0, 0, 0};
  ImuPacket p = makeImuPacket(1, 2, q, acc, kImuQuatValid | kImuMoving, AudioLevels{});
  TEST_ASSERT_EQUAL_HEX8(kImuMoving, p.flags);
  TEST_ASSERT_EQUAL_INT16(16384, p.quatW);
  TEST_ASSERT_EQUAL_INT16(0, p.quatX);
  TEST_ASSERT_EQUAL_INT16(0, p.quatY);
  TEST_ASSERT_EQUAL_INT16(0, p.quatZ);
}

void test_imu_encode_clamp() {
  const float q[4] = {2.f, -2.f, 0.f, 0.f};
  const int16_t acc[3] = {0, 0, 0};
  ImuPacket p = makeImuPacket(0, 0, q, acc, kImuQuatValid, AudioLevels{});
  TEST_ASSERT_EQUAL_INT16(32767, p.quatW);
  TEST_ASSERT_EQUAL_INT16(-32768, p.quatX);
  TEST_ASSERT_EQUAL_HEX8(kImuQuatValid, p.flags);
}

void test_imu_encodes_silence_as_the_db_floor() {
  const float q[4] = {1.f, 0.f, 0.f, 0.f};
  const int16_t acc[3] = {0, 0, 0};
  ImuPacket p = makeImuPacket(0, 0, q, acc, kImuQuatValid, AudioLevels{});
  for (int c = 0; c < 2; c++) {
    TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, p.audioRmsDbX100[c]);
    TEST_ASSERT_EQUAL_INT16(kAudioDbFloorX100, p.audioPeakDbX100[c]);
  }
  TEST_ASSERT_EQUAL_HEX8(kImuQuatValid, p.flags);  // the codec never sets kImuAudioValid
}

void test_co2_layout_golden() {
  Co2Packet p{};
  p.version = kProtocolVersion;
  p.type = static_cast<uint8_t>(PacketType::Co2Sample);
  p.seq = 258;
  p.timestampMs = 0x0A0B0C0Du;
  p.ppm = 1000;
  p.flags = kCo2Valid | kCo2VirtualActive;
  p.ageS = 7;
  const uint8_t expected[12] = {0x01, 0x02, 0x02, 0x01, 0x0D, 0x0C, 0x0B, 0x0A,
                                0xE8, 0x03, 0x05, 0x07};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reinterpret_cast<const uint8_t *>(&p), 12);
}

void test_diag_layout_golden() {
  DiagPacket p{};
  p.version = kProtocolVersion;
  p.type = static_cast<uint8_t>(PacketType::Diagnostics);
  p.seq = 1;
  p.uptimeS = 0x00010203u;
  p.imuHzX10 = 500;
  p.notifyHzX10 = 500;
  p.fifoOverflows = 1;
  p.i2sErrors = 2;
  p.uartFrameErrors = 3;
  p.freeHeapKb = 200;
  p.minFreeHeapKb = 150;
  p.flags = kDiagImuOk | kDiagAudioOk | kDiagCo2Ok;
  p.ahrsFlags = kAhrsStartup | kAhrsBiasAtRest;
  p.accelErrDeg = 3;
  p.magErrDeg = 12;
  p.gyroBiasX100[0] = 150;   // +1.50 dps
  p.gyroBiasX100[1] = -75;   // -0.75 dps
  p.gyroBiasX100[2] = 0;
  p.connItvlMsX10 = 150;  // 15.0 ms negotiated connection interval
  const uint8_t expected[34] = {0x01, 0x03, 0x01, 0x00, 0x03, 0x02, 0x01, 0x00,
                                0xF4, 0x01, 0xF4, 0x01, 0x01, 0x00, 0x02, 0x00,
                                0x03, 0x00, 0xC8, 0x00, 0x96, 0x00, 0x07, 0x11,
                                0x03, 0x0C, 0x96, 0x00, 0xB5, 0xFF, 0x00, 0x00,
                                0x96, 0x00};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reinterpret_cast<const uint8_t *>(&p), 34);
}


void test_mag_chunk_encode_golden() {
  const int16_t samples[6] = {100, -200, 300, 101, -201, 301};
  const MagChunkPacket p = makeMagChunkPacket(9, 0x01020304u, samples, 2, kMagChunkCalibrated);
  TEST_ASSERT_EQUAL(kProtocolVersion, p.version);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PacketType::MagChunk), p.type);
  TEST_ASSERT_EQUAL_UINT16(9, p.seq);
  TEST_ASSERT_EQUAL_UINT32(0x01020304u, p.timestampMs);
  TEST_ASSERT_EQUAL(2, p.count);
  TEST_ASSERT_EQUAL(kMagChunkCalibrated, p.flags);
  for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_INT16(samples[i], p.samples[i]);
  // Unused slots are zeroed so the packet never leaks stale samples.
  for (int i = 6; i < 3 * kMagChunkMaxSamples; i++) TEST_ASSERT_EQUAL_INT16(0, p.samples[i]);
}

void test_mag_chunk_clamps_count() {
  int16_t samples[3 * kMagChunkMaxSamples];
  for (int i = 0; i < 3 * kMagChunkMaxSamples; i++) samples[i] = static_cast<int16_t>(i);
  const MagChunkPacket p = makeMagChunkPacket(1, 2, samples, kMagChunkMaxSamples + 5, 0);
  TEST_ASSERT_EQUAL(kMagChunkMaxSamples, p.count);
}

void test_mag_chunk_size_fits_default_mtu() {
  // 106 bytes: fits the 23-byte ATT header plus a 185-byte payload on any MTU that
  // already carries the 208-byte audio chunk.
  TEST_ASSERT_EQUAL(106, sizeof(MagChunkPacket));
}

void test_nav_packet_size() { TEST_ASSERT_EQUAL(22, sizeof(NavPacket)); }

void test_nav_golden_a() {
  const NavPacket p = makeNavPacket(258, 12345678u, 1, kNavValid | kNavSettled, -229055560,
                                    -470608330, 4500, 123);
  const uint8_t expected[22] = {0x01, 0x07, 0x02, 0x01, 0x4E, 0x61, 0xBC, 0x00, 0x01, 0x03, 0xB8,
                                0xE3, 0x58, 0xF2, 0x36, 0x16, 0xF3, 0xE3, 0x94, 0x11, 0x7B, 0x00};
  uint8_t got[22];
  std::memcpy(got, &p, 22);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, got, 22);
}

void test_nav_golden_b() {
  const NavPacket p = makeNavPacket(65535, 4294967295u, 2, kNavSettled | kNavMoving, 900000000,
                                    -1800000000, -18000, 65535);
  const uint8_t expected[22] = {0x01, 0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x06, 0x00,
                                0xE9, 0xA4, 0x35, 0x00, 0x2E, 0xB6, 0x94, 0xB0, 0xB9, 0xFF, 0xFF};
  uint8_t got[22];
  std::memcpy(got, &p, 22);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, got, 22);
}

void test_nav_flag_constants() {
  TEST_ASSERT_EQUAL(0x01, kNavValid);
  TEST_ASSERT_EQUAL(0x02, kNavSettled);
  TEST_ASSERT_EQUAL(0x04, kNavMoving);
  TEST_ASSERT_EQUAL(0x08, kCo2Trigger);
  TEST_ASSERT_EQUAL(0x07, kTypeNav);
}

void test_nav_lat_conversion() {
  TEST_ASSERT_INT32_WITHIN(20, -229055560, navLatE7(-22.905556f));
  TEST_ASSERT_EQUAL_INT32(900000000, navLatE7(90.0f));
  TEST_ASSERT_EQUAL_INT32(900000000, navLatE7(123.0f));
  TEST_ASSERT_EQUAL_INT32(-900000000, navLatE7(-123.0f));
  TEST_ASSERT_EQUAL_INT32(0, navLatE7(0.0f));
  TEST_ASSERT_EQUAL_INT32(0, navLatE7(NAN));
  TEST_ASSERT_INT32_WITHIN(1, 450000000, navLatE7(45.0f));
}

void test_nav_lon_conversion() {
  TEST_ASSERT_EQUAL_INT32(1800000000, navLonE7(180.0f));
  TEST_ASSERT_EQUAL_INT32(1800000000, navLonE7(-180.0f));  // (-180,180] normalisation
  TEST_ASSERT_INT32_WITHIN(1, -1790000000, navLonE7(181.0f));
  TEST_ASSERT_INT32_WITHIN(1, 1790000000, navLonE7(-181.0f));
  TEST_ASSERT_INT32_WITHIN(1, 900000000, navLonE7(90.0f));
  TEST_ASSERT_INT32_WITHIN(1, 100000000, navLonE7(370.0f));
  TEST_ASSERT_EQUAL_INT32(0, navLonE7(NAN));
}

void test_nav_bearing_conversion() {
  TEST_ASSERT_EQUAL_INT16(18000, navBearingX100(180.0f));
  TEST_ASSERT_EQUAL_INT16(18000, navBearingX100(-180.0f));
  TEST_ASSERT_EQUAL_INT16(-17900, navBearingX100(181.0f));
  TEST_ASSERT_EQUAL_INT16(17900, navBearingX100(-181.0f));
  TEST_ASSERT_EQUAL_INT16(4500, navBearingX100(45.0f));
  TEST_ASSERT_EQUAL_INT16(-18000 + 1, navBearingX100(-179.99f));
  TEST_ASSERT_EQUAL_INT16(0, navBearingX100(NAN));
  TEST_ASSERT_EQUAL_INT16(0, navBearingX100(720.0f));
}

void test_nav_speed_conversion() {
  TEST_ASSERT_EQUAL_UINT16(0, navSpeedX10(0.0f));
  TEST_ASSERT_EQUAL_UINT16(123, navSpeedX10(12.3f));
  TEST_ASSERT_EQUAL_UINT16(0, navSpeedX10(-5.0f));
  TEST_ASSERT_EQUAL_UINT16(0, navSpeedX10(NAN));
  TEST_ASSERT_EQUAL_UINT16(65535, navSpeedX10(7000.0f));
  TEST_ASSERT_EQUAL_UINT16(65535, navSpeedX10(INFINITY));
}

void test_co2_trigger_flag_roundtrip_layout() {
  Co2Packet p{};
  p.flags = kCo2Valid | kCo2Trigger;
  uint8_t raw[12];
  std::memcpy(raw, &p, 12);
  TEST_ASSERT_EQUAL_HEX8(0x09, raw[10]);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_packet_sizes);
  RUN_TEST(test_imu_encode_golden);
  RUN_TEST(test_imu_encode_nan);
  RUN_TEST(test_imu_encode_clamp);
  RUN_TEST(test_sensors_encode_golden);
  RUN_TEST(test_sensors_encode_saturation_and_nan);
  RUN_TEST(test_imu_encodes_silence_as_the_db_floor);
  RUN_TEST(test_co2_layout_golden);
  RUN_TEST(test_diag_layout_golden);
  RUN_TEST(test_mag_chunk_encode_golden);
  RUN_TEST(test_mag_chunk_clamps_count);
  RUN_TEST(test_mag_chunk_size_fits_default_mtu);
  RUN_TEST(test_nav_packet_size);
  RUN_TEST(test_nav_golden_a);
  RUN_TEST(test_nav_golden_b);
  RUN_TEST(test_nav_flag_constants);
  RUN_TEST(test_nav_lat_conversion);
  RUN_TEST(test_nav_lon_conversion);
  RUN_TEST(test_nav_bearing_conversion);
  RUN_TEST(test_nav_speed_conversion);
  RUN_TEST(test_co2_trigger_flag_roundtrip_layout);
  return UNITY_END();
}
