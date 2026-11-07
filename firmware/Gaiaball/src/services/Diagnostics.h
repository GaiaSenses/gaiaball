#pragma once
#include <stdint.h>

#include "../logic/Packets.h"
#include "AudioService.h"
#include "Co2Service.h"
#include "ImuService.h"

namespace gaia {

// Health counters + task watchdog.
//
// Watchdog policy: only the App task registers (watchTask()) and feeds (feed()) the
// 10 s task WDT. The App loop waits on the IMU sample queue with a 20 ms timeout, so
// a stalled IMU task does NOT trip the WDT; it shows up as imuHzX10 dropping to 0 in
// the diag packet. Not a task; call from App.
class Diagnostics {
 public:
  // esp_task_wdt_init(10 s, panic=true). Call once at boot.
  void begin();
  // Registers the CALLING task with the WDT (call once from the App task).
  void watchTask();
  // Resets the WDT for the calling task (App task, every loop).
  void feed();

  // Subsystem health travels in the diag packet flags (shown by the config-app);
  // there is no error LED — each LED pin has exactly one function.
  DiagPacket collect(uint16_t seq, uint16_t notifyHzX10, const ImuService &imu,
                     const AudioService &audio, const Co2Service &co2, uint32_t nowMs) const;
};

}  // namespace gaia
