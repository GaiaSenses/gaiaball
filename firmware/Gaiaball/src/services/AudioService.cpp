#include "AudioService.h"

#include <Arduino.h>
#include <driver/i2s.h>

#include "../logic/AudioBlock.h"
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
  cfgDirty_.store(true);
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
  for (;;) {
    size_t bytes = 0;
    // Blocks until a DMA buffer is ready (16 ms); fine here, this is the audio task.
    const esp_err_t err = i2s_read(kPort, buf, sizeof(buf), &bytes, portMAX_DELAY);
    if (err != ESP_OK || bytes == 0) {
      if (i2sErrors_.load() < UINT32_MAX) i2sErrors_.fetch_add(1);
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if (cfgDirty_.exchange(false)) {
      detector_.configure(fastX1000_.load() / 1000.0f, slowX1000_.load() / 1000.0f,
                          debounceMs_.load());
    }
    const StereoRms r = computeStereoRms(buf, bytes / sizeof(int32_t));
    rmsL_.store(r.left);
    rmsR_.store(r.right);
    if (!enabled_.load()) continue;
    const float thr = thresholdProvider_ ? thresholdProvider_() : fixedThreshold_;
    const uint32_t now = millis();
    if (detector_.update(r.left > r.right ? r.left : r.right, thr, now) && onPeak_) onPeak_(now);
  }
}

}  // namespace gaia
