#include "AudioFeatures.h"

#include <math.h>

namespace gaia {

namespace {
// Shared tail of both dBFS encodings: 20*log10(ratio), scaled, rounded, saturated.
int16_t encodeDbX100(float ratio) {
  if (!isfinite(ratio) || ratio <= 0.0f) return kAudioDbFloorX100;
  const float db = 2000.0f * log10f(ratio);  // 100 * 20 * log10()
  if (db <= (float)kAudioDbFloorX100) return kAudioDbFloorX100;
  if (db >= (float)kAudioDbCeilX100) return kAudioDbCeilX100;
  return (int16_t)lroundf(db);
}
}  // namespace

// AES17: the reference is the RMS of a full-scale sine, hence the sqrt(2).
int16_t rmsToDbfsX100(float rms) { return encodeDbX100(rms * 1.41421356f / kAudioFullScale); }

int16_t peakToDbfsX100(float peak) { return encodeDbX100(peak / kAudioFullScale); }

uint8_t dbX100ToHalfDb(int16_t dbX100) {
  long v = lround((dbX100 - kAudioDbFloorX100) / 50.0);
  if (v < 0) return 0;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}

float dbfsToPeakCounts(float dbfs) { return kAudioFullScale * powf(10.0f, dbfs / 20.0f); }

float dbfsToRmsCounts(float dbfs) { return dbfsToPeakCounts(dbfs) / 1.41421356f; }

float dbToAmplitudeRatio(float db) { return powf(10.0f, -db / 20.0f); }

uint8_t corrLagsForSpacing(uint16_t spacingMmX10, float fs) {
  // Delay in samples of a sound arriving along the line through both microphones.
  const float lag = (spacingMmX10 / 10000.0f) / kSpeedOfSoundMps * fs;
  const float up = ceilf(lag - 1e-4f);  // a whole number of samples stays that number
  if (!(up > 0.0f)) return 0;
  if (up >= (float)kMaxCorrLag) return kMaxCorrLag;
  return (uint8_t)up;
}

uint8_t AudioLevelWindow::blocksForMs(uint16_t ms) {
  uint32_t n = ((uint32_t)ms + kBlockMs / 2) / kBlockMs;
  if (n < 1) n = 1;
  if (n > kMaxBlocks) n = kMaxBlocks;
  return (uint8_t)n;
}

void AudioLevelWindow::setBlocks(uint8_t n) {
  if (n < 1) n = 1;
  if (n > kMaxBlocks) n = kMaxBlocks;
  if (n == blocks_) return;
  blocks_ = n;
  reset();
}

void AudioLevelWindow::push(const float sumSq[2], const float peak[2], uint16_t frames,
                            const float *cross) {
  if (frames == 0) return;
  Entry &e = ring_[next_];
  for (int c = 0; c < 2; c++) {
    e.sumSq[c] = sumSq[c];
    e.peak[c] = peak[c];
  }
  for (uint8_t k = 0; k < kCorrLags; k++) e.cross[k] = cross ? cross[k] : 0.0f;
  e.frames = frames;
  next_ = (uint8_t)((next_ + 1) % blocks_);
}

AudioLevels AudioLevelWindow::levels() const {
  AudioLevels out;
  float sum[2] = {0.0f, 0.0f};
  float cross[kCorrLags] = {};
  uint32_t frames = 0;
  for (uint8_t i = 0; i < blocks_; i++) {
    const Entry &e = ring_[i];
    if (e.frames == 0) continue;
    frames += e.frames;
    for (int c = 0; c < 2; c++) {
      sum[c] += e.sumSq[c];
      if (e.peak[c] > out.peak[c]) out.peak[c] = e.peak[c];
    }
    for (uint8_t k = 0; k < kCorrLags; k++) cross[k] += e.cross[k];
  }
  if (frames == 0) return out;
  for (int c = 0; c < 2; c++) out.rms[c] = sqrtf(sum[c] / (float)frames);
  // The magnitude, not the sign: a microphone wired or mounted with the opposite polarity
  // still hears the same sound, and that must still read as coherent.
  float best = 0.0f;
  for (uint8_t k = 0; k < kCorrLags; k++) {
    const float a = fabsf(cross[k]);
    if (a > best) best = a;
  }
  const float norm = sqrtf(sum[0]) * sqrtf(sum[1]);  // two roots: sum[0]*sum[1] can overflow
  if (norm > 0.0f && isfinite(norm)) {
    const float c = best / norm;
    out.coherence = c > 1.0f ? 1.0f : c;
  }
  return out;
}

void AudioLevelWindow::reset() {
  for (uint8_t i = 0; i < kMaxBlocks; i++) ring_[i] = Entry{};
  next_ = 0;
}

}  // namespace gaia
