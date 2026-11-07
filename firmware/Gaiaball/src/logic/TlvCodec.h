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

// Lenient apply for PERSISTED data (NVS): applies each individually valid record and
// skips unknown ids or invalid values instead of rejecting the buffer, so a config
// saved by one firmware version loads on another (ids are stable; see protocol.md).
// Stops at a truncated record. The PIN record (0x41) is never applied — the PIN is
// persisted under its own NVS key. After the pass the cross-field invariants are
// re-established by restoring the affected fields' defaults (distinct LED pins,
// lpHz > hpHz). Returns the number of records applied.
size_t tlvApplyLenient(GaiaConfig &cfg, const uint8_t *data, size_t len);

}  // namespace gaia
