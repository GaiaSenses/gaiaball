#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <stdint.h>

#include <atomic>

#include "../logic/GaiaConfig.h"
#include "../logic/QuatMath.h"
#include "../vendor/ICM_20948/ICM_20948.h"

namespace gaia {

class ConfigStore;

// One IMU reading as published to consumers. flags use the ImuPacket bits
// (kImuQuatValid / kImuNineAxis / kImuMoving, see Packets.h).
struct ImuSample {
  uint32_t tUs;     // micros() when the FIFO packet was read
  Quat q;           // output orientation (mounting + zero applied, optionally smoothed)
  int16_t acc[3];   // raw DMP accelerometer, last known value
  uint8_t flags;
};

// ICM-20948 DMP reader. A dedicated task (core 1, prio 3) drains the DMP FIFO
// completely every 5 ms (max 16 packets per wake), sanitizes and transforms each
// quaternion and publishes ImuSample to a depth-8 queue (drop-oldest when full).
//
// Threading: the queue is the only cross-task data path for samples. Requests
// (zero, mounting, mode/slerp change) are std::atomic flags consumed by the IMU task.
// Counters/rate are atomics, readable from any task.
class ImuService {
 public:
  static constexpr uint8_t kQueueDepth = 8;
  // Backlog cap before a drain burst. Bytes this old (~40 DMP packets, >150 ms of
  // latency) are stale for a live globe; also catches an overflow should the
  // INT_STATUS_2 check be unavailable. Independent of the exact HW FIFO size.
  static constexpr uint16_t kFifoBacklogResetBytes = 1024;

  // I2C init + up to 10 begin() attempts (500 ms apart) + DMP init. Blocking; call
  // from setup(). Returns imuOk. On failure the service stays usable: the queue
  // exists, startTask() creates an idling task, the rest of the firmware runs.
  bool begin(ConfigStore &store);
  void startTask();

  QueueHandle_t sampleQueue() const { return queue_; }
  bool imuOk() const { return imuOk_; }

  // Callable from any task (e.g. BLE). Executed by the IMU task on its next packet.
  void requestZeroOrientation() { zeroRequested_.store(true); }
  void requestMountingCalibration() { mountRequested_.store(true); }
  // Hands a captured mounting quaternion (w,x,y,z) to the App task for persistence
  // (ConfigStore::setMountQuat + save()). Returns true once per capture; the IMU task
  // never touches ConfigStore after begin(). Safe from any task.
  bool takeCapturedMounting(float out[4]);
  // Picks up orientationMode / slerpAmount / imuNotifyHz / accel filter. Safe from
  // the App task; mode and rate changes are applied by the IMU task on its next
  // cycle (the DMP sample-rate divisor is chosen so the DMP runs at or just above
  // the requested rate; the notify throttle then paces the exact output rate).
  void applyConfig(const GaiaConfig &cfg);

  // FIFO recoveries (resetFIFO) since boot, saturating: hardware overflow
  // (INT_STATUS_2), backlog over kFifoBacklogResetBytes, and packet desync
  // (FIFOIncompleteData leaves the stream mid-packet).
  uint16_t fifoResyncCount() const { return resyncCount_.load(); }
  float measuredHz() const { return measuredHz_.load(); }

 private:
  static void taskEntry(void *arg);
  void taskLoop();
  bool initDmp(uint8_t mode);    // full legacy-like DMP init sequence
  bool switchMode(uint8_t mode); // DMP sensor re-enable on Quat6<->Quat9 change
  bool applySampleRate(uint8_t rateHz);  // gyro/accel divisor from the target rate
  void drainFifo();
  bool checkOverflow();  // true = FIFO was reset, skip this burst
  void resyncFifo();
  void processPacket(const icm_20948_DMP_data_t &d, uint32_t tUs);
  void publish(const ImuSample &s);
  void updateRate(uint32_t nowUs);

  ICM_20948_I2C icm_;
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
  std::atomic<uint16_t> resyncCount_{0};
  std::atomic<float> measuredHz_{0.0f};

  // IMU-task-only state.
  uint8_t activeMode_ = 0;
  uint8_t activeRateHz_ = 50;
  float accFilt_[3] = {0, 0, 0};
  bool accSeeded_ = false;
  bool overflowStatusUsable_ = true;  // INT_STATUS_2 FIFO_OVERFLOW check enabled
  uint8_t overflowStatusStreak_ = 0;
  Quat qMount_{1, 0, 0, 0};
  Quat qZero_{1, 0, 0, 0};
  Quat prevOut_{1, 0, 0, 0};
  bool havePrev_ = false;
  int16_t lastAcc_[3] = {0, 0, 0};
  uint32_t rateWindowStartUs_ = 0;
  uint32_t rateCount_ = 0;
};

}  // namespace gaia
