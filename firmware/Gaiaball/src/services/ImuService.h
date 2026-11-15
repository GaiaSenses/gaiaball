#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>

#include "../logic/Ahrs.h"
#include "../logic/Filters.h"
#include "../logic/GaiaConfig.h"
#include "../logic/GyroBias.h"
#include "../logic/ImuFrontEnd.h"
#include "../logic/MagAxisRemap.h"
#include "../logic/MagCalibration.h"
#include "../logic/QuatMath.h"
#include "../logic/QuatSmoother.h"
#include "Icm20948.h"
#include "ImuDriver.h"
#include "Mpu9250.h"

namespace gaia {

class ConfigStore;

// One IMU reading as published to consumers. flags use the ImuPacket bits
// (kImuQuatValid / kImuNineAxis / kImuMoving, see Packets.h).
struct ImuSample {
  uint32_t tUs;    // micros() when the sample was read
  Quat q;          // output orientation (mounting + zero applied, optionally smoothed)
  int16_t acc[3];  // accelerometer counts (±4 g, 8192 LSB/g), optionally filtered
  uint8_t flags;
  // Reference epoch: changes whenever the frame q is expressed in changes (zero
  // applied, mounting captured, Quat6/Quat9 switch), so a consumer that integrates
  // over q (globe navigation) knows to restart instead of reading a jump as motion.
  uint32_t zeroEpoch;
};

// One raw magnetometer triplet for a calibration run, in 0.1 µT, already remapped to
// the gyro/accel chip frame and BEFORE any hard/soft-iron correction.
struct MagRawSample {
  int16_t xyz[3];
};

// Everything the IMU task needs from the configuration, as one plain struct.
//
// WHY a struct instead of one atomic per field: the pipeline now has four stages with
// their own parameters. Handing them over together (one short critical section, picked
// up at the top of a task cycle) means a config write can never leave the task running
// half of the old setup and half of the new one.
struct ImuSettings {
  uint8_t orientationMode = 0;  // 0 = Quat6 (relative), 1 = Quat9 (magnetic north)
  uint8_t rateHz = 50;
  uint8_t dlpfMode = 0;  // 0 = auto
  uint8_t gyroFsSel = 2;
  uint16_t accelAlphaX1000 = 0;  // 0 = no EMA on the published accelerometer
  float accelHampelK = 0.0f;     // 0 = no spike filter
  AhrsSettings ahrs{};
  GyroBiasSettings bias{};
  QuatSmoothMode smoothMode = QuatSmoothMode::Off;
  uint8_t slerpAmount = 0;
  float oneEuroFcMinHz = 1.0f;
  float oneEuroBeta = 0.0f;
  MagCal magCal = magCalIdentity();
  // Index into the 24 proper axis permutations (logic/MagAxisRemap.h): how the
  // magnetometer's own frame maps onto the gyro/accel frame. Configurable because the
  // relationship is a property of the board, different per chip, and unknowable without
  // the hardware in hand.
  uint8_t magAxisRemap = kMagRemapDefault;
};

// THE one place that translates stored configuration into pipeline settings (units,
// fixed-point scaling, derived values). Pure function: host-testable, no I/O.
ImuSettings imuSettingsFrom(const GaiaConfig &cfg);

// Orientation service, driving whichever IMU the configuration selects (ImuDriver:
// ICM-20948 or MPU-9250; none at all is a legitimate state, see begin()).
//
// A dedicated task (core 1, prio 3) polls the data-ready flag over I2C — the module's
// INT pin is not routed on the board — at a period adapted to the configured rate
// (sample period / 4, clamped to 2..5 ms), reads the raw registers (no DMP, no FIFO)
// and runs this pipeline, in this order and for these reasons:
//
//   1. anti-alias     the chip's DLPF matched to the output rate (ImuDriver), so the
//                     sample-rate decimation cannot fold vibration into the band.
//   2. gyro bias      GyroBiasTracker: removes the zero-rate offset that would
//                     otherwise integrate into unbounded heading drift.
//   3. mag frame + cal applyMagRemap puts the magnetometer in the gyro/accel frame,
//                     then MagCal: hard/soft-iron correction, without which the 9-axis
//                     heading can be worse than the magnetometer-free estimate.
//   4. fusion         Ahrs (revised Madgwick): gyro integration corrected by the
//                     accelerometer/magnetometer, with acceleration and magnetic
//                     rejection so a thrown ball does not tilt and a nearby magnet
//                     does not swing the heading.
//   5. frame          mounting quaternion then zero: puts the output in the ball's
//                     own reference frame (chip/board tilt absorbed).
//   6. smoothing      QuatSmoother: off, legacy fixed slerp, or the 1-euro filter
//                     that only smooths while the ball is still.
//
// The published accelerometer is a separate, parallel path (Hampel spike filter then
// EMA): it is a signal for the patch, never an input to the fusion, which always uses
// the unfiltered reading.
//
// Threading: the sample queue is the only cross-task data path for samples (the
// reference epoch travels inside each sample, never as shared state). Settings
// arrive as one struct under a short critical section; one-shot requests (zero,
// mounting, magnetometer capture) are atomics consumed by the IMU task. Counters and
// diagnostics are atomics, readable from any task.
class ImuService {
 public:
  static constexpr uint8_t kQueueDepth = 8;
  static constexpr uint8_t kMagQueueDepth = 32;
  // A capture left running would stream for ever; the config-app needs ~30 s.
  static constexpr uint32_t kMagCaptureMaxMs = 180000;

