#include <unity.h>
#include <cmath>
#include <cstring>
#include "../../Gaiaball/src/logic/Packets.h"
#include "../../Gaiaball/src/logic/PacketCodec.h"

using namespace gaia;

static_assert(sizeof(ImuPacket) == 23, "ImuPacket must be 23 bytes");
static_assert(sizeof(Co2Packet) == 12, "Co2Packet must be 12 bytes");
static_assert(sizeof(DiagPacket) == 23, "DiagPacket must be 23 bytes");

void setUp() {}
void tearDown() {}

void test_packet_sizes() {
  TEST_ASSERT_EQUAL(23, sizeof(ImuPacket));
  TEST_ASSERT_EQUAL(12, sizeof(Co2Packet));
  TEST_ASSERT_EQUAL(23, sizeof(DiagPacket));
}

void test_imu_encode_golden() {
  const float q[4] = {1.f, 0.f, 0.f, 0.f};
  const int16_t acc[3] = {1, -2, 3};
  ImuPacket p = makeImuPacket(258, 0x0A0B0C0Du, q, acc, 0b011);
  const uint8_t expected[23] = {
      0x01, 0x01,                          // version, type
      0x02, 0x01,                          // seq = 258
      0x0D, 0x0C, 0x0B, 0x0A,              // timestampUs
      0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // quat W=16384, X, Y, Z = 0
      0x01, 0x00, 0xFE, 0xFF, 0x03, 0x00,  // acc 1, -2, 3
      0x03};                               // flags
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reinterpret_cast<const uint8_t *>(&p), 23);
}

void test_imu_encode_nan() {
  const float q[4] = {NAN, 0.f, 0.f, 0.f};
  const int16_t acc[3] = {0, 0, 0};
  ImuPacket p = makeImuPacket(1, 2, q, acc, kImuQuatValid | kImuMoving);
  TEST_ASSERT_EQUAL_HEX8(kImuMoving, p.flags);
  TEST_ASSERT_EQUAL_INT16(16384, p.quatW);
  TEST_ASSERT_EQUAL_INT16(0, p.quatX);
  TEST_ASSERT_EQUAL_INT16(0, p.quatY);
  TEST_ASSERT_EQUAL_INT16(0, p.quatZ);
}

void test_imu_encode_clamp() {
  const float q[4] = {2.f, -2.f, 0.f, 0.f};
  const int16_t acc[3] = {0, 0, 0};
  ImuPacket p = makeImuPacket(0, 0, q, acc, kImuQuatValid);
  TEST_ASSERT_EQUAL_INT16(32767, p.quatW);
  TEST_ASSERT_EQUAL_INT16(-32768, p.quatX);
  TEST_ASSERT_EQUAL_HEX8(kImuQuatValid, p.flags);
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
  const uint8_t expected[23] = {0x01, 0x03, 0x01, 0x00, 0x03, 0x02, 0x01, 0x00,
                                0xF4, 0x01, 0xF4, 0x01, 0x01, 0x00, 0x02, 0x00,
                                0x03, 0x00, 0xC8, 0x00, 0x96, 0x00, 0x07};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, reinterpret_cast<const uint8_t *>(&p), 23);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_packet_sizes);
  RUN_TEST(test_imu_encode_golden);
  RUN_TEST(test_imu_encode_nan);
  RUN_TEST(test_imu_encode_clamp);
  RUN_TEST(test_co2_layout_golden);
  RUN_TEST(test_diag_layout_golden);
  return UNITY_END();
}
