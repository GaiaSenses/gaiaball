#pragma once
#include <stdint.h>

// Binary BLE wire protocol v1. Little-endian, packed. See firmware/docs/protocol.md.
namespace gaia {

constexpr uint8_t kProtocolVersion = 1;

enum class PacketType : uint8_t {
  ImuSample = 0x01,
  Co2Sample = 0x02,
  Diagnostics = 0x03,
  Sensors = 0x04,     // audio levels + potentiometer; shares the diag characteristic
  AudioChunk = 0x05,  // decimated raw audio stream; shares the diag characteristic
};

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

// SensorsPacket.flags
constexpr uint8_t kSensAudioOk = 1u << 0;
constexpr uint8_t kSensAudioEnabled = 1u << 1;
constexpr uint8_t kSensPotControls = 1u << 2;
constexpr uint8_t kSensAudioFiltered = 1u << 3;  // RMS computed after the HP/LP band-pass

// AudioChunkPacket.flags
constexpr uint8_t kAudioBpfEnabled = 1u << 0;  // band-pass active (chunk itself is RAW)

// Quaternion scale: int16 = round(float * 16384) (Q14).
constexpr float kQuatScale = 16384.0f;

struct __attribute__((packed)) ImuPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampUs;
  int16_t quatW, quatX, quatY, quatZ;
  int16_t accX, accY, accZ;  // raw MPU-9250 counts (±4 g full scale, 8192 LSB/g)
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

// Live sensor levels for the config-app charts, notified on the diag characteristic
// at 5 Hz. RMS values are in 24-bit sample counts (INMP441 data, sample >> 8; full
// scale 2^23): dBFS = 20*log10(rms*sqrt(2)/2^23). Threshold is in the same RMS
// counts; potRaw is the EMA-filtered 12-bit ADC reading (0..4095).
struct __attribute__((packed)) SensorsPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint32_t audioRmsL, audioRmsR;
  uint32_t audioThreshold;
  uint16_t potRaw;
  uint8_t flags;
};

// Decimated RAW audio for the config-app's spectrum analyzer (the FFT runs in the
// frontend). Mono mix (L+R)/2, DC-removed, 16 kHz -> 2 kHz by average-of-8, int16
// (top 16 bits of the 24-bit samples). 96 samples = 48 ms per packet (~21 pkt/s,
// ~4.4 kB/s). hpHz/lpHz echo the configured band-pass so the UI can draw the
// cutoffs without a config read. Needs MTU >= 211.
struct __attribute__((packed)) AudioChunkPacket {
  uint8_t version, type;
  uint16_t seq;
  uint32_t timestampMs;
  uint16_t sampleRateHz;  // 2000
  uint8_t count;          // 96
  uint8_t flags;
  uint16_t hpHz, lpHz;
  int16_t samples[96];
};

static_assert(sizeof(ImuPacket) == 23, "ImuPacket size");
static_assert(sizeof(Co2Packet) == 12, "Co2Packet size");
static_assert(sizeof(DiagPacket) == 23, "DiagPacket size");
static_assert(sizeof(SensorsPacket) == 23, "SensorsPacket size");
static_assert(sizeof(AudioChunkPacket) == 208, "AudioChunkPacket size");

}  // namespace gaia
