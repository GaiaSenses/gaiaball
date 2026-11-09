#include "ImuService.h"

#include <Wire.h>
#include <esp_task_wdt.h>
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
constexpr TickType_t kIdleTicks = pdMS_TO_TICKS(1000);
constexpr float kMovingThresholdDeg = 0.25f;
constexpr uint32_t kRateWindowUs = 1000000;
// dt guard: a missed poll or a wrapped timestamp must not fling the integration.
constexpr float kDtMinS = 0.0005f;
constexpr float kDtMaxS = 0.05f;
// Magnetometer capture wire scale (0.1 µT per LSB, see MagChunkPacket).
constexpr float kMagCaptureLsbPerUt = 10.0f;

uint8_t clampMode(uint8_t mode) { return mode == 1 ? 1 : 0; }
uint8_t clampRate(uint8_t hz) { return hz < 10 ? 10 : (hz > 200 ? 200 : hz); }

// Data-ready poll period for a given sample rate. The GY-91's INT pin is not routed
// on the board (docs/hardware-pinout.md: INT unconnected), so INT_STATUS is polled
// over I2C; a quarter of the sample period guarantees every sample is read (the
// ready bit latches until read) while cutting idle polling traffic vs a fixed 2 ms.
// Clamped to [2, 5] ms: 50 Hz -> 5 ms, 100 Hz -> 2 ms, 200 Hz -> 2 ms.
TickType_t pollTicksForRate(uint8_t hz) {
  uint32_t ms = 250u / (hz ? hz : 1);  // = (1000 / hz) / 4
  if (ms < 2) ms = 2;
  if (ms > 5) ms = 5;
  return pdMS_TO_TICKS(ms);
}

// Loads the persisted mounting quaternion; falls back to identity if unusable.
Quat loadMount(const GaiaConfig &cfg) {
  const float *m = cfg.mountQuat;
  const float n2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[3] * m[3];
  if (!isfinite(n2) || n2 < 0.5f || n2 > 1.5f) return Quat{1, 0, 0, 0};
  const float inv = 1.0f / sqrtf(n2);
  return Quat{m[0] * inv, m[1] * inv, m[2] * inv, m[3] * inv};
}

uint8_t degToByte(float deg) {
  if (!isfinite(deg) || deg <= 0.0f) return 0;
  return deg >= 255.0f ? 255 : (uint8_t)lroundf(deg);
}

int16_t dpsToX100(float dps) {
  if (!isfinite(dps)) return 0;
  const float v = dps * 100.0f;
  if (v >= 32767.0f) return 32767;
  if (v <= -32768.0f) return -32768;
  return (int16_t)lroundf(v);
}
}  // namespace

ImuSettings imuSettingsFrom(const GaiaConfig &cfg) {
  ImuSettings s;
  s.orientationMode = clampMode(cfg.orientationMode);
  s.rateHz = clampRate(cfg.imuNotifyHz);
  s.dlpfMode = cfg.imuDlpfMode > 7 ? 0 : cfg.imuDlpfMode;
  s.gyroFsSel = cfg.gyroFsSel >= Mpu9250::kGyroFsCount ? 2 : cfg.gyroFsSel;
  s.accelAlphaX1000 = cfg.accelLpAlphaX1000 > 1000 ? 1000 : cfg.accelLpAlphaX1000;
  s.accelHampelK = (float)cfg.accelHampelKX10 / 10.0f;

  s.ahrs.gain = (float)cfg.fusionGainX100 / 100.0f;
  s.ahrs.sampleRateHz = (float)s.rateHz;
  // The AHRS needs the range to know when the gyroscope has clipped; the driver owns
  // the mapping from FS_SEL to dps.
  s.ahrs.gyroRangeDps = 0.0f;  // filled in by the IMU task from the live driver
  s.ahrs.accelRejectionDeg = (float)cfg.accelRejectionDeg;
  s.ahrs.magRejectionDeg = (float)cfg.magRejectionDeg;
  s.ahrs.rejectionTimeoutS = (float)cfg.rejectionTimeoutS;

  s.bias.restThresholdDps = (float)cfg.biasRestThDpsX100 / 100.0f;
  s.bias.maxBiasDps = (float)cfg.biasMaxDpsX100 / 100.0f;
  s.bias.tauS = (float)cfg.biasTauS;

  s.smoothMode = cfg.quatSmoothMode == 2
                     ? QuatSmoothMode::OneEuro
                     : (cfg.quatSmoothMode == 1 ? QuatSmoothMode::SlerpFixed
                                                : QuatSmoothMode::Off);
  s.slerpAmount = cfg.slerpAmount > 100 ? 100 : cfg.slerpAmount;
  s.oneEuroFcMinHz = (float)cfg.oneEuroFcMinX100 / 100.0f;
  s.oneEuroBeta = (float)cfg.oneEuroBetaX1000 / 1000.0f;
  s.magCal = magCalFromFixed(cfg.magHardIron, cfg.magSoftIron);
  return s;
}

