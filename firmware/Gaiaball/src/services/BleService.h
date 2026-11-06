#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "../logic/Packets.h"

class NimBLEServer;
class NimBLECharacteristic;

namespace gaia {

class ConfigStore;
class CommandGate;

// Command opcodes with special handling inside BleService. Every other opcode is
// forwarded verbatim through the command queue.
constexpr uint8_t kCmdLedOff = 0x00;  // legacy, ungated
constexpr uint8_t kCmdLedOn = 0x01;   // legacy, ungated
constexpr uint8_t kCmdUnlock = 0x20;  // executed inline: CommandGate::unlock(slot, PIN)

// One command-characteristic write, as handed to the App task.
// connSlot is the CommandGate connId (0/1) of the writer. arg excludes the opcode.
struct BleCommand {
  uint8_t opcode;
  uint8_t connSlot;
  uint8_t argLen;
  uint8_t arg[8];
};

// NimBLE GATT server: one service, five characteristics (short ids 1..5):
//   1 IMU     NOTIFY|READ    ImuPacket
//   2 command WRITE|WRITE_NR opcode + args (see BleCommand)
//   3 CO2     NOTIFY|READ    Co2Packet
//   4 config  READ|WRITE     TLV (read = ConfigStore::serializeTlv, PIN redacted;
//                            write = ConfigStore::applyTlv, requires the writer's
//                            connection to be unlocked)
//   5 diag    NOTIFY|READ    DiagPacket
//
// Connections: up to 2 concurrent centrals, mapped to CommandGate slots 0/1.
// Advertising is restarted after each connect while fewer than 2 are connected and
// after every disconnect. A connection that finds both slots taken is dropped.
//
// Threading:
//  - All NimBLE callbacks run on the NimBLE host task and never block: command
//    writes are pushed (non-blocking) to commandQueue(); only the cheap 0x20 unlock
//    runs inline. A full queue drops the command with a warning.
//  - Config writes call ConfigStore::applyTlv on the host task, which fires the
//    ConfigStore onChange handler synchronously there: onChange handlers MUST be
//    quick and non-blocking (set flags / queue work for the App task; no NVS save,
//    no I2C/UART, no delays). PIN changes must be forwarded to CommandGate::setPin
//    by that handler.
//  - notifyImu/Co2/Diag are meant for the App task. NimBLE's setValue() copies under
//    its own lock and notify() hands an mbuf to the host stack, so calling them from
//    a task other than the host task is the library's supported pattern. No heap
//    allocation of our own in these paths.
//  - Subscription state is tracked per characteristic and per slot in atomics
//    written by the host task, so anySubscriber() is safe from any task.
//
// The device name is taken from GaiaConfig::bleName in begin(); a renamed device
// takes effect after reboot (no runtime re-init).
//
// The App task executes queued commands and enforces the PIN for sensitive opcodes
// at execution time via CommandGate::isUnlocked(cmd.connSlot); 0x00/0x01 are ungated.
class BleService {
 public:
  static constexpr uint8_t kMaxConns = 2;
  static constexpr UBaseType_t kCommandQueueDepth = 8;
  static constexpr uint8_t kNoSlot = 0xFF;

  // Initializes NimBLE, builds the GATT table and starts advertising.
  // Returns false if the command queue or any GATT object could not be created.
  bool begin(ConfigStore &store, CommandGate &gate);

  void notifyImu(const ImuPacket &p);
  void notifyCo2(const Co2Packet &p);
  void notifyDiag(const DiagPacket &p);

  // charSel = characteristic short id: 1 (IMU), 3 (CO2) or 5 (diag). Other ids: false.
  bool anySubscriber(uint16_t charSel) const;

  // Queue of BleCommand items for the App task (depth 8). nullptr before begin().
  QueueHandle_t commandQueue() const { return cmdQueue_; }

  uint8_t connectedCount() const { return connCount_.load(); }

 private:
  class ServerCb;
  class CommandCb;
  class ConfigCb;
  class NotifyCb;

  enum NotifyIdx : uint8_t { kIdxImu = 0, kIdxCo2 = 1, kIdxDiag = 2, kNotifyChars = 3 };

  // Host-task-only helpers.
  uint8_t slotOf(uint16_t connHandle) const;
  uint8_t claimSlot(uint16_t connHandle);
  void releaseSlot(uint8_t slot);
  void restartAdvertisingIfRoom();
  void onSubscribe(uint8_t idx, uint16_t connHandle, uint16_t subValue);
  void onCommandWrite(uint16_t connHandle, const uint8_t *data, size_t len);
  void onConfigWrite(uint16_t connHandle, const uint8_t *data, size_t len);
  void onConfigRead(NimBLECharacteristic *chr);

  void notify(uint8_t idx, const void *data, size_t len);

  ConfigStore *store_ = nullptr;
  CommandGate *gate_ = nullptr;
  NimBLEServer *server_ = nullptr;
  NimBLECharacteristic *notifyChars_[kNotifyChars] = {nullptr, nullptr, nullptr};
  QueueHandle_t cmdQueue_ = nullptr;

  uint16_t connHandles_[kMaxConns] = {0xFFFF, 0xFFFF};  // host task only; 0xFFFF = free
  std::atomic<uint8_t> connCount_{0};
  // Bit n set = slot n has notifications enabled on that characteristic.
  std::atomic<uint8_t> subMask_[kNotifyChars] = {{0}, {0}, {0}};
};

}  // namespace gaia
