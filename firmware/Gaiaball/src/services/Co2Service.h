#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>

#include "../logic/Co2FrameParser.h"
#include "../logic/GaiaConfig.h"
#include "../logic/VirtualCo2Pulse.h"
#include "ConfigStore.h"

namespace gaia {

// CO2 sensor reader (UART1: RX 16, TX 17, 9600 8N1, legacy pins) plus the virtual pulse.
//
// Threading:
//  - A "co2" task (core 1, prio 1) drains the UART and owns the Co2FrameParser.
//  - realPpm()/stale()/frameCount()/errorCount() are safe from any task.
//  - The virtual pulse is NOT thread-safe: triggerVirtualPulse(), virtualActive() and
//    reportedPpm() must all be called from ONE task (the App task).
//  - applyConfig() is safe from any task: the parser spec is handed to the co2 task
//    through atomics + a dirty flag; pulse parameters are picked up lazily by the
//    pulse-owning (App) task on its next trigger/active/reported call.
//  - begin() succeeds even if no sensor is attached; co2Ok() == !stale().
class Co2Service {
 public:
  // Legacy CO2_NORMAL_VALUE, reported when the sensor is disabled.
  static constexpr uint16_t kFallbackPpm = 600;

  bool begin(ConfigStore &store);
  void startTask();
  void applyConfig(const GaiaConfig &cfg);

  uint16_t realPpm() const { return realPpm_.load(); }
  // True when no valid frame arrived within co2StaleTimeoutMs, or none ever did.
  bool stale(uint32_t nowMs) const;
  bool co2Ok(uint32_t nowMs) const { return !stale(nowMs); }

  void triggerVirtualPulse(uint32_t nowMs);
  bool virtualActive(uint32_t nowMs);
  // Virtual pulse peak while active; otherwise the real reading (co2Enabled=1)
  // or kFallbackPpm (co2Enabled=0).
  uint16_t reportedPpm(uint32_t nowMs);

  uint32_t frameCount() const { return frames_.load(); }
  uint32_t errorCount() const { return errors_.load(); }

 private:
  static void taskEntry(void *arg);
  void taskLoop();
  void syncPulseConfig();

  TaskHandle_t task_ = nullptr;
  bool began_ = false;

  Co2FrameParser parser_;  // co2-task-only after startTask()
  VirtualCo2Pulse pulse_;  // App-task-only

  // Config handoff.
  std::atomic<uint8_t> frameLen_{16}, ppmHiIndex_{6}, checksumVariant_{0};
  std::atomic<bool> parserDirty_{true};
  std::atomic<uint16_t> peakPpm_{2100};
  std::atomic<uint32_t> delayMs_{1000}, holdMs_{17000};
  std::atomic<bool> pulseDirty_{true};
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