bool ImuService::begin(ConfigStore &store) {
  const GaiaConfig &cfg = store.get();
  qMount_ = loadMount(cfg);
  for (int i = 0; i < 3; i++) gyroBiasX100_[i].store(0);
  applyConfig(cfg);

  if (queue_ == nullptr) queue_ = xQueueCreate(kQueueDepth, sizeof(ImuSample));
  if (queue_ == nullptr) GB_LOGE(kTag, "sample queue allocation failed");
  if (magQueue_ == nullptr) magQueue_ = xQueueCreate(kMagQueueDepth, sizeof(MagRawSample));

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

  // Front-end first (range + rate + anti-alias), then the pipeline settings that
  // depend on it (the AHRS overrange threshold).
  applyPendingSettings(true);
  imuOk_ = frontEndOk_;
  if (imuOk_) {
    GB_LOGI(kTag, "fusion ready (%s, %u Hz, DLPF %.0f Hz, gain %.2f)",
            active_.orientationMode == 1 ? "Quat9" : "Quat6", (unsigned)active_.rateHz,
            mpu_.dlpfCutoffHz(), active_.ahrs.gain);
  } else {
    GB_LOGE(kTag, "sample-rate setup failed; IMU disabled");
  }
  return imuOk_;
}

void ImuService::applyConfig(const GaiaConfig &cfg) {
  const ImuSettings next = imuSettingsFrom(cfg);
  portENTER_CRITICAL(&settingsMux_);
  pending_ = next;
  portEXIT_CRITICAL(&settingsMux_);
  settingsDirty_.store(true);
}

void ImuService::startMagCapture() {
  magCaptureRequested_.store(true);
  if (magQueue_ != nullptr) xQueueReset(magQueue_);
}

