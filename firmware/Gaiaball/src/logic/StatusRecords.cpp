#include "StatusRecords.h"

namespace gaia {

size_t writeStatusRecords(uint8_t *buf, size_t cap, uint8_t imuStatus, uint8_t unlockState) {
  if (cap < kStatusRecordsLen) return 0;
  const uint8_t records[kStatusRecordsLen] = {kTlvImuDetected, 1, imuStatus,
                                              kTlvUnlockState, 1, unlockState};
  for (size_t i = 0; i < kStatusRecordsLen; i++) buf[i] = records[i];
  return kStatusRecordsLen;
}

}  // namespace gaia
