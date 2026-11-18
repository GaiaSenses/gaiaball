#include "AnchorCommand.h"

namespace gaia {

namespace {
constexpr int32_t kMaxLatE6 = 90000000;
constexpr int32_t kMaxLonE6 = 180000000;
constexpr int16_t kMaxBearingX100 = 18000;

uint32_t readLe(const uint8_t *p, int n) {
  uint32_t v = 0;
  for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
  return v;
}
}  // namespace

bool parseAnchorArgs(const uint8_t *arg, size_t len, NavAnchor *out) {
  if (arg == nullptr || out == nullptr) return false;
  if (len != kAnchorArgLen && len != kAnchorArgLenNoBearing) return false;
  const int32_t lat = (int32_t)readLe(arg, 4);
  const int32_t lon = (int32_t)readLe(arg + 4, 4);
  const int16_t bearing = len == kAnchorArgLen ? (int16_t)(uint16_t)readLe(arg + 8, 2) : 0;
  if (lat < -kMaxLatE6 || lat > kMaxLatE6) return false;
  if (lon < -kMaxLonE6 || lon > kMaxLonE6) return false;
  if (bearing < -kMaxBearingX100 || bearing > kMaxBearingX100) return false;
  out->latE6 = lat;
  out->lonE6 = lon;
  out->bearingX100 = bearing;
  return true;
}

}  // namespace gaia
