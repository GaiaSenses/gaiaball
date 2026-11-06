#pragma once
#include <stdint.h>

namespace gaia {

// Synthetic CO2 event: trigger() schedules activation after delayMs; it stays
// active for holdMs. Re-trigger while pending or active is ignored.
// Timestamps are millis(); comparisons are wrap-safe.
class VirtualCo2Pulse {
 public:
  void configure(uint16_t peakPpm, uint32_t delayMs, uint32_t holdMs) {
    peakPpm_ = peakPpm; delayMs_ = delayMs; holdMs_ = holdMs;
  }
  void trigger(uint32_t nowMs);
  void cancel() { state_ = Idle; }
  bool active(uint32_t nowMs);
  bool pending(uint32_t nowMs) const;
  uint16_t peakPpm() const { return peakPpm_; }

 private:
  enum State : uint8_t { Idle, Waiting, Holding };
  State state_ = Idle;
  uint16_t peakPpm_ = 0;
  uint32_t delayMs_ = 0, holdMs_ = 0;
  uint32_t startMs_ = 0;  // activation time

  void update(uint32_t nowMs);
};

}  // namespace gaia
