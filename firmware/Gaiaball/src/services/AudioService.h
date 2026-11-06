#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>
#include <functional>

#include "../logic/GaiaConfig.h"
#include "../logic/PeakDetector.h"

namespace gaia {

// Dual INMP441 (stereo I2S) sound-peak detector.
// Pipeline per 256-frame block: i2s_read (blocks in the audio task, by design) ->
// computeStereoRms (DC-removed) -> PeakDetector fed with max(L,R) -> peak callback.
//
// Threading:
//  - onPeak() / setThresholdProvider() must be set BEFORE startTask().
//  - The peak callback AND the threshold provider run on the audio task (core 1,
//    prio 2): keep both short and non-blocking (e.g. post to a queue / set a flag;
//    the provider should just return a cached value such as PotService::threshold()).
//  - applyConfig() is safe from any task; the audio task picks it up on its next block.
//  - If I2S init fails, ok() is false and the task is never started (startTask no-ops).
class AudioService {
 public:
  // Installs the I2S driver (legacy driver/i2s.h, I2S_NUM_0, 16 kHz, 32-bit stereo).
  // Returns ok().
  bool begin(const GaiaConfig &cfg);
  void onPeak(std::function<void(uint32_t nowMs)> cb) { onPeak_ = std::move(cb); }
  void setThresholdProvider(std::function<float()> p) { thresholdProvider_ = std::move(p); }
  void startTask();
  void applyConfig(const GaiaConfig &cfg);

  bool ok() const { return ok_; }
  float lastRmsLeft() const { return rmsL_.load(); }
  float lastRmsRight() const { return rmsR_.load(); }
  uint32_t i2sErrorCount() const { return i2sErrors_.load(); }

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
  std::atomic<bool> cfgDirty_{true};

  std::atomic<float> rmsL_{0.0f};
  std::atomic<float> rmsR_{0.0f};
  std::atomic<uint32_t> i2sErrors_{0};

  PeakDetector detector_;  // audio-task-only after startTask()
};

}  // namespace gaia
