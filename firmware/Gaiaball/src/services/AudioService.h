#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>
#include <functional>

#include "../logic/AudioDecimator.h"
#include "../logic/Biquad.h"
#include "../logic/GaiaConfig.h"
#include "../logic/PeakDetector.h"

namespace gaia {

// Dual INMP441 (stereo I2S) sound-peak detector + raw-audio streamer.
// Pipeline per 256-frame block (16 kHz, 32-bit I2S, samples >> 8 = 24-bit counts):
//   i2s_read -> per-block DC removal ->
//     (a) raw mono mix -> AudioDecimator -> 2 kHz int16 chunks for the config-app
//         spectrum analyzer (the FFT runs in the frontend);
//     (b) optional breath band-pass (HP then LP biquads, config TLV 0x16..0x1A)
//         -> stereo RMS -> PeakDetector fed with max(L,R) -> peak callback.
// With the band-pass disabled the RMS math is identical to the reference
// computeStereoRms() (logic/AudioBlock.h), which stays as the host-tested spec.
//
// Threading:
//  - onPeak() / setThresholdProvider() must be set BEFORE startTask().
//  - The peak callback AND the threshold provider run on the audio task (core 1,
//    prio 2): keep both short and non-blocking.
//  - applyConfig() is safe from any task; the audio task picks it up next block.
//  - takeAudioChunk() is safe from any task (short critical section).
//  - If I2S init fails, ok() is false and the task is never started.
class AudioService {
 public:
  bool begin(const GaiaConfig &cfg);
  void onPeak(std::function<void(uint32_t nowMs)> cb) { onPeak_ = std::move(cb); }
  void setThresholdProvider(std::function<float()> p) { thresholdProvider_ = std::move(p); }
  void startTask();
  void applyConfig(const GaiaConfig &cfg);

  bool ok() const { return ok_; }
  float lastRmsLeft() const { return rmsL_.load(); }
  float lastRmsRight() const { return rmsR_.load(); }
  uint32_t i2sErrorCount() const { return i2sErrors_.load(); }
  bool bandPassEnabled() const { return bpfEnabled_.load(); }

  // Copies the latest 2 kHz chunk (AudioDecimator::kChunkSamples int16) and clears
  // the ready flag. Returns false when no fresh chunk is available.
  bool takeAudioChunk(int16_t out[AudioDecimator::kChunkSamples]);

 private:
  static void taskEntry(void *arg);
  void taskLoop();

  bool ok_ = false;
  TaskHandle_t task_ = nullptr;
  std::function<void(uint32_t)> onPeak_;
  std::function<float()> thresholdProvider_;
  float fixedThreshold_ = 100000.0f;

  // Config handoff (any task -> audio task).
  std::atomic<uint16_t> fastX1000_{250};
  std::atomic<uint16_t> slowX1000_{995};
  std::atomic<uint32_t> debounceMs_{300};
  std::atomic<bool> enabled_{true};
  std::atomic<bool> bpfEnabled_{true};
  std::atomic<uint16_t> hpHz_{10}, hpQX100_{500}, lpHz_{200}, lpQX100_{500};
  std::atomic<bool> cfgDirty_{true};

  std::atomic<float> rmsL_{0.0f};
  std::atomic<float> rmsR_{0.0f};
  std::atomic<uint32_t> i2sErrors_{0};

  // Chunk handoff (audio task -> App task).
  portMUX_TYPE chunkMux_ = portMUX_INITIALIZER_UNLOCKED;
  int16_t chunkBuf_[AudioDecimator::kChunkSamples] = {0};
  std::atomic<bool> chunkReady_{false};

  // Audio-task-only state.
  PeakDetector detector_;
  AudioDecimator decim_;
  Biquad hpL_, lpL_, hpR_, lpR_;
  bool bpfActive_ = true;
};

}  // namespace gaia
