#include "AudioBlock.h"

#include <math.h>

namespace gaia {

StereoRms computeStereoRms(const int32_t *interleaved, size_t count) {
  if (interleaved == nullptr || count < 4) return {0.0f, 0.0f};
  const size_t frames = count / 2;
  int64_t sumL = 0, sumR = 0;
  for (size_t i = 0; i < frames; i++) {
    sumL += interleaved[2 * i] >> 8;
    sumR += interleaved[2 * i + 1] >> 8;
  }
  // float, not double: the ESP32's FPU is single-precision only, so every double
  // operation here was emulated in software. With samples up to 2^23 and a few hundred
  // terms the relative error stays near 1e-5 (< 0.0001 dB), far below the 0.01 dB
  // resolution of the wire format. Pinned by test_rms_float_accumulator_precision.
  const float meanL = (float)sumL / (float)frames;
  const float meanR = (float)sumR / (float)frames;
  float accL = 0.0f, accR = 0.0f;
  for (size_t i = 0; i < frames; i++) {
    const float dl = (float)(interleaved[2 * i] >> 8) - meanL;
    const float dr = (float)(interleaved[2 * i + 1] >> 8) - meanR;
    accL += dl * dl;
    accR += dr * dr;
  }
  return {sqrtf(accL / (float)frames), sqrtf(accR / (float)frames)};
}

}  // namespace gaia
