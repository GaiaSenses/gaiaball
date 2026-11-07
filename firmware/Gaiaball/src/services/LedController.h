#pragma once
#include <stdint.h>

#include "ConfigStore.h"

namespace gaia {

// Drives the three LEDs, one pin per function (no sharing, no arbitration — the
// legacy firmware's triple use of GPIO2 was a design error, not something to paper
// over in software). Pin map (see firmware/docs/hardware-pinout.md):
//  - Sound LED (board D2, GPIO2): sound events / virtual-pulse hold. AudioService
//    events, set by the App task via setSoundLed().
//  - CO2 LED (board D1, GPIO4): on while the REAL ppm exceeds co2AlertPpm
//    (independent of the virtual pulse); driven by update().
//  - Status LED (NEW LED on GPIO23): the legacy BLE LED command (0x00/0x01) only,
//    set via setStatusLed(). Requires the physical LED to be added to the board.
// Pins come from config (TLV 0x30/0x31/0x33, enforced distinct) and are applied at
// begin() ONLY: changing them needs a reboot. Not a task: the App task calls the
// setters/update() (~20 Hz). Single-task use; not thread-safe.
class LedController {
 public:
  bool begin(ConfigStore &store);
  void setSoundLed(bool on) { soundOn_ = on; }
  void setStatusLed(bool on) { statusOn_ = on; }
  void update(uint32_t nowMs, uint16_t realPpm, uint16_t alertPpm);

 private:
  uint8_t soundPin_ = 2, co2Pin_ = 4, statusPin_ = 23;
  bool soundOn_ = false, statusOn_ = false;
  bool began_ = false;
};

}  // namespace gaia