  // Reads cfg.imuChip ONCE, points imu_ at that driver, then I2C init + up to 10
  // begin() attempts (500 ms apart). Blocking; call from setup(). Returns imuOk.
  // ImuChip::None (a factory-fresh ball) never touches I2C and is NOT an error:
  // BLE, audio and CO2 must come up normally with the IMU simply off. On any failure
  // the service stays usable: the queue exists, startTask() creates an idling task,
  // the rest of the firmware runs.
  bool begin(ConfigStore &store);
  void startTask();

  // The selection in force since boot. A configuration change only takes effect on the
  // next boot, by design: the app warns the operator and offers a reboot. Feeds
  // imuStatusByte() together with imuOk(), i.e. the read-only TLV 0x35.
  ImuChip bootChip() const { return bootChip_; }

  QueueHandle_t sampleQueue() const { return queue_; }
  QueueHandle_t magQueue() const { return magQueue_; }
  bool imuOk() const { return imuOk_; }
  // True when the selected driver found its magnetometer. Null-safe (no IMU selected).
  bool magFound() const { return imu_ != nullptr && imu_->magOk(); }

  // Callable from any task (e.g. BLE). Executed by the IMU task on its next sample.
  void requestZeroOrientation() { zeroRequested_.store(true); }
  void requestMountingCalibration() { mountRequested_.store(true); }
  // Streams raw magnetometer samples to magQueue() so the config-app can fit the
  // hard/soft-iron calibration. Idempotent; auto-stops after kMagCaptureMaxMs.
  void startMagCapture();
  void stopMagCapture() { magCaptureRequested_.store(false); }
  bool magCapturing() const { return magCaptureActive_.load(); }

  // Hands a captured mounting quaternion (w,x,y,z) to the App task for persistence
  // (ConfigStore::setMountQuat + save()). Returns true once per capture; the IMU task
  // never touches ConfigStore after begin(). Safe from any task.
  bool takeCapturedMounting(float out[4]);
  // Picks up every pipeline parameter. Safe from the App task; applied by the IMU
  // task on its next cycle.
  void applyConfig(const GaiaConfig &cfg);

  // I2C transaction failures since boot (reported in the diag packet's legacy
  // "fifoOverflows" field — same wire slot, new meaning).
  // Called about once a second by the diagnostics path from boot, i.e. possibly
  // before/without any driver: no driver means no I2C traffic, hence no errors.
  uint16_t readErrorCount() const {
    if (imu_ == nullptr) return 0;
    const uint32_t e = imu_->errorCount();
    return e > 0xFFFF ? 0xFFFF : (uint16_t)e;
  }
  float measuredHz() const { return measuredHz_.load(); }

