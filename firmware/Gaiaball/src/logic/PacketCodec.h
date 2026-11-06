#pragma once
#include <stdint.h>
#include "Packets.h"

namespace gaia {

// Encodes q (w,x,y,z; unit quaternion) to Q14 with saturation. If any component is
// non-finite, encodes identity and clears kImuQuatValid in flags.
ImuPacket makeImuPacket(uint16_t seq, uint32_t tUs, const float q[4], const int16_t acc[3],
                        uint8_t flags);

}  // namespace gaia
