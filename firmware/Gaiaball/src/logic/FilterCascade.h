#pragma once
#include <stdint.h>

namespace gaia {

// High-pass or low-pass with a selectable slope, for the microphone band-pass (HP then
// LP, config TLV 0x16..0x1A, 0x38, 0x39). Host-testable, float only.
//
// SLOPE: 6, 12, 18, 24, 36 or 48 dB/octave = Butterworth of order 1, 2, 3, 4, 6 or 8,
// built as a cascade of second-order sections plus one first-order section when the
// order is odd. The cost is linear in the order: 48 dB/oct is four sections.
//
// Q = THE GAIN EXACTLY AT THE CUTOFF, at every slope: 0.71 (1/sqrt2) is the flat
// Butterworth response (-3 dB at the cutoff), 1.00 is 0 dB, 5.00 is +14 dB. It scales the
// highest-Q section of the Butterworth set; the other sections keep their Butterworth
// Q. At the cutoff a second-order section has gain Q and a first-order one 1/sqrt2, and
// the Butterworth Qs multiply to 1/sqrt2, so the cascade's gain there is exactly Q. With a
// slope of 6 dB/oct there is no second-order section and Q has no effect. For 12 dB/oct
// this is the textbook RBJ biquad (same transfer function as before v9).
//
// STRUCTURE: each section is a TPT (topology-preserving transform, trapezoidal) state
// variable filter (A. Simper, Cytomic, 2013), the bilinear transform with the cutoff
// prewarped. Same response as an RBJ biquad, but much better conditioned in float32 when
// the cutoff is low against the 16 kHz sample rate: a 48 dB/oct high-pass at 1 Hz keeps
// its float32 error ~100 dB below the signal, where a direct-form-II-transposed biquad
// cascade sits at ~45 dB. It also tolerates a change of cutoff or Q without resetting its
// state, so a live edit does not click. Cost per second-order section: 7 multiplies and
// 7 adds per sample; first-order section: 1 multiply, 4 adds.
class FilterCascade {
 public:
  enum class Type : uint8_t { LowPass, HighPass };
  static constexpr uint8_t kMaxSections = 4;  // second-order sections at 48 dB/oct
  static constexpr uint8_t kDefaultSlopeDbOct = 12;

  // True for the slopes on offer: 6, 12, 18, 24, 36, 48 dB/octave.
  static bool validSlope(uint8_t dbPerOct);

  // fs in Hz; hz clamped to [1, 0.45*fs]; q clamped to [0.1, 20]; an invalid slope falls
  // back to 12 dB/oct. The state is cleared only when the order changes.
  void configure(Type type, float hz, float q, uint8_t slopeDbOct, float fs);

  float process(float x) {
    if (onePole_) {
      // First-order TPT section: v = (x - s) * G, lp = v + s, next s = lp + v.
      const float v = (x - s1_) * g1_;
      const float lp = v + s1_;
      s1_ = lp + v;
      x = highPass_ ? x - lp : lp;
    }
    if (highPass_) {
      for (uint8_t i = 0; i < n_; i++) x = sec_[i].highPass(x);
    } else {
      for (uint8_t i = 0; i < n_; i++) x = sec_[i].lowPass(x);
    }
    return x;
  }

  void reset();
  uint8_t order() const { return static_cast<uint8_t>(2 * n_ + (onePole_ ? 1 : 0)); }

 private:
  struct Section {
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
    float ic1 = 0.0f, ic2 = 0.0f;

    float lowPass(float v0) {
      const float v3 = v0 - ic2;
      const float v1 = a1 * ic1 + a2 * v3;
      const float v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = v1 + v1 - ic1;
      ic2 = v2 + v2 - ic2;
      return v2;
    }
    float highPass(float v0) {
      const float v3 = v0 - ic2;
      const float v1 = a1 * ic1 + a2 * v3;
      const float v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = v1 + v1 - ic1;
      ic2 = v2 + v2 - ic2;
      return v0 - k * v1 - v2;
    }
  };

  Section sec_[kMaxSections];
  uint8_t n_ = 0;          // second-order sections in use
  bool onePole_ = false;   // odd order: one first-order section in front
  bool highPass_ = false;
  float g1_ = 0.0f, s1_ = 0.0f;  // first-order section: G = g/(1+g) and its state
};

}  // namespace gaia
