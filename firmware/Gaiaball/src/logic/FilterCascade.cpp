#include "FilterCascade.h"

#include <math.h>

namespace gaia {

bool FilterCascade::validSlope(uint8_t dbPerOct) {
  switch (dbPerOct) {
    case 6: case 12: case 18: case 24: case 36: case 48: return true;
    default: return false;
  }
}

void FilterCascade::configure(Type type, float hz, float q, uint8_t slopeDbOct, float fs) {
  if (!validSlope(slopeDbOct)) slopeDbOct = kDefaultSlopeDbOct;
  if (hz < 1.0f) hz = 1.0f;
  if (hz > 0.45f * fs) hz = 0.45f * fs;
  if (q < 0.1f) q = 0.1f;
  if (q > 20.0f) q = 20.0f;

  const int order = slopeDbOct / 6;
  const uint8_t n = static_cast<uint8_t>(order / 2);
  const bool onePole = (order % 2) != 0;
  if (n != n_ || onePole != onePole_ || (type == Type::HighPass) != highPass_) {
    n_ = n;
    onePole_ = onePole;
    highPass_ = type == Type::HighPass;
    reset();
  }

  // Prewarped analog cutoff: the bilinear transform maps it exactly onto hz.
  const float g = tanf((float)M_PI * hz / fs);
  g1_ = g / (1.0f + g);
  for (uint8_t i = 0; i < n_; i++) {
    // Butterworth section Qs, highest first: 1 / (2 sin((2i+1) pi / (2 order))).
    float sq = 1.0f / (2.0f * sinf((float)(2 * i + 1) * (float)M_PI / (float)(2 * order)));
    if (i == 0) sq *= q * (float)M_SQRT2;  // the gain at the cutoff becomes q
    Section &s = sec_[i];
    s.k = 1.0f / sq;
    s.a1 = 1.0f / (1.0f + g * (g + s.k));
    s.a2 = g * s.a1;
    s.a3 = g * s.a2;
  }
}

void FilterCascade::reset() {
  for (Section &s : sec_) s.ic1 = s.ic2 = 0.0f;
  s1_ = 0.0f;
}

}  // namespace gaia
