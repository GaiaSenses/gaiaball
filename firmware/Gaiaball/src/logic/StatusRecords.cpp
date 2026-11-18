#include "StatusRecords.h"

namespace gaia {

size_t appendStatusRecords(uint8_t *buf, size_t n, size_t cap, uint8_t imuStatus,
                           uint8_t unlockState) {
  const uint8_t records[2][2] = {{kTlvImuDetected, imuStatus}, {kTlvUnlockState, unlockState}};
  for (const auto &r : records) {
    if (n + 3 > cap) break;
    buf[n++] = r[0];
    buf[n++] = 1;
    buf[n++] = r[1];
  }
  return n;
}

}  // namespace gaia
