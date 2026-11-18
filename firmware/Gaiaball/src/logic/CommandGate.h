#pragma once
#include <cstddef>
#include <cstdint>

namespace gaia {

// Command opcodes executed by the App task (docs/protocol.md). 0x20 (unlock) never gets
// there: BleService runs it inline. 0x00/0x01 (the legacy LED command) are reserved and
// unknown: no client ever sent them and the board has no LED for them.
constexpr uint8_t kCmdZero = 0x10;
constexpr uint8_t kCmdSave = 0x11;
constexpr uint8_t kCmdFactoryReset = 0x12;
constexpr uint8_t kCmdReboot = 0x13;
constexpr uint8_t kCmdMountCal = 0x14;
constexpr uint8_t kCmdMagCalStart = 0x15;
constexpr uint8_t kCmdMagCalStop = 0x16;
constexpr uint8_t kCmdAnchor = 0x17;  // map view as the navigation reference (RAM only)

// Unknown opcodes are ignored; Open ones run on any connection (0x17 only: the site
// anchors the map without a PIN, and the anchor is volatile); PinRequired ones run only
// if the writer's connection was unlocked when the write arrived.
enum class CommandAccess : uint8_t { Unknown, Open, PinRequired };
CommandAccess commandAccess(uint8_t opcode);
// The App task's verdict for a queued command (BleCommand::unlockedAtEnqueue).
bool commandAllowed(uint8_t opcode, bool unlockedAtEnqueue);

// What a connection's gate looks like right now. The byte values travel to the clients as
// the read-only config record 0x37 (unlockState, see StatusRecords.h): keep them stable.
enum class GateState : uint8_t {
  Locked = 0,     // no PIN accepted on this connection (or a wrong one)
  Unlocked = 1,   // the PIN was accepted: gated commands and config writes run
  LockedOut = 2,  // kMaxFailures wrong PINs: unlock() refuses until kLockoutMs pass
};

// Per-connection PIN unlock for sensitive BLE commands. The PIN is 4 ASCII
// digits. Two connection slots (connId 0/1); any other connId is always locked.
//
// Brute-force protection (per slot): every rejected attempt (wrong PIN or wrong
// length) counts as a failure. After kMaxFailures consecutive failures, unlock()
// refuses without comparing until kLockoutMs have elapsed since the last failure;
// after that one attempt is compared, and another failure re-arms the lockout.
// Success and onDisconnect reset the streak; a wrong PIN also re-locks a connection that was
// unlocked. Times are wrap-safe uint32 ms.
class CommandGate {
 public:
  static constexpr uint8_t kMaxConns = 2;
  static constexpr size_t kPinLen = 4;
  static constexpr uint8_t kMaxFailures = 5;
  static constexpr uint32_t kLockoutMs = 30000;

  void setPin(const char pin[4]);
  bool unlock(uint8_t connId, const uint8_t *pin, size_t len, uint32_t nowMs);
  bool isUnlocked(uint8_t connId) const;
  // Locked for any connId out of range. LockedOut uses the same wrap-safe window as unlock().
  GateState state(uint8_t connId, uint32_t nowMs) const;
  void onDisconnect(uint8_t connId);

 private:
  char pin_[kPinLen] = {'0', '0', '0', '0'};
  bool unlocked_[kMaxConns] = {false, false};
  uint8_t failures_[kMaxConns] = {0, 0};
  uint32_t lastFailMs_[kMaxConns] = {0, 0};
};

}  // namespace gaia
