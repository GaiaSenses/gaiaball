#pragma once
#include <stdint.h>

#include "ImuFrontEnd.h"

// Read-only status register imuDetected (TLV 0x35). It is NOT in the TlvCodec table: it is
// appended to the config-read buffer by BleService::onConfigRead, so a client write of it
// is an unknown id (the whole write fails) and it is never persisted, hence never stale.
namespace gaia {

// bits 0..1: the ImuChip in force at BOOT; bit 2: the sensor was actually found.
constexpr uint8_t kImuFound = 1u << 2;

// C++11 constexpr: a single return statement.
constexpr uint8_t imuStatusByte(ImuChip bootChip, bool found) {
  return static_cast<uint8_t>((static_cast<uint8_t>(bootChip) & 0x03u) | (found ? kImuFound : 0u));
}

}  // namespace gaia