// Picks up the pending settings and reprograms whatever actually changed. `force`
// applies everything (used once from begin()).
void ImuService::applyPendingSettings(bool force) {
  if (!settingsDirty_.exchange(false) && !force) return;
  ImuSettings next;
  portENTER_CRITICAL(&settingsMux_);
  next = pending_;
  portEXIT_CRITICAL(&settingsMux_);

  const bool modeChanged = force || next.orientationMode != active_.orientationMode;
  const bool frontEndChanged = force || next.rateHz != active_.rateHz ||
                               next.dlpfMode != active_.dlpfMode ||
                               next.gyroFsSel != active_.gyroFsSel;

  if (frontEndChanged) {
    bool ok = mpu_.setGyroFullScale(next.gyroFsSel);
    if (!ok) GB_LOGE(kTag, "gyro range change failed");
    const bool rateOk = mpu_.setSampleRateHz(next.rateHz, next.dlpfMode);
    if (!rateOk) GB_LOGE(kTag, "sample rate change to %u Hz failed", (unsigned)next.rateHz);
    frontEndOk_ = ok && rateOk;
  }
  // The overrange threshold is a property of the hardware range, not of the config.
  next.ahrs.gyroRangeDps = mpu_.gyroRangeDps();

  ahrs_.configure(next.ahrs);
  gyroBias_.configure(next.bias, (float)next.rateHz);
  smoother_.configure(next.smoothMode, next.slerpAmount, next.oneEuroFcMinHz,
                      next.oneEuroBeta);
  for (int i = 0; i < 3; i++) accelSpike_[i].setK(next.accelHampelK);
  if (next.accelAlphaX1000 == 0) accSeeded_ = false;

  if (modeChanged && !force) {
    // The reference frame changes (Quat6 yaw is arbitrary, Quat9 is magnetic north):
    // restart the fusion and drop the old zero.
    ahrs_.restart();
    gyroBias_.reset();
    qZero_ = Quat{1, 0, 0, 0};
    smoother_.reset();
    havePrev_ = false;
    GB_LOGI(kTag, "mode -> %s", next.orientationMode == 1 ? "Quat9" : "Quat6");
  }
  active_ = next;
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
  // Task WDT: registered and fed once per cycle, idle path included (see the
  // watchdog policy in Diagnostics.h).
  esp_task_wdt_add(nullptr);
  if (!imuOk_ || queue_ == nullptr) {
    // IMU unavailable: idle forever so the rest of the system keeps running.
    for (;;) {
      esp_task_wdt_reset();
      vTaskDelay(kIdleTicks);
    }
  }
  rateWindowStartUs_ = micros();
  lastSampleUs_ = micros();
  TickType_t pollTicks = pollTicksForRate(active_.rateHz);
  for (;;) {
    esp_task_wdt_reset();
    const uint8_t rateBefore = active_.rateHz;
    applyPendingSettings(false);
    if (active_.rateHz != rateBefore) pollTicks = pollTicksForRate(active_.rateHz);

    // Magnetometer capture window (one-shot, with a hard time limit).
    const bool wantCapture = magCaptureRequested_.load();
    if (wantCapture && !magCaptureActive_.load()) {
      magCaptureActive_.store(true);
      magCaptureStartMs_ = millis();
      GB_LOGI(kTag, "magnetometer capture started");
    } else if (magCaptureActive_.load()) {
      const bool expired = (uint32_t)(millis() - magCaptureStartMs_) >= kMagCaptureMaxMs;
      if (!wantCapture || expired) {
        magCaptureActive_.store(false);
        magCaptureRequested_.store(false);
        GB_LOGI(kTag, "magnetometer capture stopped%s", expired ? " (timeout)" : "");
      }
    }

    if (mpu_.dataReady()) processSample(micros());
    updateRate(micros());
    vTaskDelay(pollTicks);
  }
}

// Spike filter then EMA on the PUBLISHED accelerometer. Order matters: the Hampel step
// must see the raw stream, because an EMA would already have smeared a spike across
// the following samples and made it undetectable.
void ImuService::filterAccel(const int16_t raw[3], int16_t out[3]) {
  int16_t deSpiked[3];
  for (int i = 0; i < 3; i++)
    deSpiked[i] = active_.accelHampelK > 0.0f ? accelSpike_[i].update(raw[i]) : raw[i];

  if (active_.accelAlphaX1000 == 0) {
    for (int i = 0; i < 3; i++) out[i] = deSpiked[i];
    accSeeded_ = false;
    return;
  }
  const float a = (float)active_.accelAlphaX1000 / 1000.0f;
  for (int i = 0; i < 3; i++) {
    if (!accSeeded_)
      accFilt_[i] = (float)deSpiked[i];
    else
      accFilt_[i] += a * ((float)deSpiked[i] - accFilt_[i]);
    out[i] = (int16_t)lroundf(accFilt_[i]);
  }
  accSeeded_ = true;
}

void ImuService::publishMagSample(const float rawUt[3]) {
  if (magQueue_ == nullptr) return;
  MagRawSample s;
  for (int i = 0; i < 3; i++) {
    const float v = rawUt[i] * kMagCaptureLsbPerUt;
    s.xyz[i] = v >= 32767.0f ? 32767 : (v <= -32768.0f ? -32768 : (int16_t)lroundf(v));
  }
  if (xQueueSend(magQueue_, &s, 0) == pdTRUE) return;
  MagRawSample dropped;  // drop-oldest: a stalled reader must not stop the capture
  xQueueReceive(magQueue_, &dropped, 0);
  xQueueSend(magQueue_, &s, 0);
}

