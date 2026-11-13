#pragma once
#include <stdint.h>

#include "../logic/Packets.h"
#include "AudioService.h"
#include "Co2Service.h"
#include "ImuService.h"

namespace gaia {

// Health counters + task watchdog.
//
// Watchdog policy: EVERY long-running task registers with the 10 s task WDT
// (panic=true -> auto-reboot) and feeds it once per loop cycle — the App task via
// watchTask()/feed(), the imu/audio/co2 tasks via esp_task_wdt_add/reset directly in
// their taskLoop() (they cannot depend on this class: it includes their headers).
// Every blocking call in those loops has a finite timeout (i2s_read: 100 ms) so a
// stalled peripheral trips the WDT instead of degrading silently. Not a task; begin()
// is called once from App::begin() BEFORE the tasks start.
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
  // connItvlMsX10 comes from BleService::connIntervalMsX10(); it is passed in rather
  // than read here so this class stays independent of the BLE stack.
  DiagPacket collect(uint16_t seq, uint16_t notifyHzX10, const ImuService &imu,
                     const AudioService &audio, const Co2Service &co2, uint32_t nowMs,
                     uint16_t connItvlMsX10) const;
};

}  // namespace gaia
