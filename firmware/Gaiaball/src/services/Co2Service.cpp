#include "Co2Service.h"

#include <Arduino.h>
#include <esp_task_wdt.h>

#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kTag = "co2";
constexpr int kPinRx = 16;
// The board also routes GPIO17 to the sensor's RX pad, but the SC8 stops its
// active output permanently (until power-up) after receiving any query, so the
// UART is opened WITHOUT a TX pin: the firmware physically cannot talk back.
constexpr int kPinTxNone = -1;
constexpr uint32_t kBaud = 9600;
constexpr size_t kMaxBytesPerCycle = 64;
constexpr size_t kRawLineBytes = 16;  // bytes per hex-dump log line
HardwareSerial &uart() {
  static HardwareSerial s(1);
  return s;
}
}  // namespace

bool Co2Service::begin(ConfigStore &store) {
  applyConfig(store.get());
  uart().begin(kBaud, SERIAL_8N1, kPinRx, kPinTxNone);
  began_ = true;
  GB_LOGI(kTag, "UART1 ready (RX %d, listen-only, %u baud)", kPinRx, (unsigned)kBaud);
  return true;
}

void Co2Service::applyConfig(const GaiaConfig &cfg) {
  emaAlphaX1000_.store(cfg.co2EmaAlphaX1000 == 0 ? 1 : cfg.co2EmaAlphaX1000);
  staleMs_.store(cfg.co2StaleTimeoutMs);
  enabled_.store(cfg.co2Enabled != 0);
  rawLog_.store(cfg.co2RawLogMode != 0);
}

void Co2Service::startTask() {
  if (!began_ || task_ != nullptr) return;
  if (xTaskCreatePinnedToCore(&Co2Service::taskEntry, "co2", 3072, this, 1, &task_, 1) !=
      pdPASS) {
    task_ = nullptr;
    GB_LOGE(kTag, "task creation failed");
  }
}

void Co2Service::taskEntry(void *arg) {
  static_cast<Co2Service *>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void Co2Service::taskLoop() {
  uint8_t raw[kMaxBytesPerCycle];
  // Task WDT: registered and fed once per cycle (see the watchdog policy in
  // Diagnostics.h). The UART drain is bounded (kMaxBytesPerCycle) and non-blocking.
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    if (enabled_.load()) {
      size_t n = 0;
      while (n < kMaxBytesPerCycle && uart().available() > 0) {
        const int b = uart().read();
        if (b < 0) break;
        raw[n++] = (uint8_t)b;
      }
      for (size_t i = 0; i < n; i++) {
        if (parser_.feed(raw[i]) != Co2FrameParser::Result::Frame) continue;
        uint16_t ppm = parser_.ppm();
        // Optional EMA (TLV 0x28; 1000 = off). Seeds on the first reading.
        const uint16_t aX1000 = emaAlphaX1000_.load();
        if (aX1000 >= 1000) {
          emaPpm_ = (float)ppm;
          emaSeeded_ = true;
        } else {
          if (!emaSeeded_) emaPpm_ = (float)ppm;
          else emaPpm_ += (aX1000 / 1000.0f) * ((float)ppm - emaPpm_);
          emaSeeded_ = true;
          ppm = (uint16_t)lroundf(emaPpm_);
        }
        realPpm_.store(ppm);
        lastFrameMs_.store(millis());
        haveFrame_.store(true);
      }
      frames_.store(parser_.frameCount());
      errors_.store(parser_.errorCount());
      if (n > 0 && rawLog_.load()) {
        // Bench protocol capture: at most 4 lines per cycle (the logger also rate-limits).
        char line[kRawLineBytes * 3 + 1];
        for (size_t off = 0, lines = 0; off < n && lines < 4; off += kRawLineBytes, lines++) {
          size_t m = n - off < kRawLineBytes ? n - off : kRawLineBytes;
          for (size_t i = 0; i < m; i++) snprintf(line + i * 3, 4, "%02X ", raw[off + i]);
          GB_LOGI(kTag, "raw: %s", line);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

bool Co2Service::stale(uint32_t nowMs) const {
  if (!haveFrame_.load()) return true;
  return (uint32_t)(nowMs - lastFrameMs_.load()) > staleMs_.load();
}

}  // namespace gaia
