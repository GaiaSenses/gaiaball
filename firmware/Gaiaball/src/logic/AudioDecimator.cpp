#include "AudioDecimator.h"

#include <math.h>

namespace gaia {

void AudioDecimator::reset() {
  acc_ = 0.0f;
  accN_ = 0;
  n_ = 0;
  for (size_t i = 0; i < kChunkSamples; i++) out_[i] = 0;
}

bool AudioDecimator::feed(float sample) {
  acc_ += sample;
  if (++accN_ < kDecim) return false;
  float v = (acc_ / (float)kDecim) / 256.0f;  // 24-bit counts -> int16 range
  acc_ = 0.0f;
  accN_ = 0;
  if (v > 32767.0f) v = 32767.0f;
  if (v < -32768.0f) v = -32768.0f;
  out_[n_++] = (int16_t)lroundf(v);
  if (n_ < kChunkSamples) return false;
  n_ = 0;
  return true;
}

}  // namespace gaia
