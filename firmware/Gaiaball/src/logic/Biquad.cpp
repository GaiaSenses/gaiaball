#include "Biquad.h"

#include <math.h>

namespace gaia {

void Biquad::configure(Type type, float hz, float q, float fs) {
  if (hz < 1.0f) hz = 1.0f;
  if (hz > 0.45f * fs) hz = 0.45f * fs;
  if (q < 0.1f) q = 0.1f;
  if (q > 20.0f) q = 20.0f;

  const float w0 = 2.0f * (float)M_PI * hz / fs;
  const float cw = cosf(w0);
  const float sw = sinf(w0);
  const float alpha = sw / (2.0f * q);
  const float a0 = 1.0f + alpha;

  if (type == Type::LowPass) {
    b0_ = (1.0f - cw) * 0.5f / a0;
    b1_ = (1.0f - cw) / a0;
    b2_ = b0_;
  } else {  // HighPass
    b0_ = (1.0f + cw) * 0.5f / a0;
    b1_ = -(1.0f + cw) / a0;
    b2_ = b0_;
  }
  a1_ = (-2.0f * cw) / a0;
  a2_ = (1.0f - alpha) / a0;
  reset();
}

}  // namespace gaia
