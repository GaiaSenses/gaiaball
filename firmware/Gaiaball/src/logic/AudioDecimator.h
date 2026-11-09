#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gaia {

// Decimates the 16 kHz DC-removed mono mix to a 2 kHz int16 stream and packs it
// into fixed-size chunks for BLE streaming. The FFT/spectrum analysis happens in
// the config-app (frontend); the firmware only ships band-limited samples.
//
// - Average-by-8 decimation (crude anti-alias, adequate for a tuning visualizer:
//   the band of interest, breath noise, lives well below the 1 kHz Nyquist).
// - Input samples are 24-bit counts (int32 >> 8); output is int16 (input / 256,
//   saturated), i.e. the top 16 bits of the original 24-bit audio.
class AudioDecimator {
 public:
  static constexpr int kDecim = 8;
  static constexpr size_t kChunkSamples = 96;        // 48 ms per chunk at 2 kHz
  static constexpr uint16_t kOutputRateHz = 2000;    // 16000 / kDecim

  // Feeds one 16 kHz sample; returns true when chunk() holds kChunkSamples fresh
  // samples (valid until the next feed() that completes a chunk).
  bool feed(float sample);
  const int16_t *chunk() const { return out_; }

  // Drops the partial chunk and the partial average. Called when the raw stream is
  // switched back on, so the first chunk after the pause cannot mix fresh samples
  // with ones captured before it.
  void reset();

 private:
  float acc_ = 0.0f;
  int accN_ = 0;
  size_t n_ = 0;
  int16_t out_[kChunkSamples] = {0};
};

}  // namespace gaia
