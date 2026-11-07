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
constexpr bool kAd0Val = false;  // AD0/SDO low -> 0x68, as the legacy firmware
constexpr int kBeginAttempts = 10;
constexpr uint32_t kBeginRetryMs = 500;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(2);  // >= 2x the fastest sample rate
constexpr TickType_t kIdleTicks = pdMS_TO_TICKS(1000);
constexpr float kMovingThresholdDeg = 0.25f;
constexpr uint32_t kRateWindowUs = 1000000;
constexpr float kDeg2Rad = (float)M_PI / 180.0f;
constexpr float kMadgwickBeta = 0.1f;
// dt guard: a missed poll or a wrapped timestamp must not fling the integration.
constexpr float kDtMinS = 0.0005f;
constexpr float kDtMaxS = 0.05f;

uint8_t clampMode(uint8_t mode) { return mode == 1 ? 1 : 0; }
uint8_t clampRate(uint8_t hz) { return hz < 10 ? 10 : (hz > 200 ? 200 : hz); }

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
  const GaiaConfig &cfg = store.get();
  qMount_ = loadMount(cfg);
  activeMode_ = clampMode(cfg.orientationMode);
  desiredMode_.store(activeMode_);
  slerpAmount_.store(cfg.slerpAmount > 100 ? 100 : cfg.slerpAmount);
  activeRateHz_ = clampRate(cfg.imuNotifyHz);
  desiredRateHz_.store(activeRateHz_);
  accelAlphaX1000_.store(cfg.accelLpAlphaX1000 > 1000 ? 1000 : cfg.accelLpAlphaX1000);
  fusion_.configure(kMadgwickBeta);

  if (queue_ == nullptr) queue_ = xQueueCreate(kQueueDepth, sizeof(ImuSample));
  if (queue_ == nullptr) GB_LOGE(kTag, "sample queue allocation failed");

  Wire.begin(kSdaPin, kSclPin);
  Wire.setClock(kI2cHz);

  bool found = false;
  for (int attempt = 1; attempt <= kBeginAttempts; ++attempt) {
    if (mpu_.begin(Wire, kAd0Val)) {
      found = true;
      break;
    }
    GB_LOGW(kTag, "begin attempt %d/%d failed", attempt, kBeginAttempts);
    if (attempt < kBeginAttempts) delay(kBeginRetryMs);  // setup-time only
  }
  if (!found) {
    GB_LOGE(kTag, "MPU-9250 (GY-91) not found; IMU disabled");
    imuOk_ = false;
    return false;
  }

  imuOk_ = mpu_.setSampleRateHz(activeRateHz_);
  if (imuOk_) {
    GB_LOGI(kTag, "Madgwick fusion ready (%s, %u Hz)", activeMode_ == 1 ? "Quat9" : "Quat6",
            (unsigned)activeRateHz_);
  } else {
    GB_LOGE(kTag, "sample-rate setup failed; IMU disabled");
  }
  return imuOk_;
}

void ImuService::applyConfig(const GaiaConfig &cfg) {
  desiredMode_.store(clampMode(cfg.orientationMode));
  slerpAmount_.store(cfg.slerpAmount > 100 ? 100 : cfg.slerpAmount);
  desiredRateHz_.store(clampRate(cfg.imuNotifyHz));
  accelAlphaX1000_.store(cfg.accelLpAlphaX1000 > 1000 ? 1000 : cfg.accelLpAlphaX1000);
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
  lastSampleUs_ = micros();
  for (;;) {
    const uint8_t wantedMode = desiredMode_.load();
    if (wantedMode != activeMode_) {
      activeMode_ = wantedMode;
      // The reference frame changes (Quat6 yaw is arbitrary, Quat9 is magnetic
      // north): restart the fusion and drop the old zero.
      fusion_.reset();
      qZero_ = Quat{1, 0, 0, 0};
      havePrev_ = false;
      GB_LOGI(kTag, "mode -> %s", activeMode_ == 1 ? "Quat9" : "Quat6");
    }
    const uint8_t wantedRate = desiredRateHz_.load();
    if (wantedRate != activeRateHz_) {
      if (mpu_.setSampleRateHz(wantedRate))
        GB_LOGI(kTag, "sample rate -> %u Hz", (unsigned)wantedRate);
      else
        GB_LOGE(kTag, "sample rate change to %u Hz failed", (unsigned)wantedRate);
      activeRateHz_ = wantedRate;
    }
    if (mpu_.dataReady()) processSample(micros());
    updateRate(micros());
    vTaskDelay(kPollTicks);
  }
}

