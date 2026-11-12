#pragma once
#include <stdint.h>

// Magnetometer axis remap. The AK09916 (ICM-20948) and AK8963 (MPU-9250) sit in different
// orientations relative to the gyro/accel frame, and the right relationship cannot be known
// without the hardware in hand. So the remap is a configurable index into the 24 proper
// signed axis permutations (determinant +1) instead of a hard-coded literal. Improper ones
// (determinant -1) are excluded on purpose: they mirror the field, which no physical mounting
// can do, and would silently flip the heading's handedness.
//
// Order, chosen so an operator stepping through a dropdown can predict the next entry:
// the output-axis permutation is the outer key, in lexicographic order (XYZ, XZY, YXZ, YZX,
// ZXY, ZYX); within one permutation the sign patterns count upward from "+++" with '+' before
// '-' on the leftmost axis varying slowest, keeping only those whose determinant is +1.
// Each permutation therefore contributes a group of four. For XYZ (indices 0-3) the signs are
// +++, +--, -+-, --+, i.e. "+X +Y +Z", "+X -Y -Z", "-X +Y -Z", "-X -Y +Z". Index 0 is the
// identity. The default {+Y, +X, -Z} is index 8, the first entry of the third group (YXZ).
//
// A label reads "<sign><source axis>" per output axis, out = {x, y, z}: "+Y +X -Z" means
// out.x = +in.y, out.y = +in.x, out.z = -in.z.

namespace gaia {

namespace magremap_detail {

struct Entry {
  int8_t src[3];  // source axis for each output axis
  int8_t sgn[3];  // +1 / -1
  char label[9];  // "+Y +X -Z"
};

struct Table {
  Entry e[24];
};

constexpr Table build() {
  constexpr int8_t perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2},
                                  {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  constexpr int8_t permSign[6] = {1, -1, -1, 1, 1, -1};  // parity of each permutation
  Table t{};
  int n = 0;
  for (int p = 0; p < 6; ++p) {
    for (int bits = 0; bits < 8; ++bits) {  // bit 2 = leftmost axis; set bit = negative
      const int8_t s0 = (bits & 4) ? -1 : 1;
      const int8_t s1 = (bits & 2) ? -1 : 1;
      const int8_t s2 = (bits & 1) ? -1 : 1;
      if (permSign[p] * s0 * s1 * s2 != 1) continue;
      Entry &en = t.e[n++];
      const int8_t sg[3] = {s0, s1, s2};
      for (int k = 0; k < 3; ++k) {
        en.src[k] = perms[p][k];
        en.sgn[k] = sg[k];
        en.label[k * 3] = sg[k] < 0 ? '-' : '+';
        en.label[k * 3 + 1] = static_cast<char>('X' + perms[p][k]);
        en.label[k * 3 + 2] = k < 2 ? ' ' : '\0';
      }
    }
  }
  return t;
}

inline constexpr Table kTable = build();

constexpr uint8_t findIndex(const int8_t src[3], const int8_t sgn[3]) {
  for (uint8_t i = 0; i < 24; ++i) {
    bool same = true;
    for (int k = 0; k < 3; ++k) {
      same = same && kTable.e[i].src[k] == src[k] && kTable.e[i].sgn[k] == sgn[k];
    }
    if (same) return i;
  }
  return 0;
}

inline constexpr int8_t kDefaultSrc[3] = {1, 0, 2};
inline constexpr int8_t kDefaultSgn[3] = {1, 1, -1};

}  // namespace magremap_detail

inline constexpr uint8_t kMagRemapCount = 24;
// {+Y, +X, -Z}: the literal ImuService hard-coded before this table existed, so a ball that
// never touches the setting behaves exactly as before.
inline constexpr uint8_t kMagRemapDefault =
    magremap_detail::findIndex(magremap_detail::kDefaultSrc, magremap_detail::kDefaultSgn);
static_assert(kMagRemapDefault == 8, "default remap must stay {+Y, +X, -Z}");

// Out-of-range indices behave as kMagRemapDefault.
inline void applyMagRemap(uint8_t index, const float in[3], float out[3]) {
  const magremap_detail::Entry &e =
      magremap_detail::kTable.e[index < kMagRemapCount ? index : kMagRemapDefault];
  const float v[3] = {in[0], in[1], in[2]};
  for (int k = 0; k < 3; ++k) out[k] = e.sgn[k] > 0 ? v[e.src[k]] : -v[e.src[k]];
}

// For log lines and documentation; never null.
inline const char *magRemapLabel(uint8_t index) {
  return magremap_detail::kTable
      .e[index < kMagRemapCount ? index : kMagRemapDefault]
      .label;
}

}  // namespace gaia
