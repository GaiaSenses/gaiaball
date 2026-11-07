#include "VirtualCo2Pulse.h"

namespace gaia {

// Advances Waiting -> Holding -> Idle based on elapsed time (wrap-safe).
void VirtualCo2Pulse::update(uint32_t nowMs) {
  if (state_ == Waiting && (int32_t)(nowMs - startMs_) >= 0) state_ = Holding;
  if (state_ == Holding && (uint32_t)(nowMs - startMs_) >= holdMs_) state_ = Idle;
}

void VirtualCo2Pulse::trigger(uint32_t nowMs) {
  update(nowMs);
  if (state_ != Idle) return;
  startMs_ = nowMs + delayMs_;
  state_ = Waiting;
}

bool VirtualCo2Pulse::active(uint32_t nowMs) {
  update(nowMs);
  return state_ == Holding;
}

bool VirtualCo2Pulse::pending(uint32_t nowMs) const {
  return state_ == Waiting && (int32_t)(nowMs - startMs_) < 0;
}

}  // namespace gaia
