#include "AudioService.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>

#include <math.h>
#include <string.h>

#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kTag = "audio";
constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr int kPinWs = 25;
constexpr int kPinSck = 26;
constexpr int kPinSd = 33;
constexpr int kSampleRate = 16000;
// 80 frames = 5 ms per block: the envelope published on the orientation packet can
// never be fresher than one block, and the notify rate goes up to 200 Hz.
constexpr size_t kFrames = 80;
static_assert(kFrames * 1000 / kSampleRate == AudioLevelWindow::kBlockMs,
              "AudioLevelWindow counts its span in blocks of this size");
// 8 buffers = 40 ms of DMA slack before an overrun if the task is held off (the old
// 4 x 256 frames was 64 ms; 40 ms is still 8 blocks of headroom).
constexpr int kDmaBufCount = 8;
// The adaptive detector's EMA coefficients are per update and were tuned against the
// old 16 ms block. Feeding it every 3rd block keeps that cadence at 15 ms, so the
// stored alphas keep their meaning (time constants shift by 6 %, not by 3.2x). The
// absolute detector has no per-update state to keep in tune and runs on every block.
constexpr uint8_t kAdaptiveBlocks = 3;
// A DMA buffer is ready every 5 ms; a finite read timeout keeps the task feeding
// the WDT even if the I2S DMA stalls (a timeout counts as an i2sError).
constexpr TickType_t kReadTimeout = pdMS_TO_TICKS(100);
}  // namespace

bool AudioService::begin(const GaiaConfig &cfg) {
  applyConfig(cfg);
  // Valid from boot, before the first block (and stays this way if I2S fails).
  const float dbfs = thresholdProvider_ ? thresholdProvider_() : fixedThresholdDbfs_.load();
  triggerLevel_.store(cfg.breathMetric != 0 ? dbfsToPeakCounts(dbfs) : dbfsToRmsCounts(dbfs));

  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  c.sample_rate = kSampleRate;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_I2S;
  c.intr_alloc_flags = 0;
  c.dma_buf_count = kDmaBufCount;
  c.dma_buf_len = kFrames;
  c.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = kPinSck;
  pins.ws_io_num = kPinWs;
  pins.data_out_num = -1;
  pins.data_in_num = kPinSd;

  esp_err_t err = i2s_driver_install(kPort, &c, 0, nullptr);
  if (err != ESP_OK) {
    GB_LOGE(kTag, "i2s_driver_install failed: %d", (int)err);
    return false;
  }
  err = i2s_set_pin(kPort, &pins);
  if (err != ESP_OK) {
    GB_LOGE(kTag, "i2s_set_pin failed: %d", (int)err);
    i2s_driver_uninstall(kPort);
    return false;
  }
  i2s_zero_dma_buffer(kPort);
  ok_ = true;
  GB_LOGI(kTag, "I2S stereo ready (16 kHz, %u-frame blocks)", (unsigned)kFrames);
  return true;
}

void AudioService::applyConfig(const GaiaConfig &cfg) {
  fixedThresholdDbfs_.store(-cfg.breathThresholdNegDbX100 / 100.0f);
  fastX1000_.store(cfg.audioFastAlphaX1000);
  slowX1000_.store(cfg.audioSlowAlphaX1000);
  debounceMs_.store(cfg.debounceMs);
  breathMetric_.store(cfg.breathMetric);
  breathDetector_.store(cfg.breathDetector);
  breathRearmDb_.store(cfg.breathRearmDb);
  enabled_.store(cfg.audioEnabled != 0);
  bpfEnabled_.store(cfg.audioBpfEnabled != 0);
  rawStreamEnabled_.store(cfg.audioRawStream != 0);
  hpHz_.store(cfg.audioHpHz);
  hpQX100_.store(cfg.audioHpQX100);
  lpHz_.store(cfg.audioLpHz);
  lpQX100_.store(cfg.audioLpQX100);
  hpSlope_.store(cfg.audioHpSlopeDbOct);
  lpSlope_.store(cfg.audioLpSlopeDbOct);
  windowMs_.store(cfg.audioWindowMs);
  cfgDirty_.store(true);
}

AudioLevels AudioService::levels() const {
  AudioLevels l;
  l.rms[0] = rmsL_.load();
  l.rms[1] = rmsR_.load();
  l.peak[0] = peakL_.load();
  l.peak[1] = peakR_.load();
  return l;
}

bool AudioService::takeAudioChunk(int16_t out[AudioDecimator::kChunkSamples]) {
  if (!chunkReady_.load()) return false;
  portENTER_CRITICAL(&chunkMux_);
  memcpy(out, chunkBuf_, sizeof(chunkBuf_));
  chunkReady_.store(false);
  portEXIT_CRITICAL(&chunkMux_);
  return true;
}

void AudioService::startTask() {
  if (!ok_ || task_ != nullptr) return;
  if (xTaskCreatePinnedToCore(&AudioService::taskEntry, "audio", 4096, this, 2, &task_, 1) !=
      pdPASS) {
    task_ = nullptr;
    GB_LOGE(kTag, "task creation failed");
  }
}

