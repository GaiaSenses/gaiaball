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

void AudioLevelWindow::push(const float sumSq[2], const float peak[2], uint16_t frames) {
  if (frames == 0) return;
  Entry &e = ring_[next_];
  for (int c = 0; c < 2; c++) {
    e.sumSq[c] = sumSq[c];
    e.peak[c] = peak[c];
  }
  e.frames = frames;
  next_ = (uint8_t)((next_ + 1) % kBlocks);
}

AudioLevels AudioLevelWindow::levels() const {
  AudioLevels out;
  float sum[2] = {0.0f, 0.0f};
  uint32_t frames = 0;
  for (uint8_t i = 0; i < kBlocks; i++) {
    const Entry &e = ring_[i];
    if (e.frames == 0) continue;
    frames += e.frames;
    for (int c = 0; c < 2; c++) {
      sum[c] += e.sumSq[c];
      if (e.peak[c] > out.peak[c]) out.peak[c] = e.peak[c];
    }
  }
  if (frames == 0) return out;
  for (int c = 0; c < 2; c++) out.rms[c] = sqrtf(sum[c] / (float)frames);
  return out;
}

void AudioLevelWindow::reset() {
  for (uint8_t i = 0; i < kBlocks; i++) ring_[i] = Entry{};
  next_ = 0;
}

}  // namespace gaia
