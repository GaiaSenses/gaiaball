#include "App.h"

#include <esp_wifi.h>

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
constexpr uint32_t kSensorsPeriodMs = 200;               // 5 Hz (audio/pot levels)
constexpr uint32_t kCo2KeepaliveMs = 1000;               // 1 Hz
constexpr uint32_t kSoundLedHoldMs = 100;                // legacy peak flash
// Legacy CO2_NORMAL_VALUE: reported when the sensor is disabled so the site's Pd
// patch still receives a plausible baseline (documented in docs/protocol.md).
constexpr uint16_t kCo2FallbackPpm = 600;

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

  // Guarantee the Wi-Fi radio is off: nothing here initializes it, and BLE/Wi-Fi
  // coexistence would wreck the notify pacing. ESP_ERR_WIFI_NOT_INIT is the
  // expected (already-off) result; anything else gets stopped and torn down.
  {
    const esp_err_t werr = esp_wifi_stop();
    if (werr != ESP_ERR_WIFI_NOT_INIT) {
      esp_wifi_deinit();
      GB_LOGW(kTag, "Wi-Fi was initialized (stop=%d); deinitialized", (int)werr);
    }
  }

  if (!store_.begin()) GB_LOGW(kTag, "NVS unavailable: running on volatile defaults");
  const GaiaConfig &cfg = store_.get();
  applySnapshot(cfg);
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
  // Applied only now so the whole boot (incl. the I2C identity probe) always prints;
  // from here on the config switch (TLV 0x50, default OFF) rules.
  Logger::setEnabled(cfg.serialLogEnabled != 0);
}

void App::taskEntry(void *arg) {
  static_cast<App *>(arg)->taskLoop();
  vTaskDelete(nullptr);
}

