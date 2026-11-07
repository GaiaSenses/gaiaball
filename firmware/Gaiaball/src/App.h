#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>

#include "logic/CommandGate.h"
#include "logic/NotifyThrottle.h"
#include "logic/VirtualCo2Pulse.h"
#include "services/AudioService.h"
#include "services/BleService.h"
#include "services/Co2Service.h"
#include "services/ConfigStore.h"
#include "services/Diagnostics.h"
#include "services/ImuService.h"
#include "services/LedController.h"
#include "services/PotService.h"

namespace gaia {

// Application core: owns every service, boots them in order and runs the App task
// (core 1, prio 2) that moves data between them. See firmware/docs/architecture.md.
//
// Cross-task boundaries handled here:
//  - Audio peak callback (audio task) only stores lastPeakMs_ + peakPending_; the App
//    task triggers the virtual CO2 pulse and drives the sound LED.
//  - Audio threshold provider (audio task) reads threshold_, an atomic the App task
//    refreshes after every PotService::poll() (PotService itself is single-task).
//  - ConfigStore onChange (NimBLE host task) only sets configDirty_; the App task
//    re-applies the config to every service.
class App {
 public:
  // Boots everything and starts all tasks. Call once from setup().
  void begin();

 private:
  static void taskEntry(void *arg);
  void taskLoop();

  void handleImuSample(const ImuSample &s);
  // THE one place that copies config scalars into the App-task mirrors (and derived
  // settings: gate PIN, throttle rate, virtual-pulse params). begin() and
  // applyConfigIfDirty() both call it; never copy a field outside it.
  void applySnapshot(const GaiaConfig &cfg);
  void applyConfigIfDirty();
  // Virtual pulse peak while active; otherwise the real reading (co2Enabled=1) or
  // the legacy fallback 600 (co2Enabled=0).
  uint16_t reportedCo2Ppm(uint32_t nowMs);
  void updateSoundLed(uint32_t nowMs);
  void maybeNotifyCo2(uint32_t nowMs);
  void sendDiag(uint32_t nowMs);
  void sendSensors(uint32_t nowMs);
  void maybeSendAudioChunk(uint32_t nowMs);
  void drainCommands();
  void dispatch(const BleCommand &cmd);
  void persistCapturedMounting();

  ConfigStore store_;
  CommandGate gate_;
  LedController led_;
  BleService ble_;
  ImuService imu_;
  AudioService audio_;
  PotService pot_;
  Co2Service co2_;
  Diagnostics diag_;
  NotifyThrottle imuThrottle_;

  TaskHandle_t task_ = nullptr;

  // Cross-task handoffs.
  std::atomic<uint32_t> lastPeakMs_{0};
  std::atomic<bool> peakPending_{false};
  std::atomic<float> threshold_{100000.0f};
  std::atomic<bool> configDirty_{false};

  // App-task-only state.
  VirtualCo2Pulse virtualPulse_;  // pure logic; triggered/consumed only here
  uint8_t imuNotifyHz_ = 0;
  uint16_t co2AlertPpm_ = 2000;
  bool co2Enabled_ = true;
  bool audioEnabled_ = true;
  bool potControls_ = true;
  bool audioBpf_ = true;
  uint16_t audioHpHz_ = 10;
  uint16_t audioLpHz_ = 200;
  bool peakSeen_ = false;
  uint32_t peakMs_ = 0;
  uint16_t imuSeq_ = 0;
  uint16_t co2Seq_ = 0;
  uint16_t diagSeq_ = 0;
  uint16_t sensorsSeq_ = 0;
  uint16_t audioChunkSeq_ = 0;
  uint32_t imuNotifiesInWindow_ = 0;
  uint32_t diagWindowStartMs_ = 0;
  uint32_t lastPotMs_ = 0;
  uint32_t lastLedMs_ = 0;
  uint32_t lastDiagMs_ = 0;
  uint32_t lastSensorsMs_ = 0;
  uint32_t lastCo2NotifyMs_ = 0;
  uint16_t lastCo2Ppm_ = 0;
  uint8_t lastCo2Flags_ = 0;
  bool co2Sent_ = false;
};

}  // namespace gaia