void AudioService::taskEntry(void *arg) {
  static_cast<AudioService *>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void AudioService::taskLoop() {
  static int32_t buf[kFrames * 2];  // 640 B, kept off the 4 KB task stack
  // Task WDT: registered and fed once per cycle (see the watchdog policy in
  // Diagnostics.h); the finite i2s_read timeout is what makes the feed reachable.
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    size_t bytes = 0;
    const esp_err_t err = i2s_read(kPort, buf, sizeof(buf), &bytes, kReadTimeout);
    if (err != ESP_OK || bytes == 0) {
      if (i2sErrors_.load() < UINT32_MAX) i2sErrors_.fetch_add(1);
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (cfgDirty_.exchange(false)) {
      BreathParams bp;
      bp.mode = breathDetector_.load() != 0 ? BreathMode::Adaptive : BreathMode::Absolute;
      bp.rearmDb = breathRearmDb_.load();
      bp.debounceMs = debounceMs_.load();
      bp.fastAlpha = fastX1000_.load() / 1000.0f;
      bp.slowAlpha = slowX1000_.load() / 1000.0f;
      const bool wantPeak = breathMetric_.load() != 0;
      // A new metric changes the units the detector works in: the adaptive EMAs (and
      // a pending disarm) would mix RMS and peak counts, so start the detector over.
      // A new mode is reset by configure() itself; anything else applies live.
      if (wantPeak != metricPeak_) detector_ = BreathDetector();
      if (bp.mode != breathParams_.mode) detectorPhase_ = 0;
      metricPeak_ = wantPeak;
      breathParams_ = bp;
      breathDirty_ = true;  // thresholdCounts is filled in once the levels are in
      window_.setBlocks(AudioLevelWindow::blocksForMs(windowMs_.load()));
      // Raw stream toggled: starting fresh keeps the first chunk after a pause from
      // mixing new samples with ones captured before it; stopping drops the chunk the
      // App task has not picked up yet.
      const bool wantRaw = rawStreamEnabled_.load();
      if (wantRaw != rawStreamActive_) {
        decim_.reset();
        chunkReady_.store(false);
        rawStreamActive_ = wantRaw;
      }
      BandPassSettings band;
      band.enabled = bpfEnabled_.load();
      band.hpHz = (float)hpHz_.load();
      band.hpQ = hpQX100_.load() / 100.0f;
      band.hpSlopeDbOct = hpSlope_.load();
      band.lpHz = (float)lpHz_.load();
      band.lpQ = lpQX100_.load() / 100.0f;
      band.lpSlopeDbOct = lpSlope_.load();
      frontEnd_.configure(band);
    }

    // One pass over the samples: DC blocker, raw-stream feed (before the band-pass),
    // optional band-pass, energy and peak (AudioFrontEnd).
    const size_t frames = (bytes / sizeof(int32_t)) / 2;
    if (frames == 0) continue;
    const BlockEnergy e = frontEnd_.processBlock(buf, frames, [this](float mono) {
      // Only while the raw stream is on (GaiaConfig::audioRawStream).
      if (rawStreamActive_ && decim_.feed(mono)) {
        portENTER_CRITICAL(&chunkMux_);
        memcpy(chunkBuf_, decim_.chunk(), sizeof(chunkBuf_));
        chunkReady_.store(true);
        portEXIT_CRITICAL(&chunkMux_);
      }
    });
    window_.push(e.sumSq, e.peak, (uint16_t)frames);
    const AudioLevels lv = window_.levels();
    rmsL_.store(lv.rms[0]);
    rmsR_.store(lv.rms[1]);
    peakL_.store(lv.peak[0]);
    peakR_.store(lv.peak[1]);

    // Switching the detector off or on starts it over: a baseline or a pending disarm
    // from before the pause says nothing about the room now.
    const bool enabled = enabled_.load();
    if (enabled != detecting_) {
      detector_ = BreathDetector();
      breathDirty_ = true;
      detectorPhase_ = 0;
      detecting_ = enabled;
      breathLatch_.store(false);  // enable/disable restarts the detector: no stale bit 4
    }

    // Threshold in dBFS (potentiometer or fixed), converted to counts of the active
    // metric. Reconfigured here, on the detector's own task, only when it changed.
    const float dbfs = thresholdProvider_ ? thresholdProvider_() : fixedThresholdDbfs_.load();
    if (breathDirty_ || dbfs != activeDbfs_) {
      activeDbfs_ = dbfs;
      breathParams_.thresholdCounts = metricPeak_ ? dbfsToPeakCounts(dbfs) : dbfsToRmsCounts(dbfs);
      detector_.configure(breathParams_);
      breathDirty_ = false;
    }

    if (!enabled) {  // audioEnabled = 0: nothing fires and the state stays 0
      breathRecognized_.store(false);
      breathLatch_.store(false);  // a packet must not carry bit 4 after audio is disabled
      triggerLevel_.store(detector_.triggerLevel());
      continue;
    }
    // Absolute: every block. Adaptive: its own, slower cadence (see kAdaptiveBlocks).
    bool evaluate = true;
    if (breathParams_.mode == BreathMode::Adaptive) {
      if (++detectorPhase_ < kAdaptiveBlocks) {
        evaluate = false;
      } else {
        detectorPhase_ = 0;
      }
    }
    if (evaluate) {
      const uint32_t now = millis();
      if (detector_.update(metricPeak_ ? lv.peak : lv.rms, now) && onPeak_) onPeak_(now);
    }
    const bool recognized = detector_.recognized();
    breathRecognized_.store(recognized);
    if (recognized) breathLatch_.store(true);
    triggerLevel_.store(detector_.triggerLevel());
  }
}

}  // namespace gaia
