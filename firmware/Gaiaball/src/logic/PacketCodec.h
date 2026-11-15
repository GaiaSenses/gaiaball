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

// Pure packing of a navigation pose; the caller supplies already-converted integers.
NavPacket makeNavPacket(uint16_t seq, uint32_t tUs, uint8_t mode, uint8_t flags, int32_t latE7,
                        int32_t lonE7, int16_t bearingX100, uint16_t speedDegSX10);

// Conversions from float degrees to the NavPacket integer units: rounded, saturated,
// and NaN/inf encode as 0 (speed: negative also 0). Latitude clamps to +-90; longitude
// and bearing are normalised to (-180, 180] (so -180 encodes as +180).
int32_t navLatE7(float deg);
int32_t navLonE7(float deg);
int16_t navBearingX100(float deg);
uint16_t navSpeedX10(float degS);

}  // namespace gaia
