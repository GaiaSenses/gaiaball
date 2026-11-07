#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>

#include "../logic/Fusion.h"
#include "../logic/GaiaConfig.h"
#include "../logic/QuatMath.h"
#include "Mpu9250.h"

namespace gaia {

class ConfigStore;

// One IMU reading as published to consumers. flags use the ImuPacket bits
// (kImuQuatValid / kImuNineAxis / kImuMoving, see Packets.h).
struct ImuSample {
  uint32_t tUs;     // micros() when the sample was read
  Quat q;           // output orientation (mounting + zero applied, optionally smoothed)
  int16_t acc[3];   // accelerometer counts (±4 g, 8192 LSB/g), optionally EMA-filtered
  uint8_t flags;
};

// GY-91 (MPU-9250) orientation service. A dedicated task (core 1, prio 3) polls the
// data-ready flag over I2C — the module's INT pin is not routed on the board — at a
// period adapted to the configured rate (sample period / 4, clamped to 2..5 ms),
// reads the raw registers (no DMP, no FIFO), corrects the
// gyro bias (GyroBiasTracker: adapts only at rest, killing stand-still yaw drift),
// runs Madgwick fusion (6-axis Quat6, or 9-axis Quat9 with the AK8963 mag) and
// publishes ImuSample to a depth-8 queue (drop-oldest when full).
//
// Threading: the queue is the only cross-task data path for samples. Requests
// (zero, mounting, mode/rate/filters) are std::atomic flags consumed by the IMU task.
// Counters/rate are atomics, readable from any task.
class ImuService {
 public:
  static constexpr uint8_t kQueueDepth = 8;

  // I2C init + up to 10 begin() attempts (500 ms apart). Blocking; call from setup().
  // Returns imuOk. On failure the service stays usable: the queue exists, startTask()
  // creates an idling task, the rest of the firmware runs.
  bool begin(ConfigStore &store);
  void startTask();

  QueueHandle_t sampleQueue() const { return queue_; }
  bool imuOk() const { return imuOk_; }

  // Callable from any task (e.g. BLE). Executed by the IMU task on its next sample.
  void requestZeroOrientation() { zeroRequested_.store(true); }
  void requestMountingCalibration() { mountRequested_.store(true); }
  // Hands a captured mounting quaternion (w,x,y,z) to the App task for persistence
  // (ConfigStore::setMountQuat + save()). Returns true once per capture; the IMU task
  // never touches ConfigStore after begin(). Safe from any task.
  bool takeCapturedMounting(float out[4]);
  // Picks up orientationMode / slerpAmount / imuNotifyHz / accel filter. Safe from
  // the App task; mode and rate changes are applied by the IMU task on its next cycle.
  void applyConfig(const GaiaConfig &cfg);

  // I2C transaction failures since boot (reported in the diag packet's legacy
  // "fifoOverflows" field — same wire slot, new meaning).
  uint16_t readErrorCount() const {
    const uint32_t e = mpu_.errorCount();
    return e > 0xFFFF ? 0xFFFF : (uint16_t)e;
  }
  float measuredHz() const { return measuredHz_.load(); }

 private:
  static void taskEntry(void *arg);
  void taskLoop();
  void processSample(uint32_t tUs);
  void publish(const ImuSample &s);
  void updateRate(uint32_t nowUs);

  Mpu9250 mpu_;
  MadgwickFusion fusion_;
  GyroBiasTracker gyroBias_;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool imuOk_ = false;

  // Requests from other tasks.
  std::atomic<bool> zeroRequested_{false};
  std::atomic<bool> mountRequested_{false};
  std::atomic<uint8_t> desiredMode_{0};
  std::atomic<uint8_t> slerpAmount_{0};
  std::atomic<uint8_t> desiredRateHz_{50};
  // Accel EMA alpha x1000 (0 = off / raw passthrough).
  std::atomic<uint16_t> accelAlphaX1000_{0};

  // Mounting capture handoff (IMU task -> App task). Short critical section, taken
  // only on a capture or a take, never per packet.
  portMUX_TYPE mountMux_ = portMUX_INITIALIZER_UNLOCKED;
  float capturedMount_[4] = {1, 0, 0, 0};
  std::atomic<bool> mountCaptured_{false};

  // Stats (written by the IMU task only).
  std::atomic<float> measuredHz_{0.0f};

  // IMU-task-only state.
  uint8_t activeMode_ = 0;
  uint8_t activeRateHz_ = 50;
  float lastMagBody_[3] = {0, 0, 0};  // AK8963 reading remapped to the chip frame
  uint32_t lastSampleUs_ = 0;
  float accFilt_[3] = {0, 0, 0};
  bool accSeeded_ = false;
  Quat qMount_{1, 0, 0, 0};
  Quat qZero_{1, 0, 0, 0};
  Quat prevOut_{1, 0, 0, 0};
  bool havePrev_ = false;
  uint32_t rateWindowStartUs_ = 0;
  uint32_t rateCount_ = 0;
};

}  // namespace gaia
