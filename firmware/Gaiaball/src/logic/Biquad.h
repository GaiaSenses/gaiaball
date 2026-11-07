#pragma once
#include <stdint.h>

namespace gaia {

// RBJ audio-cookbook biquad (Direct Form II transposed), float, host-testable.
// Used as configurable high-pass + low-pass on the microphone stream so the
// breath band (HP..LP) feeds the peak detector. NOTE: Q > 0.707 adds a resonant
// peak of roughly 20*log10(Q) dB AT the cutoff frequency; steeper roll-off comes
// from filter order, not from Q. Q is exposed to the UI so the band edges can be
// tuned against the live spectrum.
class Biquad {
 public:
  enum class Type : uint8_t { LowPass, HighPass };

  // fs in Hz; hz clamped to [1, 0.45*fs]; q clamped to [0.1, 20].
  void configure(Type type, float hz, float q, float fs);

  float process(float x) {
    const float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
  }

  void reset() { z1_ = z2_ = 0.0f; }

 private:
  float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
  float z1_ = 0.0f, z2_ = 0.0f;
};

}  // namespace gaia
