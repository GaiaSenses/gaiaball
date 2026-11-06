#include "ImuService.h"

#include <Wire.h>
#include <math.h>

#include "../logic/Packets.h"
#include "ConfigStore.h"
#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kTag = "imu";
constexpr int kSdaPin = 21;
constexpr int kSclPin = 22;
constexpr uint32_t kI2cHz = 400000;
constexpr bool kAd0Val = false;  // AD0 low, as the legacy firmware
constexpr int kBeginAttempts = 10;
constexpr uint32_t kBeginRetryMs = 500;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(5);
constexpr TickType_t kIdleTicks = pdMS_TO_TICKS(1000);
constexpr int kMaxPacketsPerCycle = 16;
constexpr float kMovingThresholdDeg = 0.25f;
constexpr double kQ30 = 1073741824.0;  // 2^30
constexpr uint32_t kRateWindowUs = 1000000;

inv_icm20948_sensor quatSensorFor(uint8_t mode) {
  return mode == 1 ? INV_ICM20948_SENSOR_ORIENTATION : INV_ICM20948_SENSOR_GAME_ROTATION_VECTOR;
}
DMP_ODR_Registers quatOdrRegFor(uint8_t mode) {
  return mode == 1 ? DMP_ODR_Reg_Quat9 : DMP_ODR_Reg_Quat6;
}
uint8_t clampMode(uint8_t mode) { return mode == 1 ? 1 : 0; }

// Loads the persisted mounting quaternion; falls back to identity if unusable.
Quat loadMount(const GaiaConfig &cfg) {
  const float *m = cfg.mountQuat;
  const float n2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[3] * m[3];
  if (!isfinite(n2) || n2 < 0.5f || n2 > 1.5f) return Quat{1, 0, 0, 0};
  const float inv = 1.0f / sqrtf(n2);
  return Quat{m[0] * inv, m[1] * inv, m[2] * inv, m[3] * inv};
}
}  // namespace

bool ImuService::begin(ConfigStore &store) {
  store_ = &store;
  const GaiaConfig &cfg = store.get();
  qMount_ = loadMount(cfg);
  activeMode_ = clampMode(cfg.orientationMode);
  desiredMode_.store(activeMode_);
  slerpAmount_.store(cfg.slerpAmount > 100 ? 100 : cfg.slerpAmount);

  if (queue_ == nullptr) queue_ = xQueueCreate(kQueueDepth, sizeof(ImuSample));
  if (queue_ == nullptr) GB_LOGE(kTag, "sample queue allocation failed");

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(kI2cHz);

  bool found = false;
  for (int attempt = 1; attempt <= kBeginAttempts; ++attempt) {
    icm_.begin(Wire, kAd0Val);
    if (icm_.status == ICM_20948_Stat_Ok) {
      found = true;
      break;
    }
    GB_LOGW(kTag, "begin attempt %d/%d failed: %s", attempt, kBeginAttempts, icm_.statusString());
    if (attempt < kBeginAttempts) delay(kBeginRetryMs);  // setup-time only
  }
  if (!found) {
    GB_LOGE(kTag, "ICM-20948 not found; IMU disabled");
    imuOk_ = false;
    return false;
  }

  imuOk_ = initDmp(activeMode_);
  if (imuOk_) {
    GB_LOGI(kTag, "DMP ready (%s)", activeMode_ == 1 ? "Quat9" : "Quat6");
  } else {
    GB_LOGE(kTag, "DMP init failed; IMU disabled");
  }
  return imuOk_;
}

bool ImuService::initDmp(uint8_t mode) {
  bool ok = true;
  ok &= icm_.initializeDMP() == ICM_20948_Stat_Ok;
  ok &= icm_.enableDMPSensor(quatSensorFor(mode)) == ICM_20948_Stat_Ok;
  ok &= icm_.enableDMPSensor(INV_ICM20948_SENSOR_ACCELEROMETER) == ICM_20948_Stat_Ok;
  ok &= icm_.setDMPODRrate(quatOdrRegFor(mode), 0) == ICM_20948_Stat_Ok;  // 0 = max rate
  ok &= icm_.setDMPODRrate(DMP_ODR_Reg_Accel, 0) == ICM_20948_Stat_Ok;
  ok &= icm_.enableFIFO() == ICM_20948_Stat_Ok;
  ok &= icm_.enableDMP() == ICM_20948_Stat_Ok;
  ok &= icm_.resetDMP() == ICM_20948_Stat_Ok;
  ok &= icm_.resetFIFO() == ICM_20948_Stat_Ok;
  return ok;
}

