#include "CommandGate.h"

namespace gaia {

CommandAccess commandAccess(uint8_t opcode) {
  switch (opcode) {
    case kCmdAnchor:
    case kCmdEndCo2Pulse:
      return CommandAccess::Open;
    case kCmdZero:
    case kCmdSave:
    case kCmdFactoryReset:
    case kCmdReboot:
    case kCmdMountCal:
    case kCmdMagCalStart:
    case kCmdMagCalStop:
      return CommandAccess::PinRequired;
    default:
      return CommandAccess::Unknown;
  }
}

bool commandAllowed(uint8_t opcode, bool unlockedAtEnqueue) {
  switch (commandAccess(opcode)) {
    case CommandAccess::Open:
      return true;
    case CommandAccess::PinRequired:
      return unlockedAtEnqueue;
    default:
      return false;
  }
}

void CommandGate::setPin(const char pin[4]) {
  for (size_t i = 0; i < kPinLen; i++) pin_[i] = pin[i];
}

bool CommandGate::unlock(uint8_t connId, const uint8_t *pin, size_t len, uint32_t nowMs) {
  if (connId >= kMaxConns) return false;
  if (failures_[connId] >= kMaxFailures && (uint32_t)(nowMs - lastFailMs_[connId]) < kLockoutMs)
    return false;  // locked out: refuse without comparing

  bool ok = pin != nullptr && len == kPinLen;
  if (ok) {
    uint8_t diff = 0;  // constant-time compare
    for (size_t i = 0; i < kPinLen; i++) diff |= (uint8_t)(pin[i] ^ (uint8_t)pin_[i]);
    ok = diff == 0;
  }
  if (!ok) {
    // A wrong PIN also re-locks an unlocked connection: whoever checks a PIN must learn it is wrong.
    unlocked_[connId] = false;
    if (failures_[connId] < 0xFF) failures_[connId]++;
    lastFailMs_[connId] = nowMs;
    return false;
  }
  failures_[connId] = 0;
  unlocked_[connId] = true;
  return true;
}

bool CommandGate::isUnlocked(uint8_t connId) const {
  return connId < kMaxConns && unlocked_[connId];
}

GateState CommandGate::state(uint8_t connId, uint32_t nowMs) const {
  if (connId >= kMaxConns) return GateState::Locked;
  if (unlocked_[connId]) return GateState::Unlocked;
  if (failures_[connId] >= kMaxFailures && (uint32_t)(nowMs - lastFailMs_[connId]) < kLockoutMs)
    return GateState::LockedOut;
  return GateState::Locked;
}

void CommandGate::onDisconnect(uint8_t connId) {
  if (connId >= kMaxConns) return;
  unlocked_[connId] = false;
  failures_[connId] = 0;
  lastFailMs_[connId] = 0;
}

}  // namespace gaia
