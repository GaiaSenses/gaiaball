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

// The table itself lives in MagAxisRemap.cpp: the device build is C++11, which has neither
// inline variables nor constexpr loops, and one definition keeps it ODR-clean.
namespace gaia {

constexpr uint8_t kMagRemapCount = 24;
// {+Y, +X, -Z}: the literal ImuService hard-coded before this table existed, so a ball that
// never touches the setting behaves exactly as before. Pinned by a static_assert in the .cpp.
constexpr uint8_t kMagRemapDefault = 8;

// Out-of-range indices behave as kMagRemapDefault. `in` and `out` may alias.
void applyMagRemap(uint8_t index, const float in[3], float out[3]);

// For log lines and documentation; never null.
const char *magRemapLabel(uint8_t index);

}  // namespace gaia