bool ImuService::switchMode(uint8_t mode) {
  const uint8_t old = activeMode_;
  bool ok = true;
  ok &= icm_.enableDMP(false) == ICM_20948_Stat_Ok;
  ok &= icm_.enableDMPSensor(quatSensorFor(old), false) == ICM_20948_Stat_Ok;
  ok &= icm_.enableDMPSensor(quatSensorFor(mode), true) == ICM_20948_Stat_Ok;
  ok &= icm_.setDMPODRrate(quatOdrRegFor(mode), 0) == ICM_20948_Stat_Ok;
  ok &= icm_.enableDMP(true) == ICM_20948_Stat_Ok;
  ok &= icm_.resetDMP() == ICM_20948_Stat_Ok;
  ok &= icm_.resetFIFO() == ICM_20948_Stat_Ok;
  activeMode_ = mode;
  // The reference frame changed (Quat6 yaw is arbitrary, Quat9 is magnetic north):
  // a zero captured in the old frame is meaningless now.
  qZero_ = Quat{1, 0, 0, 0};
  havePrev_ = false;
  if (ok) {
    GB_LOGI(kTag, "mode -> %s", mode == 1 ? "Quat9" : "Quat6");
  } else {
    GB_LOGE(kTag, "mode switch to %s reported errors", mode == 1 ? "Quat9" : "Quat6");
  }
  return ok;
}

void ImuService::applyConfig(const GaiaConfig &cfg) {
  desiredMode_.store(clampMode(cfg.orientationMode));
  slerpAmount_.store(cfg.slerpAmount > 100 ? 100 : cfg.slerpAmount);
}

void ImuService::startTask() {
  if (task_ != nullptr) return;
  if (xTaskCreatePinnedToCore(&ImuService::taskEntry, "imu", 6144, this, 3, &task_, 1) != pdPASS) {
    task_ = nullptr;
    GB_LOGE(kTag, "task creation failed");
  }
}

void ImuService::taskEntry(void *arg) { static_cast<ImuService *>(arg)->taskLoop(); }

void ImuService::taskLoop() {
  if (!imuOk_ || queue_ == nullptr) {
    // IMU unavailable: idle forever so the rest of the system keeps running.
    for (;;) vTaskDelay(kIdleTicks);
  }
  rateWindowStartUs_ = micros();
  for (;;) {
    const uint8_t wanted = desiredMode_.load();
    if (wanted != activeMode_) switchMode(wanted);
    drainFifo();
    updateRate(micros());
    vTaskDelay(kPollTicks);
  }
}

void ImuService::drainFifo() {
  icm_20948_DMP_data_t data;
  for (int n = 0; n < kMaxPacketsPerCycle; ++n) {
    icm_.readDMPdataFromFIFO(&data);
    const ICM_20948_Status_e st = icm_.status;
    if (st == ICM_20948_Stat_Ok || st == ICM_20948_Stat_FIFOMoreDataAvail) {
      processPacket(data, micros());
      if (st == ICM_20948_Stat_Ok) return;  // FIFO empty after this packet
      continue;
    }
    if (st == ICM_20948_Stat_UnrecognisedDMPHeader || st == ICM_20948_Stat_UnrecognisedDMPHeader2) {
      // Packet framing lost: the FIFO overflowed (wrapped) or was corrupted. Resync.
      const uint16_t c = overflowCount_.load();
      if (c != UINT16_MAX) overflowCount_.store(c + 1);
      icm_.resetFIFO();
      GB_LOGW(kTag, "FIFO desync/overflow (#%u); FIFO reset", (unsigned)(c + 1));
    }
    // FIFONoDataAvail / FIFOIncompleteData: wait for the next wake. I2C errors: same.
    return;
  }
}

