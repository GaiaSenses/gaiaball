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
  // Picks up orientationMode / slerpAmount. Safe from the App task; a mode change
  // re-enables the DMP sensors on the IMU task's next cycle.
  void applyConfig(const GaiaConfig &cfg);

  uint16_t fifoOverflowCount() const { return overflowCount_.load(); }
  float measuredHz() const { return measuredHz_.load(); }

 private:
  static void taskEntry(void *arg);
  void taskLoop();
  bool initDmp(uint8_t mode);    // full legacy-like DMP init sequence
  bool switchMode(uint8_t mode); // DMP sensor re-enable on Quat6<->Quat9 change
  void drainFifo();
  void processPacket(const icm_20948_DMP_data_t &d, uint32_t tUs);
  void publish(const ImuSample &s);
  void updateRate(uint32_t nowUs);

  ICM_20948_I2C icm_;
  ConfigStore *store_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  TaskHandle_t task_ = nullptr;
  bool imuOk_ = false;

  // Requests from other tasks.
  std::atomic<bool> zeroRequested_{false};
  std::atomic<bool> mountRequested_{false};
  std::atomic<uint8_t> desiredMode_{0};
  std::atomic<uint8_t> slerpAmount_{0};

  // Stats (written by the IMU task only).
  std::atomic<uint16_t> overflowCount_{0};
  std::atomic<float> measuredHz_{0.0f};

  // IMU-task-only state.
  uint8_t activeMode_ = 0;
  Quat qMount_{1, 0, 0, 0};
  Quat qZero_{1, 0, 0, 0};
  Quat prevOut_{1, 0, 0, 0};
  bool havePrev_ = false;
  int16_t lastAcc_[3] = {0, 0, 0};
  uint32_t rateWindowStartUs_ = 0;
  uint32_t rateCount_ = 0;
};

}  // namespace gaia
