#pragma once
#include <stdint.h>

#include "../logic/Packets.h"
#include "AudioService.h"
#include "Co2Service.h"
#include "ImuService.h"
#include "LedController.h"

namespace gaia {

// Health counters + task watchdog.
//
// Watchdog policy: only the App task registers (watchTask()) and feeds (feed()) the
// 10 s task WDT. The IMU task is monitored indirectly: App consumes its sample queue,
// so a stalled IMU task starves App and trips the WDT. Not a task; call from App.
class Diagnostics {
 public:
  // esp_task_wdt_init(10 s, panic=true). Call once at boot.
  void begin();
  // Registers the CALLING task with the WDT (call once from the App task).
  void watchTask();
  // Resets the WDT for the calling task (App task, every loop).
  void feed();

  DiagPacket collect(uint16_t seq, uint16_t notifyHzX10, const ImuService &imu,
                     const AudioService &audio, const Co2Service &co2, uint32_t nowMs) const;

  // Error LED source is ON when the IMU or audio is not ok. CO2 staleness is NOT an
  // error: it is the expected state when the sensor is absent.
  void driveErrorLed(LedController &led, const ImuService &imu, const AudioService &audio) const;
};

}  // namespace gaia
