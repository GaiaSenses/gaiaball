#pragma once
#include <cstddef>
#include <cstdint>

namespace gaia {

// Per-connection PIN unlock for sensitive BLE commands. The PIN is 4 ASCII
// digits. Two connection slots (connId 0/1); any other connId is always locked.
//
// Brute-force protection (per slot): every rejected attempt (wrong PIN or wrong
// length) counts as a failure. After kMaxFailures consecutive failures, unlock()
// refuses without comparing until kLockoutMs have elapsed since the last failure;
// after that one attempt is compared, and another failure re-arms the lockout.
// Success and onDisconnect reset the streak. Times are wrap-safe uint32 ms.
class CommandGate {
 public:
  static constexpr uint8_t kMaxConns = 2;
  static constexpr size_t kPinLen = 4;
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kLockoutMs = 30000;

  void setPin(const char pin[4]);
  bool unlock(uint8_t connId, const uint8_t *pin, size_t len, uint32_t nowMs);
  bool isUnlocked(uint8_t connId) const;
  void onDisconnect(uint8_t connId);

 private:
  char pin_[kPinLen] = {'0', '0', '0', '0'};
  bool unlocked_[kMaxConns] = {false, false};
  uint8_t failures_[kMaxConns] = {0, 0};
  uint32_t lastFailMs_[kMaxConns] = {0, 0};
};

}  // namespace gaia
