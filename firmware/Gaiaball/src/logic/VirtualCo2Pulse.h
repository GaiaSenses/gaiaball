#pragma once
#include <stdint.h>

namespace gaia {

// Synthetic CO2 event: trigger() activates it at once (the breath itself is the
// event, so the Co2Packet with the peak leaves on the same App loop) and it stays
// active for holdMs. Re-trigger while active is ignored.
// Timestamps are millis(); comparisons are wrap-safe.
class VirtualCo2Pulse {
 public:
  void configure(uint16_t peakPpm, uint32_t holdMs) {
    peakPpm_ = peakPpm; holdMs_ = holdMs;
  }
  void trigger(uint32_t nowMs);
  void cancel() { holding_ = false; }
  bool active(uint32_t nowMs);
  uint16_t peakPpm() const { return peakPpm_; }

 private:
  bool holding_ = false;
  uint16_t peakPpm_ = 0;
  uint32_t holdMs_ = 0;
  uint32_t startMs_ = 0;  // activation time

  void update(uint32_t nowMs);
};

}  // namespace gaia