void App::taskLoop() {
  diag_.watchTask();
  const uint32_t startMs = millis();
  lastPotMs_ = lastLedMs_ = lastDiagMs_ = lastSensorsMs_ = diagWindowStartMs_ = startMs;

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
      virtualPulse_.trigger(nowMs);
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

    if (elapsed(nowMs, lastSensorsMs_, kSensorsPeriodMs)) {
      lastSensorsMs_ = nowMs;
      sendSensors(nowMs);
    }

    maybeSendAudioChunk(nowMs);

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

void App::applySnapshot(const GaiaConfig &cfg) {
  gate_.setPin(cfg.pin);
  co2AlertPpm_ = cfg.co2AlertPpm;
  co2Enabled_ = cfg.co2Enabled != 0;
  audioEnabled_ = cfg.audioEnabled != 0;
  potControls_ = cfg.potControlsThreshold != 0;
  audioBpf_ = cfg.audioBpfEnabled != 0;
  audioHpHz_ = cfg.audioHpHz;
  audioLpHz_ = cfg.audioLpHz;
  virtualPulse_.configure(cfg.virtualPeakPpm, cfg.virtualDelayMs, cfg.virtualHoldMs);
  // setRateHz() restarts the schedule: only call it on an actual change.
  if (cfg.imuNotifyHz != imuNotifyHz_) {
    imuNotifyHz_ = cfg.imuNotifyHz;
    imuThrottle_.setRateHz(imuNotifyHz_);
  }
  // Logger::setEnabled is intentionally NOT here: begin() applies it only after the
  // whole boot sequence has printed (see end of begin()).
}

void App::applyConfigIfDirty() {
  if (!configDirty_.exchange(false)) return;
  // Scalar reads of the live config on the App task (allowed by ConfigStore).
  const GaiaConfig &cfg = store_.get();
  imu_.applyConfig(cfg);
  audio_.applyConfig(cfg);
  co2_.applyConfig(cfg);
  pot_.applyConfig(cfg);
  threshold_.store(pot_.threshold());
  applySnapshot(cfg);
  Logger::setEnabled(cfg.serialLogEnabled != 0);
  GB_LOGI(kTag, "config applied");
}

void App::updateSoundLed(uint32_t nowMs) {
  // Legacy: LED flashes 100 ms on a peak and stays on while the virtual pulse is active.
  const bool recentPeak = peakSeen_ && (uint32_t)(nowMs - peakMs_) < kSoundLedHoldMs;
  led_.setSoundLed(recentPeak || virtualPulse_.active(nowMs));
}

uint16_t App::reportedCo2Ppm(uint32_t nowMs) {
  if (virtualPulse_.active(nowMs)) return virtualPulse_.peakPpm();
  return co2Enabled_ ? co2_.realPpm() : kCo2FallbackPpm;
}

void App::maybeNotifyCo2(uint32_t nowMs) {
  const uint16_t ppm = reportedCo2Ppm(nowMs);
  const bool stale = co2_.stale(nowMs);
  uint8_t flags = 0;
  // valid = ppm is backed by a fresh real reading (co2Enabled and not stale).
  if (co2Enabled_ && !stale) flags |= kCo2Valid;
  if (stale) flags |= kCo2Stale;
  if (virtualPulse_.active(nowMs)) flags |= kCo2VirtualActive;

  const bool changed = !co2Sent_ || ppm != lastCo2Ppm_ || flags != lastCo2Flags_;
  if (!changed && !elapsed(nowMs, lastCo2NotifyMs_, kCo2KeepaliveMs)) return;

  uint32_t ageS = 255;
  if (co2_.haveFrame()) {
    ageS = (uint32_t)(nowMs - co2_.lastFrameMs()) / 1000;
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
}

// Live sensor levels for the config-app charts (5 Hz, diag characteristic).
void App::sendSensors(uint32_t nowMs) {
  if (!ble_.anySubscriber(5)) return;  // diag characteristic (…0005)
  uint8_t flags = 0;
  if (audio_.ok()) flags |= kSensAudioOk;
  if (audioEnabled_) flags |= kSensAudioEnabled;
  if (potControls_) flags |= kSensPotControls;
  if (audioBpf_) flags |= kSensAudioFiltered;
  const SensorsPacket p =
      makeSensorsPacket(sensorsSeq_++, nowMs, audio_.lastRmsLeft(), audio_.lastRmsRight(),
                        pot_.threshold(), pot_.filteredRaw(), flags);
  ble_.notifySensors(p);
}

// Raw 2 kHz audio stream for the frontend spectrum analyzer. Chunks arrive from the
// audio task every 48 ms; this runs every App loop (<= 20 ms), so none are missed
// while a diag subscriber is listening. Without subscribers the chunk is discarded.
void App::maybeSendAudioChunk(uint32_t nowMs) {
  AudioChunkPacket p;
  if (!audio_.takeAudioChunk(p.samples)) return;
  if (!ble_.anySubscriber(5)) return;
  p.version = kProtocolVersion;
  p.type = (uint8_t)PacketType::AudioChunk;
  p.seq = audioChunkSeq_++;
  p.timestampMs = nowMs;
  p.sampleRateHz = AudioDecimator::kOutputRateHz;
  p.count = (uint8_t)AudioDecimator::kChunkSamples;
  p.flags = audioBpf_ ? kAudioBpfEnabled : 0;
  p.hpHz = audioHpHz_;
  p.lpHz = audioLpHz_;
  ble_.notifyAudioChunk(p);
}

void App::drainCommands() {
  QueueHandle_t q = ble_.commandQueue();
  if (q == nullptr) return;
  BleCommand cmd;
  while (xQueueReceive(q, &cmd, 0) == pdTRUE) dispatch(cmd);
}

void App::dispatch(const BleCommand &cmd) {
  switch (cmd.opcode) {
    // 0x00/0x01 (legacy LED command) are reserved and ignored: no client ever sent
    // them (the site declares the characteristic but never writes it), and the board
    // has no LED for them. They fall through to "unknown opcode" below.
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
