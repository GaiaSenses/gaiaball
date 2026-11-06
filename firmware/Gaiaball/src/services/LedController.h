#pragma once
#include <stdint.h>

#include "../logic/LedPriority.h"
#include "ConfigStore.h"

namespace gaia {

// Drives the two status LEDs. Not a task: the App task calls set()/update() (~20 Hz).
//  - Sound LED: level = LedPriority::resolved() over BleCommand/SoundEvent/Error sources.
//  - CO2 LED: on while the REAL ppm exceeds co2AlertPpm (independent of the virtual pulse).
// Pins come from config (TLV 0x30/0x31) and are applied at begin() ONLY: changing them
// needs a reboot. Single-task use (App task); not thread-safe.
class LedController {
 public:
  bool begin(ConfigStore &store);
  void set(LedPriority::Source src, bool on) { prio_.set(src, on); }
  void update(uint32_t nowMs, uint16_t realPpm, uint16_t alertPpm);

 private:
  LedPriority prio_;
  uint8_t soundPin_ = 2, co2Pin_ = 4;
  bool began_ = false;
};

}  // namespace gaia