  // --- pipeline state for the diagnostics packet (see kAhrs* in Packets.h) ---
  uint8_t ahrsFlags() const { return ahrsFlags_.load(); }
  uint8_t accelErrDeg() const { return accelErrDeg_.load(); }
  uint8_t magErrDeg() const { return magErrDeg_.load(); }
  // Estimated gyroscope zero-rate offset in 0.01 dps per LSB.
  void gyroBiasX100(int16_t out[3]) const {
    for (int i = 0; i < 3; i++) out[i] = gyroBiasX100_[i].load();
  }

 private:
  static void taskEntry(void *arg);
  void taskLoop();
  void applyPendingSettings(bool force);
  void processSample(uint32_t tUs);
  // Stamps the current reference epoch on s, then queues it.
  void publish(ImuSample &s);
  void publishMagSample(const float rawUt[3]);
  void updateRate(uint32_t nowUs);
  void updateDiagnostics();
  // Published-accelerometer path (spike filter then EMA), counts in and out.
  void filterAccel(const int16_t raw[3], int16_t out[3]);

  // The drivers are concrete members (no dynamic allocation anywhere in the firmware)
  // and imu_ points at the one the configuration selected - or at nothing, which is why
  // every use of imu_ is null-checked.
  Mpu9250 mpu_;
  Icm20948 icm_;
  ImuDriver *imu_ = nullptr;
  ImuChip bootChip_ = ImuChip::None;
  Ahrs ahrs_;
  GyroBiasTracker gyroBias_;
  QuatSmoother smoother_;
  Hampel5<int16_t> accelSpike_[3];
  QueueHandle_t queue_ = nullptr;
  QueueHandle_t magQueue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool imuOk_ = false;
  // Last front-end programming result (range + rate + DLPF): what begin() reports.
  bool frontEndOk_ = false;

  // Requests from other tasks.
  std::atomic<bool> zeroRequested_{false};
  std::atomic<bool> mountRequested_{false};
  std::atomic<bool> magCaptureRequested_{false};
  std::atomic<bool> magCaptureActive_{false};

  // Settings handover (App task -> IMU task). Short critical section, taken only on a
  // config change and on the task's pickup, never per sample.
  portMUX_TYPE settingsMux_ = portMUX_INITIALIZER_UNLOCKED;
  ImuSettings pending_{};
  std::atomic<bool> settingsDirty_{false};

  // Mounting capture handoff (IMU task -> App task).
  portMUX_TYPE mountMux_ = portMUX_INITIALIZER_UNLOCKED;
  float capturedMount_[4] = {1, 0, 0, 0};
  std::atomic<bool> mountCaptured_{false};

  // Stats and pipeline state (written by the IMU task only).
  std::atomic<float> measuredHz_{0.0f};
  std::atomic<uint8_t> ahrsFlags_{0};
  std::atomic<uint8_t> accelErrDeg_{0};
  std::atomic<uint8_t> magErrDeg_{0};
  std::atomic<int16_t> gyroBiasX100_[3];

  // IMU-task-only state.
  ImuSettings active_{};
  float lastMagBody_[3] = {0, 0, 0};  // magnetometer reading in the gyro/accel frame
  bool haveMag_ = false;
  uint32_t lastSampleUs_ = 0;
  uint32_t magCaptureStartMs_ = 0;
  float accFilt_[3] = {0, 0, 0};
  bool accSeeded_ = false;
  Quat qMount_{1, 0, 0, 0};
  Quat qZero_{1, 0, 0, 0};
  uint32_t zeroEpoch_ = 0;  // bumped on every reference change; see ImuSample::zeroEpoch
  Quat prevOut_{1, 0, 0, 0};
  bool havePrev_ = false;
  uint32_t rateWindowStartUs_ = 0;
  uint32_t rateCount_ = 0;
};

}  // namespace gaia
