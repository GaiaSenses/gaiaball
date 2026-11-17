#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gaia {

// Command 0x17 "anchor" (docs/protocol.md): a client's current map view becomes the
// navigation reference for this session. RAM only: never a config register, never
// persisted by 0x11 / 0x14, gone after a reboot.
struct NavAnchor {
  int32_t latE6;        // degrees x 1e6, -90000000..90000000
  int32_t lonE6;        // degrees x 1e6, -180000000..180000000
  int16_t bearingX100;  // Mapbox bearing, degrees x 100, -18000..18000
};

// Argument lengths (opcode excluded): i32 latE6, i32 lonE6, then the optional
// i16 bearingX100 (absent = bearing 0). All little-endian.
constexpr size_t kAnchorArgLen = 10;
constexpr size_t kAnchorArgLenNoBearing = 8;

// Decodes and validates the argument. False (and *out untouched) on a null pointer, on
// any length other than 8 or 10, or on a value outside its range.
bool parseAnchorArgs(const uint8_t *arg, size_t len, NavAnchor *out);

}  // namespace gaia
