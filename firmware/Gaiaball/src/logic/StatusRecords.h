#pragma once
#include <cstddef>
#include <cstdint>

namespace gaia {

// Read-only status records the firmware puts in every config read (BleService::onConfigRead).
// They are NOT registers: no NVS key, no codec entry, and a config write that contains either
// id is rejected whole.
constexpr uint8_t kTlvImuDetected = 0x35;  // u8, see ImuStatus.h
constexpr uint8_t kTlvUnlockState = 0x37;  // u8, GateState of the READING connection
constexpr size_t kStatusRecordsLen = 6;    // both records, 3 bytes each

// Writes `0x35 imuStatus` then `0x37 unlockState` at buf[0] and returns kStatusRecordsLen, or 0
// (nothing written) if cap is smaller. They go FIRST, before the serialized config: NimBLE runs
// onRead only for the first chunk of a long read and serves the following blob reads from one
// value shared by every central, so a per-connection byte must sit in the first chunk (6 bytes
// fit even at the minimum MTU of 23).
size_t writeStatusRecords(uint8_t *buf, size_t cap, uint8_t imuStatus, uint8_t unlockState);

}  // namespace gaia
