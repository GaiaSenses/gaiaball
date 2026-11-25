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
// Index 0 = left, 1 = right. coherence: see AudioLevelWindow::levels().
struct AudioLevels {
  float rms[2] = {0.0f, 0.0f};
  float peak[2] = {0.0f, 0.0f};
  float coherence = 0.0f;
};

// Left/right correlation of the breath discriminator (v10).
//
// WHY: sound from a distance (speech, music, a knock on the table) reaches both
// microphones as the same pressure wave, a few tens of microseconds apart, so the two
// signals are almost identical; a breath on the ball is turbulence at each microphone
// port, different at each one. The normalized correlation of the two band-passed
// signals separates the two cases whatever the level.
//
// The arrival delay of a far sound is at most spacing / speed of sound (15.4 mm on the
// board: 45 us, under one sample at 16 kHz), so the correlation is searched over the lags
// that spacing allows and the best one is kept. Lags are in samples; kMaxCorrLag covers
// 200 mm at 16 kHz.
constexpr uint8_t kMaxCorrLag = 10;
constexpr uint8_t kCorrLags = 2 * kMaxCorrLag + 1;  // index kMaxCorrLag + k holds lag k
constexpr float kSpeedOfSoundMps = 343.0f;          // air at ~20 degC

// Lags to search for a microphone spacing in 0.1 mm: ceil(spacing / c * fs), 0..kMaxCorrLag.
uint8_t corrLagsForSpacing(uint16_t spacingMmX10, float fs);

// AES17 dBFS x100: 0 dBFS is the RMS of a full-scale sine (FS/sqrt(2)), so a
// full-scale sine reads 0.00 and not -3.01. Saturates to the wire range; zero,
// negative and non-finite inputs encode as the floor.
int16_t rmsToDbfsX100(float rms);

// Peak dBFS x100: 0 dBFS is a full-scale sample, so this scale is 3.01 dB below the
// RMS one for the same sine. Same saturation rules.
int16_t peakToDbfsX100(float peak);

// Half-dB wire encoding of the movement packet: clamp(lround((dbX100 + 12000) / 50), 0, 255).
// The floor (-120 dBFS) is 0; dBFS = value / 2 - 120, so 255 is +7.5 dBFS.
uint8_t dbX100ToHalfDb(int16_t dbX100);

// Inverse conversions, linear counts (same 24-bit sample units as AudioLevels).
// RMS scale is AES17 (0 dBFS = full-scale sine = 2^23/sqrt(2)); peak scale 0 dBFS = 2^23.
float dbfsToRmsCounts(float dbfs);
float dbfsToPeakCounts(float dbfs);
// 10^(-db/20): amplitude ratio of a drop of `db` dB (3 dB -> 0.7079).
float dbToAmplitudeRatio(float db);

// Sliding energy window over the last blocks() I2S blocks (configurable, TLV 0x2F).
//
// WHY pool blocks instead of reporting the bare block: at 200 Hz the I2S block is 5 ms,
// shorter than one period of the low end of the breath band (10 ms at 100 Hz), so a
// single block measures instantaneous amplitude rather than energy and the value would
// wobble with the waveform. Pooling N 5 ms blocks gives an N x 5 ms energy window while
// still publishing a fresh value every block. The peak is the maximum over the same
// span, so peak/RMS stays a meaningful crest factor.
//
// The span is GaiaConfig::audioWindowMs (default 50 ms = 10 blocks, was fixed at 20 ms):
// longer = steadier level, slower to rise and to fall.
//
// Not thread-safe: push(), levels() and setBlocks() belong to the audio task.
class AudioLevelWindow {
 public:
  static constexpr uint16_t kBlockMs = 5;      // 80 frames at 16 kHz (AudioService)
  static constexpr uint8_t kMaxBlocks = 40;    // 200 ms
  static constexpr uint8_t kDefaultBlocks = 10;  // 50 ms

  // Window length in ms -> blocks, rounded to the nearest block, clamped to 1..kMaxBlocks.
  static uint8_t blocksForMs(uint16_t ms);

  // sumSq = sum of squared samples of this block per channel, peak = max |sample| per
  // channel, frames = samples per channel in the block. frames == 0 is ignored.
  // cross (optional, kCorrLags values): sum of left[n] * right[n - k] over the block for
  // each lag k, at index kMaxCorrLag + k (AudioFrontEnd fills the lags it searches).
  void push(const float sumSq[2], const float peak[2], uint16_t frames,
            const float *cross = nullptr);
  // Pooled RMS, windowed peak and coherence. All-zero while the window holds no samples.
  // coherence = max over the lags of |pooled cross sum| / sqrt(pooled energy L * R), in
  // 0..1: near 1 for one sound reaching both microphones, near 0 for independent noise
  // at each one (a breath on the ports). 0 when either channel is silent.
  AudioLevels levels() const;
  void reset();
  // Changes the span (clamped to 1..kMaxBlocks). A different span starts the window
  // over: it refills within the new span, levels() averaging only what it holds.
  void setBlocks(uint8_t n);
  uint8_t blocks() const { return blocks_; }

 private:
  struct Entry {
    float sumSq[2] = {0.0f, 0.0f};
    float peak[2] = {0.0f, 0.0f};
    float cross[kCorrLags] = {};
    uint16_t frames = 0;
  };
  Entry ring_[kMaxBlocks] = {};
  uint8_t blocks_ = kDefaultBlocks;
  uint8_t next_ = 0;
};

}  // namespace gaia
