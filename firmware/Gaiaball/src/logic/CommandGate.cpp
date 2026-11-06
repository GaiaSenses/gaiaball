#include "CommandGate.h"

namespace gaia {

void CommandGate::setPin(const char pin[4]) {
  for (size_t i = 0; i < kPinLen; i++) pin_[i] = pin[i];
}

bool CommandGate::unlock(uint8_t connId, const uint8_t *pin, size_t len) {
  if (connId >= kMaxConns || pin == nullptr || len != kPinLen) return false;
  uint8_t diff = 0;  // constant-time compare
  for (size_t i = 0; i < kPinLen; i++) diff |= (uint8_t)(pin[i] ^ (uint8_t)pin_[i]);
  if (diff != 0) return false;
  unlocked_[connId] = true;
  return true;
}

bool CommandGate::isUnlocked(uint8_t connId) const {
  return connId < kMaxConns && unlocked_[connId];
}

void CommandGate::onDisconnect(uint8_t connId) {
  if (connId < kMaxConns) unlocked_[connId] = false;
}

}  // namespace gaia
