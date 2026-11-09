#include "PacketCodec.h"
#include <math.h>

namespace gaia {

static int16_t encodeQ14(float v) {
  float s = v * kQuatScale;
  if (s >= 32767.0f) return 32767;
  if (s <= -32768.0f) return -32768;
  return static_cast<int16_t>(lroundf(s));
}

ImuPacket makeImuPacket(uint16_t seq, uint32_t tUs, const float q[4], const int16_t acc[3],
                        uint8_t flags, const AudioLevels &audio) {
  ImuPacket p{};
  p.version = kProtocolVersion;
  p.type = static_cast<uint8_t>(PacketType::ImuSample);
  p.seq = seq;
  p.timestampUs = tUs;
  bool finite = true;
  for (int i = 0; i < 4; ++i) finite = finite && isfinite(q[i]);
  if (finite) {
    p.quatW = encodeQ14(q[0]);
    p.quatX = encodeQ14(q[1]);
    p.quatY = encodeQ14(q[2]);
    p.quatZ = encodeQ14(q[3]);
  } else {
    p.quatW = encodeQ14(1.0f);
    flags = static_cast<uint8_t>(flags & ~kImuQuatValid);
  }
  p.accX = acc[0];
  p.accY = acc[1];
  p.accZ = acc[2];
  p.flags = flags;
  for (int c = 0; c < 2; ++c) {
    p.audioRmsDbX100[c] = rmsToDbfsX100(audio.rms[c]);
    p.audioPeakDbX100[c] = peakToDbfsX100(audio.peak[c]);
  }
  return p;
}

static uint32_t encodeU32(float v) {
  if (!isfinite(v) || v <= 0.0f) return 0;
  if (v >= 4294967040.0f) return 0xFFFFFFFFu;  // largest float below 2^32
  return static_cast<uint32_t>(lroundf(v));
}

static uint16_t encodeU16(float v) {
  if (!isfinite(v) || v <= 0.0f) return 0;
  if (v >= 65535.0f) return 0xFFFF;
  return static_cast<uint16_t>(lroundf(v));
}

SensorsPacket makeSensorsPacket(uint16_t seq, uint32_t tMs, float rmsL, float rmsR,
                                float threshold, float potRaw, uint8_t flags) {
  SensorsPacket p{};
  p.version = kProtocolVersion;
  p.type = static_cast<uint8_t>(PacketType::Sensors);
  p.seq = seq;
  p.timestampMs = tMs;
  p.audioRmsL = encodeU32(rmsL);
  p.audioRmsR = encodeU32(rmsR);
  p.audioThreshold = encodeU32(threshold);
  p.potRaw = encodeU16(potRaw);
  p.flags = flags;
  return p;
}

MagChunkPacket makeMagChunkPacket(uint16_t seq, uint32_t tMs, const int16_t *samples,
                                  uint8_t count, uint8_t flags) {
  MagChunkPacket p{};
  p.version = kProtocolVersion;
  p.type = static_cast<uint8_t>(PacketType::MagChunk);
  p.seq = seq;
  p.timestampMs = tMs;
  if (count > kMagChunkMaxSamples) count = kMagChunkMaxSamples;
  p.count = count;
  p.flags = flags;
  if (samples != nullptr) {
    for (int i = 0; i < 3 * count; ++i) p.samples[i] = samples[i];
  }
  return p;
}

}  // namespace gaia
