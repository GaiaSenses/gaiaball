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
constexpr size_t kFrames = 256;
// A DMA buffer is ready every 16 ms; a finite read timeout keeps the task feeding
// the WDT even if the I2S DMA stalls (a timeout counts as an i2sError).
constexpr TickType_t kReadTimeout = pdMS_TO_TICKS(100);
}  // namespace

bool AudioService::begin(const GaiaConfig &cfg) {
  applyConfig(cfg);
  fixedThreshold_ = (float)cfg.thresholdFixed;

  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  c.sample_rate = kSampleRate;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  c.communication_format = I2S_COMM_FORMAT_I2S;
  c.intr_alloc_flags = 0;
  c.dma_buf_count = 4;
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
  GB_LOGI(kTag, "I2S stereo ready (16 kHz)");
  return true;
}

void AudioService::applyConfig(const GaiaConfig &cfg) {
  fastX1000_.store(cfg.audioFastAlphaX1000);
  slowX1000_.store(cfg.audioSlowAlphaX1000);
  debounceMs_.store(cfg.debounceMs);
  enabled_.store(cfg.audioEnabled != 0);
  bpfEnabled_.store(cfg.audioBpfEnabled != 0);
  hpHz_.store(cfg.audioHpHz);
  hpQX100_.store(cfg.audioHpQX100);
  lpHz_.store(cfg.audioLpHz);
  lpQX100_.store(cfg.audioLpQX100);
  cfgDirty_.store(true);
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
  static int32_t buf[kFrames * 2];  // 2 KB, kept off the 4 KB task stack
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
      detector_.configure(fastX1000_.load() / 1000.0f, slowX1000_.load() / 1000.0f,
                          debounceMs_.load());
      bpfActive_ = bpfEnabled_.load();
      const float fs = (float)kSampleRate;
      hpL_.configure(Biquad::Type::HighPass, (float)hpHz_.load(), hpQX100_.load() / 100.0f, fs);
      hpR_.configure(Biquad::Type::HighPass, (float)hpHz_.load(), hpQX100_.load() / 100.0f, fs);
      lpL_.configure(Biquad::Type::LowPass, (float)lpHz_.load(), lpQX100_.load() / 100.0f, fs);
      lpR_.configure(Biquad::Type::LowPass, (float)lpHz_.load(), lpQX100_.load() / 100.0f, fs);
    }

    // Per-block DC removal (same math as the reference computeStereoRms), fused with
    // the raw-audio decimator feed and the optional band-pass before the RMS.
    const size_t frames = (bytes / sizeof(int32_t)) / 2;
    if (frames == 0) continue;
    int64_t sumL = 0, sumR = 0;
    for (size_t i = 0; i < frames; i++) {
      sumL += buf[2 * i] >> 8;
      sumR += buf[2 * i + 1] >> 8;
    }
    const float meanL = (float)((double)sumL / (double)frames);
    const float meanR = (float)((double)sumR / (double)frames);
    double accL = 0.0, accR = 0.0;
    for (size_t i = 0; i < frames; i++) {
      float l = (float)(buf[2 * i] >> 8) - meanL;
      float r = (float)(buf[2 * i + 1] >> 8) - meanR;
      // Raw (unfiltered) mono mix feeds the 2 kHz stream for the frontend FFT.
      if (decim_.feed(0.5f * (l + r))) {
        portENTER_CRITICAL(&chunkMux_);
        memcpy(chunkBuf_, decim_.chunk(), sizeof(chunkBuf_));
        chunkReady_.store(true);
        portEXIT_CRITICAL(&chunkMux_);
      }
      if (bpfActive_) {
        l = lpL_.process(hpL_.process(l));
        r = lpR_.process(hpR_.process(r));
      }
      accL += (double)l * l;
      accR += (double)r * r;
    }
    const float rl = sqrtf((float)(accL / (double)frames));
    const float rr = sqrtf((float)(accR / (double)frames));
    rmsL_.store(rl);
    rmsR_.store(rr);
    if (!enabled_.load()) continue;
    const float thr = thresholdProvider_ ? thresholdProvider_() : fixedThreshold_;
    const uint32_t now = millis();
    if (detector_.update(rl > rr ? rl : rr, thr, now) && onPeak_) onPeak_(now);
  }
}

}  // namespace gaia
