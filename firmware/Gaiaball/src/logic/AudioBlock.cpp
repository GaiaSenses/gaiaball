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
  const double meanL = (double)sumL / (double)frames;
  const double meanR = (double)sumR / (double)frames;
  double accL = 0.0, accR = 0.0;
  for (size_t i = 0; i < frames; i++) {
    const double dl = (double)(interleaved[2 * i] >> 8) - meanL;
    const double dr = (double)(interleaved[2 * i + 1] >> 8) - meanR;
    accL += dl * dl;
    accR += dr * dr;
  }
  return {(float)sqrt(accL / (double)frames), (float)sqrt(accR / (double)frames)};
}

}  // namespace gaia
