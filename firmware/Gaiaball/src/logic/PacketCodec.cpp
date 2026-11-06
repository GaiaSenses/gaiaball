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
                        uint8_t flags) {
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
  return p;
}

}  // namespace gaia
