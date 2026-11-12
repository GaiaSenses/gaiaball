#include "MagAxisRemap.h"

namespace gaia {

namespace {

struct MagRemapEntry {
  int8_t src[3];  // source axis (0=X, 1=Y, 2=Z) for each output axis
  int8_t sgn[3];  // +1 / -1
  char label[9];  // "+Y +X -Z"
};

// Generated from the constexpr builder the host tests were first written against, not typed
// by hand: permutation order XYZ, XZY, YXZ, YZX, ZXY, ZYX, then sign patterns (see header).
constexpr MagRemapEntry kMagRemapTable[kMagRemapCount] = {
    {{0, 1, 2}, {1, 1, 1}, "+X +Y +Z"},
    {{0, 1, 2}, {1, -1, -1}, "+X -Y -Z"},
    {{0, 1, 2}, {-1, 1, -1}, "-X +Y -Z"},
    {{0, 1, 2}, {-1, -1, 1}, "-X -Y +Z"},
    {{0, 2, 1}, {1, 1, -1}, "+X +Z -Y"},
    {{0, 2, 1}, {1, -1, 1}, "+X -Z +Y"},
    {{0, 2, 1}, {-1, 1, 1}, "-X +Z +Y"},
    {{0, 2, 1}, {-1, -1, -1}, "-X -Z -Y"},
    {{1, 0, 2}, {1, 1, -1}, "+Y +X -Z"},
    {{1, 0, 2}, {1, -1, 1}, "+Y -X +Z"},
    {{1, 0, 2}, {-1, 1, 1}, "-Y +X +Z"},
    {{1, 0, 2}, {-1, -1, -1}, "-Y -X -Z"},
    {{1, 2, 0}, {1, 1, 1}, "+Y +Z +X"},
    {{1, 2, 0}, {1, -1, -1}, "+Y -Z -X"},
    {{1, 2, 0}, {-1, 1, -1}, "-Y +Z -X"},
    {{1, 2, 0}, {-1, -1, 1}, "-Y -Z +X"},
    {{2, 0, 1}, {1, 1, 1}, "+Z +X +Y"},
    {{2, 0, 1}, {1, -1, -1}, "+Z -X -Y"},
    {{2, 0, 1}, {-1, 1, -1}, "-Z +X -Y"},
    {{2, 0, 1}, {-1, -1, 1}, "-Z -X +Y"},
    {{2, 1, 0}, {1, 1, -1}, "+Z +Y -X"},
    {{2, 1, 0}, {1, -1, 1}, "+Z -Y +X"},
    {{2, 1, 0}, {-1, 1, 1}, "-Z +Y +X"},
    {{2, 1, 0}, {-1, -1, -1}, "-Z -Y -X"},
};

static_assert(kMagRemapTable[kMagRemapDefault].src[0] == 1 && kMagRemapTable[kMagRemapDefault].src[1] == 0 &&
                  kMagRemapTable[kMagRemapDefault].src[2] == 2 && kMagRemapTable[kMagRemapDefault].sgn[0] == 1 &&
                  kMagRemapTable[kMagRemapDefault].sgn[1] == 1 && kMagRemapTable[kMagRemapDefault].sgn[2] == -1,
              "default remap must stay {+Y, +X, -Z}");

}  // namespace

void applyMagRemap(uint8_t index, const float in[3], float out[3]) {
  const MagRemapEntry &e = kMagRemapTable[index < kMagRemapCount ? index : kMagRemapDefault];
  for (int k = 0; k < 3; ++k) out[k] = e.sgn[k] > 0 ? in[e.src[k]] : -in[e.src[k]];
}

const char *magRemapLabel(uint8_t index) {
  return kMagRemapTable[index < kMagRemapCount ? index : kMagRemapDefault].label;
}

}  // namespace gaia
