#pragma once
#include <cstddef>
#include <cstdint>

namespace gaia {

// Per-connection PIN unlock for sensitive BLE commands. The PIN is 4 ASCII
// digits. Two connection slots (connId 0/1); any other connId is always locked.
class CommandGate {
 public:
  static constexpr uint8_t kMaxConns = 2;
  static constexpr size_t kPinLen = 4;

  void setPin(const char pin[4]);
  bool unlock(uint8_t connId, const uint8_t *pin, size_t len);
  bool isUnlocked(uint8_t connId) const;
  void onDisconnect(uint8_t connId);

 private:
  char pin_[kPinLen] = {'0', '0', '0', '0'};
  bool unlocked_[kMaxConns] = {false, false};
};

}  // namespace gaia