void ImuService::processSample(uint32_t tUs) {
  int16_t accRaw[3], gyroRaw[3];
  if (!mpu_.readAccelGyro(accRaw, gyroRaw)) return;

  // Accel for the wire: raw counts, optionally EMA-filtered (TLV 0x04).
  int16_t accOut[3];
  const uint16_t aX1000 = accelAlphaX1000_.load();
  if (aX1000 == 0) {
    for (int i = 0; i < 3; i++) accOut[i] = accRaw[i];
    accSeeded_ = false;
  } else {
    const float a = (float)aX1000 / 1000.0f;
    for (int i = 0; i < 3; i++) {
      if (!accSeeded_) accFilt_[i] = (float)accRaw[i];
      else accFilt_[i] += a * ((float)accRaw[i] - accFilt_[i]);
      accOut[i] = (int16_t)lroundf(accFilt_[i]);
    }
    accSeeded_ = true;
  }

  // Physical units for the fusion.
  float gDps[3];
  for (int i = 0; i < 3; i++) gDps[i] = (float)gyroRaw[i] / Mpu9250::kGyroLsbPerDps;
  gyroBias_.correct(gDps, tUs / 1000u);
  const float gx = gDps[0] * kDeg2Rad, gy = gDps[1] * kDeg2Rad, gz = gDps[2] * kDeg2Rad;
  const float ax = (float)accRaw[0] / Mpu9250::kAccelLsbPerG;
  const float ay = (float)accRaw[1] / Mpu9250::kAccelLsbPerG;
  const float az = (float)accRaw[2] / Mpu9250::kAccelLsbPerG;

  float dt = (float)(uint32_t)(tUs - lastSampleUs_) * 1e-6f;
  lastSampleUs_ = tUs;
  if (dt < kDtMinS || dt > kDtMaxS) dt = 1.0f / (float)activeRateHz_;

  const bool nine = activeMode_ == 1;
  if (nine) {
    float uT[3];
    if (mpu_.readMag(uT)) {
      // AK8963 axes differ from the gyro/accel chip frame: X<->Y swapped, Z inverted.
      lastMagBody_[0] = uT[1];
      lastMagBody_[1] = uT[0];
      lastMagBody_[2] = -uT[2];
    }
    fusion_.update9(gx, gy, gz, ax, ay, az, lastMagBody_[0], lastMagBody_[1],
                    lastMagBody_[2], dt);
  } else {
    fusion_.update6(gx, gy, gz, ax, ay, az, dt);
  }
  ++rateCount_;

  ImuSample s;
  s.tUs = tUs;
  s.acc[0] = accOut[0];
  s.acc[1] = accOut[1];
  s.acc[2] = accOut[2];
  s.flags = nine ? kImuNineAxis : 0;

  const Quat raw = fusion_.quat();
  if (!isfinite(raw.w) || !isfinite(raw.x) || !isfinite(raw.y) || !isfinite(raw.z)) {
    // Numeric blow-up (should not happen; fusion renormalizes): restart the filter
    // and publish the last good output with quatValid=0.
    fusion_.reset();
    s.q = prevOut_;
    publish(s);
    return;
  }

  if (mountRequested_.exchange(false)) {
    qMount_ = raw;
    qZero_ = Quat{1, 0, 0, 0};  // the old zero lived in the previous mounting frame
    havePrev_ = false;
    portENTER_CRITICAL(&mountMux_);
    capturedMount_[0] = raw.w;
    capturedMount_[1] = raw.x;
    capturedMount_[2] = raw.y;
    capturedMount_[3] = raw.z;
    mountCaptured_.store(true);
    portEXIT_CRITICAL(&mountMux_);
    GB_LOGI(kTag, "mounting calibrated (pending persistence by app task)");
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

bool ImuService::takeCapturedMounting(float out[4]) {
  if (!mountCaptured_.load()) return false;
  portENTER_CRITICAL(&mountMux_);
  const bool had = mountCaptured_.exchange(false);
  if (had) {
    for (int i = 0; i < 4; ++i) out[i] = capturedMount_[i];
  }
  portEXIT_CRITICAL(&mountMux_);
  return had;
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
  measuredHz_.store((float)rateCount_ * 1000000.0f / (float)elapsed);
  rateCount_ = 0;
  rateWindowStartUs_ = nowUs;
}

}  // namespace gaia
