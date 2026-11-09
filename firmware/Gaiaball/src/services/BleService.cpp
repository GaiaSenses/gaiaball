#include "BleService.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include "../logic/CommandGate.h"
#include "ConfigStore.h"
#include "Logger.h"

namespace gaia {

namespace {

constexpr const char *kTag = "BLE";

constexpr const char *kServiceUuid = "19b10000-e8f2-537e-4f6c-d104768a1214";
constexpr const char *kImuUuid = "19b10001-e8f2-537e-4f6c-d104768a1214";
constexpr const char *kCommandUuid = "19b10002-e8f2-537e-4f6c-d104768a1214";
constexpr const char *kCo2Uuid = "19b10003-e8f2-537e-4f6c-d104768a1214";
constexpr const char *kConfigUuid = "19b10004-e8f2-537e-4f6c-d104768a1214";
constexpr const char *kDiagUuid = "19b10005-e8f2-537e-4f6c-d104768a1214";

constexpr uint16_t kPreferredMtu = 247;
constexpr uint16_t kNoHandle = 0xFFFF;  // == BLE_HS_CONN_HANDLE_NONE
constexpr size_t kConfigReadBuf = 384;  // see ConfigStore kTlvBufCap
// onSubscribe() subValue bit for notifications (NimBLE-Arduino 1.4.3 keeps its
// NIMBLE_SUB_NOTIFY private to NimBLECharacteristic.cpp).
constexpr uint16_t kSubNotify = 0x0001;

}  // namespace

// ---------------------------------------------------------------------------
// Thin NimBLE callback adapters: extract the connection handle and forward.

class BleService::ServerCb : public NimBLEServerCallbacks {
 public:
  explicit ServerCb(BleService &s) : s_(s) {}

  void onConnect(NimBLEServer *server, ble_gap_conn_desc *desc) override {
    uint8_t slot = s_.claimSlot(desc->conn_handle);
    if (slot == kNoSlot) {
      GB_LOGW(kTag, "conn %u rejected: both slots in use", desc->conn_handle);
      server->disconnect(desc->conn_handle);
      return;
    }
    GB_LOGI(kTag, "conn %u -> slot %u (%u connected)", desc->conn_handle, slot,
            s_.connCount_.load());
    s_.restartAdvertisingIfRoom();
  }

  void onDisconnect(NimBLEServer *, ble_gap_conn_desc *desc) override {
    uint8_t slot = s_.slotOf(desc->conn_handle);
    if (slot != kNoSlot) {
      s_.releaseSlot(slot);
      GB_LOGI(kTag, "slot %u disconnected (%u connected)", slot, s_.connCount_.load());
    }
    s_.restartAdvertisingIfRoom();
  }

  void onMTUChange(uint16_t mtu, ble_gap_conn_desc *desc) override {
    GB_LOGD(kTag, "conn %u MTU %u", desc->conn_handle, mtu);
  }

 private:
  BleService &s_;
};

class BleService::NotifyCb : public NimBLECharacteristicCallbacks {
 public:
  NotifyCb(BleService &s, uint8_t idx) : s_(s), idx_(idx) {}

  void onSubscribe(NimBLECharacteristic *, ble_gap_conn_desc *desc, uint16_t subValue) override {
    s_.onSubscribe(idx_, desc->conn_handle, subValue);
  }

 private:
  BleService &s_;
  uint8_t idx_;
};

class BleService::CommandCb : public NimBLECharacteristicCallbacks {
 public:
  explicit CommandCb(BleService &s) : s_(s) {}

  void onWrite(NimBLECharacteristic *chr, ble_gap_conn_desc *desc) override {
    NimBLEAttValue v = chr->getValue();
    s_.onCommandWrite(desc->conn_handle, v.data(), v.length());
  }

 private:
  BleService &s_;
};

class BleService::ConfigCb : public NimBLECharacteristicCallbacks {
 public:
  explicit ConfigCb(BleService &s) : s_(s) {}

  void onRead(NimBLECharacteristic *chr, ble_gap_conn_desc *) override { s_.onConfigRead(chr); }

  void onWrite(NimBLECharacteristic *chr, ble_gap_conn_desc *desc) override {
    NimBLEAttValue v = chr->getValue();
    s_.onConfigWrite(desc->conn_handle, v.data(), v.length());
  }

