#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>

#include "../logic/Co2FrameParser.h"
#include "../logic/GaiaConfig.h"
#include "ConfigStore.h"

namespace gaia {

// CO2 sensor reader (UART1: RX 16, 9600 8N1). Real sensor only — the virtual pulse
// (blow -> synthetic ppm) is pure logic/ and lives in the App, which owns its timing.
//
// Sensor: SC8-CO2-5K (Shenzhen Shenchen Technology / "YYS SC8"), NDIR, 400-5000 ppm.
// It actively outputs one 16-byte 0x42 0x4D frame per second after power-on, and
// sending it ANY query stops that output until the next power cycle — so this
// service is strictly listen-only: the UART is opened without a TX pin and nothing
// is ever written to the sensor.
//
// Threading:
//  - A "co2" task (core 1, prio 1) drains the UART and owns the parser.
//  - realPpm()/stale()/haveFrame()/lastFrameMs()/frameCount()/errorCount() are safe
//    from any task.
//  - applyConfig() is safe from any task.
//  - begin() succeeds even if no sensor is attached; co2Ok() == !stale().
class Co2Service {
 public:
  bool begin(ConfigStore &store);
  void startTask();
  void applyConfig(const GaiaConfig &cfg);

  uint16_t realPpm() const { return realPpm_.load(); }
  // True when no valid frame arrived within co2StaleTimeoutMs, or none ever did.
  bool stale(uint32_t nowMs) const;
  bool co2Ok(uint32_t nowMs) const { return !stale(nowMs); }

  uint32_t frameCount() const { return frames_.load(); }
  uint32_t errorCount() const { return errors_.load(); }
  // Whether any valid frame ever arrived, and the millis() of the latest one
  // (meaningful only when haveFrame() is true). Source of truth for the packet's
  // ageS field — exact, not tick-resolution.
  bool haveFrame() const { return haveFrame_.load(); }
  uint32_t lastFrameMs() const { return lastFrameMs_.load(); }

 private:
  static void taskEntry(void *arg);
  void taskLoop();

  TaskHandle_t task_ = nullptr;
  bool began_ = false;

  Co2FrameParser parser_;    // co2-task-only after startTask()
  float emaPpm_ = 0.0f;      // co2-task-only
  bool emaSeeded_ = false;   // co2-task-only

  // Config handoff.
  std::atomic<uint16_t> emaAlphaX1000_{1000};  // 1000 = no smoothing
  std::atomic<uint32_t> staleMs_{5000};
  std::atomic<bool> enabled_{true};
  std::atomic<bool> rawLog_{false};

  // Task -> readers.
  std::atomic<uint16_t> realPpm_{0};
  std::atomic<uint32_t> lastFrameMs_{0};
  std::atomic<bool> haveFrame_{false};
  std::atomic<uint32_t> frames_{0}, errors_{0};
};

}  // namespace gaia
