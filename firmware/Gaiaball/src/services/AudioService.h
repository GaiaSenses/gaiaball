#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>
#include <functional>

#include "../logic/AudioDecimator.h"
#include "../logic/AudioFeatures.h"
#include "../logic/Biquad.h"
#include "../logic/BreathDetector.h"
#include "../logic/GaiaConfig.h"

namespace gaia {

// Dual INMP441 (stereo I2S) breath detector, envelope publisher and raw-audio
// streamer.
//
// Pipeline per 80-frame block (5 ms at 16 kHz, 32-bit I2S, samples >> 8 = 24-bit
// counts):
//   i2s_read -> per-block DC removal ->
//     (a) raw mono mix -> AudioDecimator -> 2 kHz int16 chunks for the config-app
//         spectrum analyzer (the FFT runs in the frontend). OPT-IN: only while
//         GaiaConfig::audioRawStream is set, because this one stream used to be 53 %
//         of all the BLE air time on the link;
//     (b) optional breath band-pass (HP then LP biquads, config TLV 0x16..0x1A)
//         -> per-channel sum of squares and max |sample| -> AudioLevelWindow (audioWindowMs,
//            TLV 0x2F, default 50 ms, pooled energy, fresh every block) ->
//              - levels(), published on every ImuPacket (RMS + peak per channel) and
//                on the 5 Hz SensorsPacket;
//              - BreathDetector fed with both channels of the configured metric
//                (TLV 0x2C: RMS or peak) -> peak callback on the firing edge, and
//                the breath bit (recognized + latch) for the ImuPacket. The absolute
//                detector runs on every block; the adaptive one every 3rd block.
// With the band-pass disabled the RMS math is identical to the reference
// computeStereoRms() (logic/AudioBlock.h), which stays as the host-tested spec.
//
// WHY the block is 5 ms and not 16 ms: the published envelope can never be fresher
// than one block, and the orientation notify rate goes up to 200 Hz. The per-sample
// cost does not change (the work is per sample), only the number of task wake-ups
// (62.5/s -> 200/s). The peak is tracked at the full 16 kHz, which is the one thing
// the host could not do for itself: the decimated stream it used to measure is
// averaged by 8, so it hides exactly the transients a peak value reports.
//
// Threshold: the provider returns dBFS (the potentiometer, or the fixed TLV 0x2B
// value). The audio task converts it to counts of the active metric
// (dbfsToRmsCounts / dbfsToPeakCounts) and reconfigures the detector whenever the
// value or the config changes; the detector itself only ever runs on this task.
//
// Threading:
//  - onPeak() / setThresholdProvider() must be set BEFORE startTask().
//  - The peak callback AND the threshold provider run on the audio task (core 1,
//    prio 2): keep both short and non-blocking.
//  - applyConfig() is safe from any task; the audio task picks it up next block.
//  - takeAudioChunk() is safe from any task (short critical section).
//  - breathRecognized(), takeBreathLatch() and triggerLevelCounts() are atomics the
//    audio task writes on every block: safe from any task.
//  - If I2S init fails, ok() is false and the task is never started.
class AudioService {
 public:
  bool begin(const GaiaConfig &cfg);
  void onPeak(std::function<void(uint32_t nowMs)> cb) { onPeak_ = std::move(cb); }
  // The provider returns the breath threshold in dBFS (scale of the active metric).
  void setThresholdProvider(std::function<float()> p) { thresholdProvider_ = std::move(p); }
  void startTask();
  void applyConfig(const GaiaConfig &cfg);

  bool ok() const { return ok_; }
  // Latest pooled envelope (audioWindowMs window, default 50 ms), in 24-bit sample units. Safe from any task.
  AudioLevels levels() const;
  float lastRmsLeft() const { return rmsL_.load(); }
  float lastRmsRight() const { return rmsR_.load(); }
  uint32_t i2sErrorCount() const { return i2sErrors_.load(); }
  bool bandPassEnabled() const { return bpfEnabled_.load(); }

  // Breath bit of the ImuPacket. recognized: the detector is between a fire and its
  // re-arm. The latch is set on every block with recognized() true and cleared by
  // takeBreathLatch() (the IMU packet sender), so a recognition shorter than one
  // notify interval still reaches the next packet.
  bool breathRecognized() const { return breathRecognized_.load(); }
  bool takeBreathLatch() { return breathLatch_.exchange(false); }
  // Effective trigger level in counts of the active metric (absolute: the line;
  // adaptive: slow baseline + threshold). Published on the SensorsPacket.
  float triggerLevelCounts() const { return triggerLevel_.load(); }

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

  // Config handoff (any task -> audio task).
  std::atomic<float> fixedThresholdDbfs_{-35.5f};  // used only without a provider
  std::atomic<uint16_t> fastX1000_{250};
  std::atomic<uint16_t> slowX1000_{995};
  std::atomic<uint32_t> debounceMs_{300};
  std::atomic<uint8_t> breathMetric_{0};    // 0 RMS, 1 peak
  std::atomic<uint8_t> breathDetector_{0};  // 0 absolute, 1 adaptive
  std::atomic<uint8_t> breathRearmDb_{3};
  std::atomic<bool> enabled_{true};
  std::atomic<bool> bpfEnabled_{true};
  std::atomic<bool> rawStreamEnabled_{false};
  std::atomic<uint16_t> hpHz_{10}, hpQX100_{500}, lpHz_{200}, lpQX100_{500};
  std::atomic<uint16_t> windowMs_{50};
  std::atomic<bool> cfgDirty_{true};

  std::atomic<float> rmsL_{0.0f};
  std::atomic<float> rmsR_{0.0f};
  std::atomic<float> peakL_{0.0f};
  std::atomic<float> peakR_{0.0f};
  std::atomic<uint32_t> i2sErrors_{0};

  // Breath handoff (audio task -> App task).
  std::atomic<bool> breathRecognized_{false};
  std::atomic<bool> breathLatch_{false};
  std::atomic<float> triggerLevel_{0.0f};

  // Chunk handoff (audio task -> App task).
  portMUX_TYPE chunkMux_ = portMUX_INITIALIZER_UNLOCKED;
  int16_t chunkBuf_[AudioDecimator::kChunkSamples] = {0};
  std::atomic<bool> chunkReady_{false};

  // Audio-task-only state.
  BreathDetector detector_;
  BreathParams breathParams_;  // last applied; thresholdCounts follows activeDbfs_
  bool metricPeak_ = false;
  bool breathDirty_ = true;  // breathParams_ changed and is not in detector_ yet
  bool detecting_ = true;    // enabled_ as last seen by the audio task
  float activeDbfs_ = 0.0f;
  AudioDecimator decim_;
  AudioLevelWindow window_;
  Biquad hpL_, lpL_, hpR_, lpR_;
  bool bpfActive_ = true;
  bool rawStreamActive_ = false;
  uint8_t detectorPhase_ = 0;
};

}  // namespace gaia
