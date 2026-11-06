#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gaia {

struct StereoRms {
  float left, right;
};

// RMS of an interleaved stereo block (L,R,L,R,...) of left-aligned 32-bit I2S words.
// Per channel: sample >> 8 (24-bit data), subtract that channel's mean over THIS block
// (DC removal), RMS of the residuals. Result is in 24-bit sample units.
// count = total int32 samples; an odd trailing sample is ignored; count < 4 -> {0,0}.
StereoRms computeStereoRms(const int32_t *interleaved, size_t count);

}  // namespace gaia
