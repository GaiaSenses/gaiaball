#include "App.h"

#include <Arduino.h>
#include <esp_system.h>

#include "logic/PacketCodec.h"
#include "logic/Packets.h"
#include "services/Logger.h"

namespace gaia {

namespace {
constexpr const char *kTag = "app";

constexpr uint32_t kAppStack = 6144;
constexpr UBaseType_t kAppPrio = 2;
constexpr BaseType_t kAppCore = 1;

constexpr TickType_t kImuWaitTicks = pdMS_TO_TICKS(20);  // loop pacer when IMU is silent
constexpr uint32_t kPotPeriodMs = 100;                   // 10 Hz
constexpr uint32_t kLedPeriodMs = 50;                    // 20 Hz
constexpr uint32_t kDiagPeriodMs = 1000;                 // 1 Hz
constexpr uint32_t kCo2KeepaliveMs = 1000;               // 1 Hz
constexpr uint32_t kSoundLedHoldMs = 100;                // legacy peak flash

// Gated command opcodes (see docs/protocol.md).
constexpr uint8_t kCmdZero = 0x10;
constexpr uint8_t kCmdSave = 0x11;
constexpr uint8_t kCmdFactoryReset = 0x12;
constexpr uint8_t kCmdReboot = 0x13;
constexpr uint8_t kCmdMountCal = 0x14;

inline bool elapsed(uint32_t nowMs, uint32_t sinceMs, uint32_t periodMs) {
  return (uint32_t)(nowMs - sinceMs) >= periodMs;
}
}  // namespace

void App::begin() {
  Serial.begin(115200);
  Logger::begin(LogLevel::Info);
  GB_LOGI(kTag, "Gaiaball v2 booting");

  if (!store_.begin()) GB_LOGW(kTag, "NVS unavailable: running on volatile defaults");
  const GaiaConfig &cfg = store_.get();
  gate_.setPin(cfg.pin);
  imuNotifyHz_ = cfg.imuNotifyHz;
  co2AlertPpm_ = cfg.co2AlertPpm;
  imuThrottle_.setRateHz(imuNotifyHz_);
  // Runs on the caller's task (NimBLE host for BLE config writes): flag only.
  store_.setOnChange([this](const GaiaConfig &) { configDirty_.store(true); });

  led_.begin(store_);
  if (!ble_.begin(store_, gate_)) GB_LOGE(kTag, "BLE init failed");

  if (!imu_.begin(store_)) GB_LOGE(kTag, "IMU not available");

  pot_.begin(cfg);
  threshold_.store(pot_.threshold());
  // Both callbacks run on the audio task: atomics only.
  audio_.onPeak([this](uint32_t nowMs) {
    lastPeakMs_.store(nowMs);
    peakPending_.store(true);
  });
  audio_.setThresholdProvider([this]() { return threshold_.load(); });
  if (!audio_.begin(cfg)) GB_LOGE(kTag, "audio not available");

  co2_.begin(store_);
  diag_.begin();

  if (xTaskCreatePinnedToCore(&App::taskEntry, "app", kAppStack, this, kAppPrio, &task_,
                              kAppCore) != pdPASS) {
    task_ = nullptr;
    GB_LOGE(kTag, "app task creation failed");
  }

  imu_.startTask();
  audio_.startTask();
  co2_.startTask();
  GB_LOGI(kTag, "boot complete, free heap %u", (unsigned)esp_get_free_heap_size());
}

void App::taskEntry(void *arg) {
  static_cast<App *>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void App::taskLoop() {
  diag_.watchTask();
  const uint32_t startMs = millis();
  lastPotMs_ = lastLedMs_ = lastDiagMs_ = diagWindowStartMs_ = startMs;

  ImuSample sample;
  for (;;) {
    // Loop pacer: wakes on every IMU sample, or after 20 ms without one.
    if (imu_.sampleQueue() != nullptr &&
        xQueueReceive(imu_.sampleQueue(), &sample, kImuWaitTicks) == pdTRUE) {
      handleImuSample(sample);
    } else if (imu_.sampleQueue() == nullptr) {
      vTaskDelay(kImuWaitTicks);
    }

    const uint32_t nowMs = millis();
    applyConfigIfDirty();

    if (peakPending_.exchange(false)) {
      peakMs_ = lastPeakMs_.load();
      peakSeen_ = true;
      co2_.triggerVirtualPulse(nowMs);
    }

    if (elapsed(nowMs, lastPotMs_, kPotPeriodMs)) {
      lastPotMs_ = nowMs;
      pot_.poll(nowMs);
      threshold_.store(pot_.threshold());
    }

    if (elapsed(nowMs, lastLedMs_, kLedPeriodMs)) {
      lastLedMs_ = nowMs;
      updateSoundLed(nowMs);
      maybeNotifyCo2(nowMs);
      // CO2 alert LED follows the real reading only; a stale sensor never holds it on.
      const uint16_t realPpm = co2_.stale(nowMs) ? 0 : co2_.realPpm();
      led_.update(nowMs, realPpm, co2AlertPpm_);
    }

    if (elapsed(nowMs, lastDiagMs_, kDiagPeriodMs)) {
      lastDiagMs_ = nowMs;
      sendDiag(nowMs);
    }

    drainCommands();
    persistCapturedMounting();
    diag_.feed();
  }
}

void App::handleImuSample(const ImuSample &s) {
  if (!imuThrottle_.shouldSend(s.tUs)) return;
  if (!ble_.anySubscriber(1)) return;
  const float q[4] = {s.q.w, s.q.x, s.q.y, s.q.z};
  const ImuPacket p = makeImuPacket(imuSeq_++, s.tUs, q, s.acc, s.flags);
  ble_.notifyImu(p);
  imuNotifiesInWindow_++;
}

void App::applyConfigIfDirty() {
  if (!configDirty_.exchange(false)) return;
  // Scalar reads of the live config on the App task (allowed by ConfigStore).
  const GaiaConfig &cfg = store_.get();
  gate_.setPin(cfg.pin);
  imu_.applyConfig(cfg);
  audio_.applyConfig(cfg);
  co2_.applyConfig(cfg);
  pot_.applyConfig(cfg);
  threshold_.store(pot_.threshold());
  co2AlertPpm_ = cfg.co2AlertPpm;
  // setRateHz() restarts the schedule: only call it on an actual change.
  if (cfg.imuNotifyHz != imuNotifyHz_) {
    imuNotifyHz_ = cfg.imuNotifyHz;
    imuThrottle_.setRateHz(imuNotifyHz_);
  }
  GB_LOGI(kTag, "config applied");
}

void App::updateSoundLed(uint32_t nowMs) {
  // Legacy: LED flashes 100 ms on a peak and stays on while the virtual pulse is active.
  const bool recentPeak = peakSeen_ && (uint32_t)(nowMs - peakMs_) < kSoundLedHoldMs;
  led_.set(LedPriority::Source::SoundEvent, recentPeak || co2_.virtualActive(nowMs));
}

void App::maybeNotifyCo2(uint32_t nowMs) {
  // Track frame arrivals to derive the reading's age (resolution = this 20 Hz tick).
  const uint32_t frames = co2_.frameCount();
  if (frames != co2LastFrameCount_) {
    co2LastFrameCount_ = frames;
    co2LastFrameMs_ = nowMs;
    co2HaveFrame_ = true;
  }

  const uint16_t ppm = co2_.reportedPpm(nowMs);
  const bool stale = co2_.stale(nowMs);
  uint8_t flags = 0;
  // valid = ppm is backed by a fresh real reading (co2Enabled and not stale).
  if (store_.get().co2Enabled && !stale) flags |= kCo2Valid;
  if (stale) flags |= kCo2Stale;
  if (co2_.virtualActive(nowMs)) flags |= kCo2VirtualActive;

  const bool changed = !co2Sent_ || ppm != lastCo2Ppm_ || flags != lastCo2Flags_;
  if (!changed && !elapsed(nowMs, lastCo2NotifyMs_, kCo2KeepaliveMs)) return;

  uint32_t ageS = 255;
  if (co2HaveFrame_) {
    ageS = (uint32_t)(nowMs - co2LastFrameMs_) / 1000;
    if (ageS > 255) ageS = 255;
  }

  Co2Packet p = {};
  p.version = kProtocolVersion;
  p.type = (uint8_t)PacketType::Co2Sample;
  p.seq = co2Seq_++;
  p.timestampMs = nowMs;
  p.ppm = ppm;
  p.flags = flags;
  p.ageS = (uint8_t)ageS;
  ble_.notifyCo2(p);

  co2Sent_ = true;
  lastCo2Ppm_ = ppm;
  lastCo2Flags_ = flags;
  lastCo2NotifyMs_ = nowMs;
}

void App::sendDiag(uint32_t nowMs) {
  const uint32_t windowMs = nowMs - diagWindowStartMs_;
  uint32_t hzX10 = windowMs ? (imuNotifiesInWindow_ * 10000u + windowMs / 2) / windowMs : 0;
  if (hzX10 > 0xFFFF) hzX10 = 0xFFFF;
  imuNotifiesInWindow_ = 0;
  diagWindowStartMs_ = nowMs;

  const DiagPacket p = diag_.collect(diagSeq_++, (uint16_t)hzX10, imu_, audio_, co2_, nowMs);
  ble_.notifyDiag(p);
  diag_.driveErrorLed(led_, imu_, audio_);
}

void App::drainCommands() {
  QueueHandle_t q = ble_.commandQueue();
  if (q == nullptr) return;
  BleCommand cmd;
  while (xQueueReceive(q, &cmd, 0) == pdTRUE) dispatch(cmd);
}

void App::dispatch(const BleCommand &cmd) {
  switch (cmd.opcode) {
    case kCmdLedOff:
      led_.set(LedPriority::Source::BleCommand, false);
      return;
    case kCmdLedOn:
      led_.set(LedPriority::Source::BleCommand, true);
      return;
    case kCmdZero:
    case kCmdSave:
    case kCmdFactoryReset:
    case kCmdReboot:
    case kCmdMountCal:
      break;
    default:
      GB_LOGW(kTag, "unknown opcode 0x%02X (slot %u)", cmd.opcode, cmd.connSlot);
      return;
  }

  if (!cmd.unlockedAtEnqueue) {
    GB_LOGW(kTag, "opcode 0x%02X refused: slot %u locked", cmd.opcode, cmd.connSlot);
    return;
  }

  switch (cmd.opcode) {
    case kCmdZero:
      GB_LOGI(kTag, "zero orientation requested");
      imu_.requestZeroOrientation();
      break;
    case kCmdSave:
      if (store_.save())
        GB_LOGI(kTag, "config saved");
      else
        GB_LOGE(kTag, "config save failed");
      break;
    case kCmdFactoryReset:
      GB_LOGW(kTag, "factory reset, rebooting");
      store_.factoryReset();
      Serial.flush();
      esp_restart();
      break;
    case kCmdReboot:
      GB_LOGW(kTag, "reboot requested");
      Serial.flush();
      esp_restart();
      break;
    case kCmdMountCal:
      GB_LOGI(kTag, "mounting calibration requested");
      imu_.requestMountingCalibration();
      break;
  }
}

void App::persistCapturedMounting() {
  float q[4];
  if (!imu_.takeCapturedMounting(q)) return;
  store_.setMountQuat(q);
  if (store_.save())
    GB_LOGI(kTag, "mounting saved (%.4f %.4f %.4f %.4f)", q[0], q[1], q[2], q[3]);
  else
    GB_LOGE(kTag, "mounting save failed");
}

}  // namespace gaia
