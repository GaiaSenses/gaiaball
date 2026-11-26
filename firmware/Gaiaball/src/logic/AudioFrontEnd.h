#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "AudioFeatures.h"
#include "FilterCascade.h"
#include "Filters.h"

namespace gaia {

// Breath band-pass as configured (TLV 0x16..0x1A, 0x38, 0x39). Q is the gain at the cutoff
// (0.71 = flat Butterworth); slopes in dB/octave (see FilterCascade).
struct BandPassSettings {
  bool enabled = true;
  float hpHz = 10.0f;
  float hpQ = 0.71f;
  uint8_t hpSlopeDbOct = 12;
  float lpHz = 200.0f;
  float lpQ = 0.71f;
  uint8_t lpSlopeDbOct = 12;
};

// Energy and peak of one block, per channel (0 = left, 1 = right), in 24-bit counts.
// cross[kMaxCorrLag + k] = sum of left[n] * right[n - k] over the block, for the lags
// -corrLags()..corrLags() (the others stay 0); see AudioLevelWindow for the coherence.
struct BlockEnergy {
  float sumSq[2] = {0.0f, 0.0f};
  float peak[2] = {0.0f, 0.0f};
  float cross[kCorrLags] = {};
};

// Per-sample microphone front end of AudioService, host-testable:
//
//   I2S word >> 8 (24-bit counts) -> DC blocker (2 Hz) -> (a) mono mix (L+R)/2 to the raw
//   sink (the config-app's spectrum stream: it is taken BEFORE the band-pass on purpose,
//   so the spectrum shows what reaches the filters); (b) optional HP then LP -> sum of
//   squares and max |sample| of the block, and the left x right products over the lags
//   the microphone spacing allows (setCorrLags), for the coherence of the breath detector.
//
// The whole chain is linear and time-invariant, so the spectrum of what feeds the detector
// is exactly the raw spectrum times the band-pass response the config-app draws.
//
// One instance per audio task; not thread-safe (configure() and processBlock() run on the
// audio task).
class AudioFrontEnd {
 public:
  static constexpr float kDcCutoffHz = 2.0f;

  explicit AudioFrontEnd(float fs = 16000.0f) : fs_(fs) {
    dc_[0].configure(kDcCutoffHz, fs);
    dc_[1].configure(kDcCutoffHz, fs);
    configure(BandPassSettings());
  }

  // Applies new band settings. Cutoff and Q changes keep the filter state (no click); a
  // slope change restarts that filter. Turning the band-pass back on restarts both: their
  // state froze when it was turned off, and that old sound would come out as a transient
  // (with a high Q, ringing long enough to cross the breath threshold), and clears the
  // correlation history with them. The DC blockers are never touched.
  void configure(const BandPassSettings &s) {
    const bool reenabled = s.enabled && !bpf_;
    bpf_ = s.enabled;
    for (int ch = 0; ch < 2; ch++) {
      hp_[ch].configure(FilterCascade::Type::HighPass, s.hpHz, s.hpQ, s.hpSlopeDbOct, fs_);
      lp_[ch].configure(FilterCascade::Type::LowPass, s.lpHz, s.lpQ, s.lpSlopeDbOct, fs_);
      if (reenabled) {
        hp_[ch].reset();
        lp_[ch].reset();
      }
    }
    if (reenabled) {  // the correlation history holds the signal of the old setting too
      for (uint8_t i = 0; i < kMaxCorrLag; i++) histL_[i] = histR_[i] = 0.0f;
      hpos_ = 0;
    }
  }

  bool bandPassEnabled() const { return bpf_; }

  // Lags (in samples, each way) of the left/right correlation, clamped to kMaxCorrLag. The
  // sample history is kept whatever the value, so a change applies from the next sample.
  void setCorrLags(uint8_t k) { lags_ = k > kMaxCorrLag ? kMaxCorrLag : k; }
  uint8_t corrLags() const { return lags_; }

  // One interleaved stereo block (L, R, L, R, ...) of left-aligned 32-bit I2S words.
  // rawMono(float) is called once per frame with the DC-free mono mix (pre band-pass).
  template <typename RawSink>
  BlockEnergy processBlock(const int32_t *lr, size_t frames, RawSink &&rawMono) {
    BlockEnergy e;
    for (size_t i = 0; i < frames; i++) {
      // 24-bit integers are exact in float (|x| < 2^24).
      float l = dc_[0].process((float)(lr[2 * i] >> 8));
      float r = dc_[1].process((float)(lr[2 * i + 1] >> 8));
      rawMono(0.5f * (l + r));
      if (bpf_) {
        l = lp_[0].process(hp_[0].process(l));
        r = lp_[1].process(hp_[1].process(r));
      }
      // float, not double: the Xtensa LX6 FPU is single-precision only (double is
      // emulated in software). Over one block the relative error stays near 1e-5.
      e.sumSq[0] += l * l;
      e.sumSq[1] += r * r;
      // Peak at the full 16 kHz, on the same signal as the RMS, so that peak/RMS is a
      // meaningful crest factor.
      const float al = fabsf(l), ar = fabsf(r);
      if (al > e.peak[0]) e.peak[0] = al;
      if (ar > e.peak[1]) e.peak[1] = ar;
      // Left x right over the lags, on the same signal as the energy. hpos_ is the next
      // slot to write, so the sample k frames ago sits k slots behind it. Cost: 2k + 1
      // multiplies per frame (3 for the 15.4 mm board).
      e.cross[kMaxCorrLag] += l * r;
      for (uint8_t k = 1; k <= lags_; k++) {
        const uint8_t j = (uint8_t)(hpos_ >= k ? hpos_ - k : hpos_ + kMaxCorrLag - k);
        e.cross[kMaxCorrLag + k] += l * histR_[j];  // right delayed: the sound reached L first
        e.cross[kMaxCorrLag - k] += histL_[j] * r;  // left delayed
      }
      histL_[hpos_] = l;
      histR_[hpos_] = r;
      hpos_ = (uint8_t)(hpos_ + 1 == kMaxCorrLag ? 0 : hpos_ + 1);
    }
    return e;
  }

  BlockEnergy processBlock(const int32_t *lr, size_t frames) {
    return processBlock(lr, frames, [](float) {});
  }

 private:
  float fs_;
  bool bpf_ = true;
  DcBlocker dc_[2];
  FilterCascade hp_[2], lp_[2];
  uint8_t lags_ = 1;
  float histL_[kMaxCorrLag] = {}, histR_[kMaxCorrLag] = {};
  uint8_t hpos_ = 0;
};

}  // namespace gaia
