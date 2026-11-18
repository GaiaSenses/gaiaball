#pragma once
#include <cstddef>
#include <cstdint>

namespace gaia {

// Read-only status records the firmware appends after the serialized configuration on
// every config read (BleService::onConfigRead). They are NOT registers: no NVS key, no
// codec entry, and a config write that contains either id is rejected whole.
constexpr uint8_t kTlvImuDetected = 0x35;  // u8, see ImuStatus.h
constexpr uint8_t kTlvUnlockState = 0x37;  // u8, GateState of the READING connection

// Appends `0x35 imuStatus` and then `0x37 unlockState` (3 bytes each) to buf[0..n) and
// returns the new length. A record that would not fit in `cap` is skipped, in that order
// (the full config is far below the 384 B read buffer, so in practice both always fit).
size_t appendStatusRecords(uint8_t *buf, size_t n, size_t cap, uint8_t imuStatus,
                           uint8_t unlockState);

}  // namespace gaia