 private:
  BleService &s_;
};

// ---------------------------------------------------------------------------

bool BleService::begin(ConfigStore &store, CommandGate &gate) {
  store_ = &store;
  gate_ = &gate;
  for (uint8_t i = 0; i < kMaxConns; i++) connHandles_[i] = kNoHandle;

  cmdQueue_ = xQueueCreate(kCommandQueueDepth, sizeof(BleCommand));
  if (!cmdQueue_) {
    GB_LOGE(kTag, "command queue alloc failed");
    return false;
  }

  const GaiaConfig &cfg = store.get();
  gate.setPin(cfg.pin);

  NimBLEDevice::init(cfg.bleName);
  NimBLEDevice::setMTU(kPreferredMtu);

  server_ = NimBLEDevice::createServer();
  if (!server_) return false;
  // Re-advertising is managed by ServerCb (needs the 2-connection cap).
  server_->advertiseOnDisconnect(false);
  // Callback objects live for the whole program (NimBLE is never deinitialized).
  server_->setCallbacks(new ServerCb(*this));

  NimBLEService *svc = server_->createService(kServiceUuid);
  if (!svc) return false;

  NimBLECharacteristic *imu =
      svc->createCharacteristic(kImuUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  NimBLECharacteristic *cmd =
      svc->createCharacteristic(kCommandUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  NimBLECharacteristic *co2 =
      svc->createCharacteristic(kCo2Uuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  NimBLECharacteristic *config =
      svc->createCharacteristic(kConfigUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  NimBLECharacteristic *diag =
      svc->createCharacteristic(kDiagUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  if (!imu || !cmd || !co2 || !config || !diag) return false;

  notifyChars_[kIdxImu] = imu;
  notifyChars_[kIdxCo2] = co2;
  notifyChars_[kIdxDiag] = diag;
  imu->setCallbacks(new NotifyCb(*this, kIdxImu));
  co2->setCallbacks(new NotifyCb(*this, kIdxCo2));
  diag->setCallbacks(new NotifyCb(*this, kIdxDiag));
  cmd->setCallbacks(new CommandCb(*this));
  config->setCallbacks(new ConfigCb(*this));

  // Pre-size the notify values so setValue() never reallocates later.
  ImuPacket ip{};
  Co2Packet cp{};
  DiagPacket dp{};
  imu->setValue(reinterpret_cast<const uint8_t *>(&ip), sizeof(ip));
  co2->setValue(reinterpret_cast<const uint8_t *>(&cp), sizeof(cp));
  diag->setValue(reinterpret_cast<const uint8_t *>(&dp), sizeof(dp));

  if (!svc->start()) {
    GB_LOGE(kTag, "service start failed");
    return false;
  }

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(kServiceUuid);
  adv->setScanResponse(true);  // name moves to the scan response when it does not fit
  if (!adv->start()) {
    GB_LOGE(kTag, "advertising start failed");
    return false;
  }
  GB_LOGI(kTag, "advertising as \"%s\"", cfg.bleName);
  return true;
}

void BleService::notifyImu(const ImuPacket &p) { notify(kIdxImu, &p, sizeof(p)); }
void BleService::notifyCo2(const Co2Packet &p) { notify(kIdxCo2, &p, sizeof(p)); }
void BleService::notifyDiag(const DiagPacket &p) { notify(kIdxDiag, &p, sizeof(p)); }
void BleService::notifySensors(const SensorsPacket &p) { notify(kIdxDiag, &p, sizeof(p)); }
void BleService::notifyAudioChunk(const AudioChunkPacket &p) { notify(kIdxDiag, &p, sizeof(p)); }
void BleService::notifyMagChunk(const MagChunkPacket &p) { notify(kIdxDiag, &p, sizeof(p)); }

void BleService::notify(uint8_t idx, const void *data, size_t len) {
  NimBLECharacteristic *chr = notifyChars_[idx];
  if (!chr || subMask_[idx].load() == 0) return;
  chr->setValue(static_cast<const uint8_t *>(data), len);
  chr->notify();
}

bool BleService::anySubscriber(uint16_t charSel) const {
  switch (charSel) {
    case 1: return subMask_[kIdxImu].load() != 0;
    case 3: return subMask_[kIdxCo2].load() != 0;
    case 5: return subMask_[kIdxDiag].load() != 0;
    default: return false;
  }
}

// --- host task only ---------------------------------------------------------

uint8_t BleService::slotOf(uint16_t connHandle) const {
  for (uint8_t i = 0; i < kMaxConns; i++)
    if (connHandles_[i] == connHandle) return i;
  return kNoSlot;
}

uint8_t BleService::claimSlot(uint16_t connHandle) {
  uint8_t slot = slotOf(kNoHandle);
  if (slot == kNoSlot) return kNoSlot;
  connHandles_[slot] = connHandle;
  connCount_.fetch_add(1);
  return slot;
}

void BleService::releaseSlot(uint8_t slot) {
  connHandles_[slot] = kNoHandle;
  connCount_.fetch_sub(1);
  // The host may tear the connection down before (or without) delivering the
  // unsubscribe events, so clear this slot's subscriptions explicitly.
  uint8_t keep = (uint8_t)~(1u << slot);
  for (uint8_t i = 0; i < kNotifyChars; i++) subMask_[i].fetch_and(keep);
  gate_->onDisconnect(slot);
}

void BleService::restartAdvertisingIfRoom() {
  if (connCount_.load() >= kMaxConns) return;
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  if (adv->isAdvertising()) return;
  if (!adv->start()) GB_LOGW(kTag, "advertising restart failed");
}

void BleService::onSubscribe(uint8_t idx, uint16_t connHandle, uint16_t subValue) {
  uint8_t slot = slotOf(connHandle);
  if (slot == kNoSlot) return;
  uint8_t bit = (uint8_t)(1u << slot);
  if (subValue & kSubNotify)
    subMask_[idx].fetch_or(bit);
  else
    subMask_[idx].fetch_and((uint8_t)~bit);
  GB_LOGD(kTag, "slot %u char %u notify %s", slot, idx, (subValue & kSubNotify) ? "on" : "off");
}

void BleService::onCommandWrite(uint16_t connHandle, const uint8_t *data, size_t len) {
  uint8_t slot = slotOf(connHandle);
  if (slot == kNoSlot || len == 0) return;
  const uint8_t opcode = data[0];

  if (opcode == kCmdUnlock) {
    bool ok = gate_->unlock(slot, data + 1, len - 1, millis());
    if (ok)
      GB_LOGI(kTag, "slot %u unlocked", slot);
    else
      GB_LOGW(kTag, "slot %u unlock rejected (bad PIN or locked out)", slot);
    return;
  }

  BleCommand cmd{};
  if (len - 1 > sizeof(cmd.arg)) {
    GB_LOGW(kTag, "cmd 0x%02x: %u arg bytes > %u, dropped", opcode, (unsigned)(len - 1),
            (unsigned)sizeof(cmd.arg));
    return;
  }
  cmd.opcode = opcode;
  cmd.connSlot = slot;
  cmd.argLen = (uint8_t)(len - 1);
  cmd.unlockedAtEnqueue = gate_->isUnlocked(slot) ? 1 : 0;
  memcpy(cmd.arg, data + 1, cmd.argLen);
  if (xQueueSend(cmdQueue_, &cmd, 0) != pdTRUE) GB_LOGW(kTag, "cmd 0x%02x dropped: queue full", opcode);
}

void BleService::onConfigWrite(uint16_t connHandle, const uint8_t *data, size_t len) {
  uint8_t slot = slotOf(connHandle);
  if (slot == kNoSlot) return;
  if (!gate_->isUnlocked(slot)) {
    GB_LOGW(kTag, "slot %u config write rejected: locked", slot);
    return;
  }
  if (store_->applyTlv(data, len))
    GB_LOGI(kTag, "slot %u config applied (%u bytes)", slot, (unsigned)len);
  else
    GB_LOGW(kTag, "slot %u config write invalid TLV, ignored", slot);
}

void BleService::onConfigRead(NimBLECharacteristic *chr) {
  uint8_t buf[kConfigReadBuf];
  size_t n = store_->serializeTlv(buf, sizeof(buf));
  chr->setValue(buf, n);
}

}  // namespace gaia
