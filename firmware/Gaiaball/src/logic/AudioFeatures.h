#pragma once
#include <stdint.h>

namespace gaia {

// Microphone envelope features and their wire encoding.
//
// WHY these live in the firmware instead of the browser: the decimated 2 kHz stream
// that used to carry them is low-pass filtered by an average-of-8, which hides exactly
// the transients a peak value is supposed to report, and it only reaches the host at
// ~21 packets/s. Computing RMS and peak here, at the full 16 kHz rate, gives an
// envelope that is both more faithful and available at the orientation notify rate.

// Full-scale amplitude of one INMP441 sample as handled by this firmware: the I2S word
// is 32-bit left-aligned 24-bit data and the pipeline works on (word >> 8).
constexpr float kAudioFullScale = 8388608.0f;  // 2^23

// Wire range of a dBFS x100 field. The floor matches the config-app's display floor;
// the ceiling leaves room for a signal whose crest factor is below 3 dB (a square wave
// reads above 0 dBFS on the AES17 RMS scale) without overflowing int16.
constexpr int16_t kAudioDbFloorX100 = -12000;  // -120.00 dBFS
constexpr int16_t kAudioDbCeilX100 = 6000;     //  +60.00 dBFS

// One reporting window of microphone levels, in 24-bit sample units (linear).
// Index 0 = left, 1 = right.
struct AudioLevels {
  float rms[2] = {0.0f, 0.0f};
  float peak[2] = {0.0f, 0.0f};
};

// AES17 dBFS x100: 0 dBFS is the RMS of a full-scale sine (FS/sqrt(2)), so a
// full-scale sine reads 0.00 and not -3.01. Saturates to the wire range; zero,
// negative and non-finite inputs encode as the floor.
int16_t rmsToDbfsX100(float rms);

// Peak dBFS x100: 0 dBFS is a full-scale sample, so this scale is 3.01 dB below the
// RMS one for the same sine. Same saturation rules.
int16_t peakToDbfsX100(float peak);

// Sliding energy window over the last kBlocks I2S blocks.
//
// WHY pool blocks instead of reporting the bare block: at 200 Hz the I2S block is 5 ms,
// shorter than one period of the low end of the breath band (10 ms at 100 Hz), so a
// single block measures instantaneous amplitude rather than energy and the value would
// wobble with the waveform. Pooling four 5 ms blocks keeps a 20 ms energy window while
// still publishing a fresh value every block (75 % overlap). The peak is the maximum
// over the same span, which is what "peak of the last 20 ms" should mean.
//
// Not thread-safe: push() and levels() belong to the audio task.
class AudioLevelWindow {
 public:
  static constexpr uint8_t kBlocks = 4;

  // sumSq = sum of squared samples of this block per channel, peak = max |sample| per
  // channel, frames = samples per channel in the block. frames == 0 is ignored.
  void push(const float sumSq[2], const float peak[2], uint16_t frames);
  // Pooled RMS and windowed peak. All-zero while the window holds no samples.
  AudioLevels levels() const;
  void reset();

 private:
  struct Entry {
    float sumSq[2] = {0.0f, 0.0f};
    float peak[2] = {0.0f, 0.0f};
    uint16_t frames = 0;
  };
  Entry ring_[kBlocks] = {};
  uint8_t next_ = 0;
};

}  // namespace gaia