void ImuService::processSample(uint32_t tUs) {
  int16_t accRaw[3], gyroRaw[3];
  if (!mpu_.readAccelGyro(accRaw, gyroRaw)) return;

  // --- physical units ------------------------------------------------------
  const float accelG[3] = {
      (float)accRaw[0] / Mpu9250::kAccelLsbPerG,
      (float)accRaw[1] / Mpu9250::kAccelLsbPerG,
      (float)accRaw[2] / Mpu9250::kAccelLsbPerG,
  };
  const float lsbPerDps = mpu_.gyroLsbPerDps();
  float gDps[3];
  for (int i = 0; i < 3; i++) gDps[i] = (float)gyroRaw[i] / lsbPerDps;

  // --- stage 2: gyroscope zero-rate offset ---------------------------------
  gyroBias_.correct(gDps, accelG, tUs / 1000u);

  float dt = (float)(uint32_t)(tUs - lastSampleUs_) * 1e-6f;
  lastSampleUs_ = tUs;
  if (dt < kDtMinS || dt > kDtMaxS) dt = 1.0f / (float)active_.rateHz;

  // --- stage 3: magnetometer ------------------------------------------------
  const bool nine = active_.orientationMode == 1;
  if (nine) {
    float uT[3];
    if (mpu_.readMag(uT)) {
      // AK8963 axes differ from the gyro/accel chip frame: X<->Y swapped, Z inverted.
      const float chipFrame[3] = {uT[1], uT[0], -uT[2]};
      if (magCaptureActive_.load()) publishMagSample(chipFrame);  // RAW, for the fit
      magApply(active_.magCal, chipFrame, lastMagBody_);
      haveMag_ = true;
    }
  }

  // --- stage 4: fusion ------------------------------------------------------
  if (nine && haveMag_)
    ahrs_.update9(gDps, accelG, lastMagBody_, dt);
  else
    ahrs_.update6(gDps, accelG, dt);
  ++rateCount_;

  ImuSample s;
  s.tUs = tUs;
  filterAccel(accRaw, s.acc);
  s.flags = nine ? kImuNineAxis : 0;

  const Quat raw = ahrs_.quat();
  if (!isfinite(raw.w) || !isfinite(raw.x) || !isfinite(raw.y) || !isfinite(raw.z)) {
    // Numeric blow-up (should not happen; the AHRS renormalizes every update): restart
    // the filter and publish the last good output with quatValid=0.
    ahrs_.restart();
    s.q = prevOut_;
    publish(s);
    return;
  }

  // --- stage 5: mounting and zero ------------------------------------------
  if (mountRequested_.exchange(false)) {
    qMount_ = raw;
    qZero_ = Quat{1, 0, 0, 0};  // the old zero lived in the previous mounting frame
    havePrev_ = false;
    smoother_.reset();
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
    smoother_.reset();
    GB_LOGI(kTag, "orientation zeroed");
  }
  Quat q = quatMultiply(quatConjugate(qZero_), mounted);

  // --- stage 6: output smoothing -------------------------------------------
  q = smoother_.update(q, dt);

  s.flags |= kImuQuatValid;
  if (havePrev_ && quatAngleDeg(prevOut_, q) > kMovingThresholdDeg) s.flags |= kImuMoving;
  s.q = q;
  prevOut_ = q;
  havePrev_ = true;
  publish(s);
  updateDiagnostics();
}

void ImuService::updateDiagnostics() {
  uint8_t f = 0;
  if (ahrs_.startup()) f |= kAhrsStartup;
  if (ahrs_.overrangeRecovery()) f |= kAhrsOverrange;
  if (ahrs_.accelIgnored()) f |= kAhrsAccelIgnored;
  if (ahrs_.magIgnored()) f |= kAhrsMagIgnored;
  if (gyroBias_.atRest()) f |= kAhrsBiasAtRest;
  if (gyroBias_.adapting()) f |= kAhrsBiasAdapting;
  if (!active_.magCal.isIdentity()) f |= kAhrsMagCalibrated;
  if (magCaptureActive_.load()) f |= kAhrsMagCapture;
  ahrsFlags_.store(f);
  accelErrDeg_.store(degToByte(ahrs_.accelErrorDeg()));
  magErrDeg_.store(degToByte(ahrs_.magErrorDeg()));
  const float *b = gyroBias_.bias();
  for (int i = 0; i < 3; i++) gyroBiasX100_[i].store(dpsToX100(b[i]));
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
