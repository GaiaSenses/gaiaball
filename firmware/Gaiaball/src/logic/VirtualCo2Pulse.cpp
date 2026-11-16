#include "VirtualCo2Pulse.h"

namespace gaia {

// Ends the pulse once holdMs has elapsed since activation (wrap-safe).
void VirtualCo2Pulse::update(uint32_t nowMs) {
  if (holding_ && (uint32_t)(nowMs - startMs_) >= holdMs_) holding_ = false;
}

void VirtualCo2Pulse::trigger(uint32_t nowMs) {
  update(nowMs);
  if (holding_ || holdMs_ == 0) return;
  startMs_ = nowMs;
  holding_ = true;
}

bool VirtualCo2Pulse::active(uint32_t nowMs) {
  update(nowMs);
  return holding_;
}

}  // namespace gaia
