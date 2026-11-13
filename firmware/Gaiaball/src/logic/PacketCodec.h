#pragma once
#include <stdint.h>
#include "AudioFeatures.h"
#include "Packets.h"

namespace gaia {

// Encodes q (w,x,y,z; unit quaternion) to Q14 with saturation. If any component is
// non-finite, encodes identity and clears kImuQuatValid in flags.
// `audio` holds the linear microphone levels of the window that ended with this sample
// (24-bit sample units); they are encoded to dBFS x100 here. Silence, zero and
// non-finite levels encode as kAudioDbFloorX100. Declaring the levels live is the
// caller's job (kImuAudioValid): this function never sets that flag.
ImuPacket makeImuPacket(uint16_t seq, uint32_t tUs, const float q[4], const int16_t acc[3],
                        uint8_t flags, const AudioLevels &audio);

// Encodes the live sensor levels. Floats are rounded and saturated to the integer
// field ranges; non-finite inputs encode as 0.
SensorsPacket makeSensorsPacket(uint16_t seq, uint32_t tMs, float rmsL, float rmsR,
                                float threshold, float potRaw, uint8_t flags);

// Packs up to kMagChunkMaxSamples raw magnetometer triplets (0.1 uT per LSB, x/y/z
// interleaved). `count` is clamped and the unused tail is zeroed, so a short chunk
// never leaks the previous one's samples.
MagChunkPacket makeMagChunkPacket(uint16_t seq, uint32_t tMs, const int16_t *samples,
                                  uint8_t count, uint8_t flags);

}  // namespace gaia
