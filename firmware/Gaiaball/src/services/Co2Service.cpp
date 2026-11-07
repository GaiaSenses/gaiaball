#include "Co2Service.h"

#include <Arduino.h>

#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kTag = "co2";
constexpr int kPinRx = 16;
constexpr int kPinTx = 17;
constexpr uint32_t kBaud = 9600;
constexpr size_t kMaxBytesPerCycle = 64;
constexpr size_t kRawLineBytes = 16;  // bytes per hex-dump log line
// S8 Modbus poll period. The sensor updates its measurement every 4 s; polling at
// 2 s halves the worst-case reading age without meaningful UART/CPU cost.
constexpr uint32_t kS8PollMs = 2000;
HardwareSerial &uart() {
  static HardwareSerial s(1);
  return s;
}
}  // namespace

bool Co2Service::begin(ConfigStore &store) {
  applyConfig(store.get());
  uart().begin(kBaud, SERIAL_8N1, kPinRx, kPinTx);
  began_ = true;
  GB_LOGI(kTag, "UART1 ready (RX %d, TX %d, %u baud)", kPinRx, kPinTx, (unsigned)kBaud);
  return true;
}

void Co2Service::applyConfig(const GaiaConfig &cfg) {
  protocol_.store(cfg.co2Protocol);
  checksumVariant_.store(cfg.co2ChecksumVariant);
  parserDirty_.store(true);
  peakPpm_.store(cfg.virtualPeakPpm);
  delayMs_.store(cfg.virtualDelayMs);
  holdMs_.store(cfg.virtualHoldMs);
  pulseDirty_.store(true);
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
  uint32_t lastPollMs = 0;
  for (;;) {
    if (parserDirty_.exchange(false)) {
      Co2FrameSpec spec;
      spec.frameLen = frameLen_.load();
      spec.ppmHiIndex = ppmHiIndex_.load();
      spec.checksumVariant = checksumVariant_.load();
      parser_.configure(spec);
    }
    if (enabled_.load()) {
      const bool modbus = protocol_.load() == 0;
      if (modbus) {
        const uint32_t nowMs = millis();
        if (lastPollMs == 0 || (uint32_t)(nowMs - lastPollMs) >= kS8PollMs) {
          lastPollMs = nowMs;
          s8_.reset();  // a response never spans polls; drop any stale partial
          uart().write(kS8ReadCo2Request, kS8RequestLen);
        }
      }
      size_t n = 0;
      while (n < kMaxBytesPerCycle && uart().available() > 0) {
        const int b = uart().read();
        if (b < 0) break;
        raw[n++] = (uint8_t)b;
      }
      for (size_t i = 0; i < n; i++) {
        bool gotFrame = false;
        uint16_t ppm = 0;
        if (modbus) {
          if (s8_.feed(raw[i]) == S8ResponseParser::Result::Frame) {
            gotFrame = true;
            ppm = s8_.ppm();
          }
        } else if (parser_.feed(raw[i]) == Co2FrameParser::Result::Frame) {
          gotFrame = true;
          ppm = parser_.ppm();
        }
        if (gotFrame) {
          realPpm_.store(ppm);
          lastFrameMs_.store(millis());
          haveFrame_.store(true);
        }
      }
      frames_.store(modbus ? s8_.frameCount() : parser_.frameCount());
      errors_.store(modbus ? s8_.errorCount() : parser_.errorCount());
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

void Co2Service::syncPulseConfig() {
  if (pulseDirty_.exchange(false))
    pulse_.configure(peakPpm_.load(), delayMs_.load(), holdMs_.load());
}

void Co2Service::triggerVirtualPulse(uint32_t nowMs) {
  syncPulseConfig();
  pulse_.trigger(nowMs);
}

bool Co2Service::virtualActive(uint32_t nowMs) {
  syncPulseConfig();
  return pulse_.active(nowMs);
}

uint16_t Co2Service::reportedPpm(uint32_t nowMs) {
  if (virtualActive(nowMs)) return pulse_.peakPpm();
  return enabled_.load() ? realPpm_.load() : kFallbackPpm;
}

}  // namespace gaia
