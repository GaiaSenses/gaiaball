#pragma once
#include <stddef.h>
#include <stdint.h>
#include "GaiaConfig.h"

// TLV records: id u8 | len u8 | value (little-endian). See firmware/docs/protocol.md.
namespace gaia {

constexpr uint8_t kTlvPin = 0x41;

// Emits every register except 0x41 (PIN). Returns bytes written, or 0 if `cap`
// is too small (nothing is guaranteed about `out` in that case).
size_t tlvSerialize(const GaiaConfig &cfg, uint8_t *out, size_t cap);

// Validates the ENTIRE buffer (known ids, exact lengths, value ranges) before
// touching `cfg`. On any violation returns false and `cfg` is unchanged.
// Duplicate ids are allowed; the last one wins.
bool tlvApply(GaiaConfig &cfg, const uint8_t *data, size_t len);

}  // namespace gaia
