#pragma once
#include <stdint.h>

// Binary BLE wire protocol v1. Little-endian, packed. See firmware/docs/protocol.md.
namespace gaia {

constexpr uint8_t kProtocolVersion = 1;

enum class PacketType : uint8_t { ImuSample = 0x01, Co2Sample = 0x02, Diagnostics = 0x03 };

// ImuPacket.flags
constexpr uint8_t kImuQuatValid = 1u << 0;
constexpr uint8_t kImuNineAxis = 1u << 1;
constexpr uint8_t kImuMoving = 1u << 2;

// Co2Packet.flags
constexpr uint8_t kCo2Valid = 1u << 0;
constexpr uint8_t kCo2Stale = 1u << 1;
constexpr uint8_t kCo2VirtualActive = 1u << 2;

// DiagPacket.flags
constexpr uint8_t kDiagImuOk = 1u << 0;
constexpr uint8_t kDiagAudioOk = 1u << 1;
constexpr uint8_t kDiagCo2Ok = 1u << 2;

// Quaternion scale: int16 = round(float * 16384) (Q14).
constexpr float kQuatScale = 16384.0f;

struct __attribute__((packed)) ImuPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampUs;
  int16_t quatW, quatX, quatY, quatZ;
  int16_t accX, accY, accZ;  // raw DMP counts
  uint8_t flags;
};

struct __attribute__((packed)) Co2Packet {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint16_t ppm;
  uint8_t flags;
  uint8_t ageS;
};

struct __attribute__((packed)) DiagPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t uptimeS;
  uint16_t imuHzX10, notifyHzX10, fifoOverflows, i2sErrors, uartFrameErrors, freeHeapKb,
      minFreeHeapKb;
  uint8_t flags;
};

static_assert(sizeof(ImuPacket) == 23, "ImuPacket size");
static_assert(sizeof(Co2Packet) == 12, "Co2Packet size");
static_assert(sizeof(DiagPacket) == 23, "DiagPacket size");

}  // namespace gaia