void ImuService::processPacket(const icm_20948_DMP_data_t &d, uint32_t tUs) {
  if ((d.header & DMP_header_bitmap_Accel) != 0) {
    lastAcc_[0] = d.Raw_Accel.Data.X;
    lastAcc_[1] = d.Raw_Accel.Data.Y;
    lastAcc_[2] = d.Raw_Accel.Data.Z;
  }

  const bool nine = activeMode_ == 1;
  const uint16_t quatBit = nine ? DMP_header_bitmap_Quat9 : DMP_header_bitmap_Quat6;
  if ((d.header & quatBit) == 0) return;  // accel-only (or stale-mode) packet

  int32_t q1, q2, q3;
  if (nine) {
    q1 = d.Quat9.Data.Q1;
    q2 = d.Quat9.Data.Q2;
    q3 = d.Quat9.Data.Q3;
  } else {
    q1 = d.Quat6.Data.Q1;
    q2 = d.Quat6.Data.Q2;
    q3 = d.Quat6.Data.Q3;
  }
  ++rateCount_;

  ImuSample s;
  s.tUs = tUs;
  s.acc[0] = lastAcc_[0];
  s.acc[1] = lastAcc_[1];
  s.acc[2] = lastAcc_[2];
  s.flags = nine ? kImuNineAxis : 0;

  Quat raw;
  if (!sanitizeQuat(q1 / kQ30, q2 / kQ30, q3 / kQ30, raw)) {
    // Invalid packet: no orientation math, no calibration capture. Publish the last
    // good output with quatValid=0; the consumer decides what to do.
    s.q = prevOut_;
    publish(s);
    return;
  }

  if (mountRequested_.exchange(false)) {
    qMount_ = raw;
    qZero_ = Quat{1, 0, 0, 0};  // the old zero lived in the previous mounting frame
    havePrev_ = false;
    if (store_ != nullptr) {
      GaiaConfig &cfg = store_->get();
      cfg.mountQuat[0] = raw.w;
      cfg.mountQuat[1] = raw.x;
      cfg.mountQuat[2] = raw.y;
      cfg.mountQuat[3] = raw.z;
      const bool saved = store_->save();
      GB_LOGI(kTag, "mounting calibrated (%s)", saved ? "saved" : "NOT saved");
    }
  }

  const Quat mounted = quatMultiply(quatConjugate(qMount_), raw);
  if (zeroRequested_.exchange(false)) {
    qZero_ = mounted;
    havePrev_ = false;  // jump straight to the new reference, no smoothing glide
    GB_LOGI(kTag, "orientation zeroed");
  }
  Quat q = quatMultiply(quatConjugate(qZero_), mounted);

  const uint8_t slerp = slerpAmount_.load();
  if (slerp > 0 && havePrev_) {
    q = quatSlerp(prevOut_, q, 1.0f - (float)slerp / 100.0f * 0.9f);
  }

  s.flags |= kImuQuatValid;
  if (havePrev_ && quatAngleDeg(prevOut_, q) > kMovingThresholdDeg) s.flags |= kImuMoving;
  s.q = q;
  prevOut_ = q;
  havePrev_ = true;
  publish(s);
}

void ImuService::publish(const ImuSample &s) {
  if (xQueueSend(queue_, &s, 0) == pdTRUE) return;
  // Full: drop the oldest so the newest sample always gets in (single producer).
  ImuSample dropped;
  xQueueReceive(queue_, &dropped, 0);
  xQueueSend(queue_, &s, 0);
}

void ImuService::updateRate(uint32_t nowUs) {
  const uint32_t elapsed = nowUs - rateWindowStartUs_;
  if (elapsed < kRateWindowUs) return;
  measuredHz_.store((float)rateCount_ * 1e6f / (float)elapsed);
  rateCount_ = 0;
  rateWindowStartUs_ = nowUs;
}

}  // namespace gaia
