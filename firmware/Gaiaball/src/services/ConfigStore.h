#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "../logic/GaiaConfig.h"

namespace gaia {

// Since v6 the config is persisted as TLV (stable register ids, unknown/invalid records
// skipped on load), so a schema bump no longer discards the stored config — new fields
// simply load their defaults. The stored version is read back only to migrate values whose
// meaning or default changed (migrateStoredConfig in GaiaConfig.h).
// v6: TLV persistence; v7: imuChip, magAxisRemap; v8: breath detector in dB, 0x15 retired;
// v9: band-pass slopes (0x38, 0x39) and a flat band by default (Q 0.71, was 5.00);
// v10: breath discriminators (0x3A..0x3F, 0x42, 0x43), new fields only (nothing to migrate).
constexpr uint8_t kConfigSchemaVersion = 10;
static_assert(kConfigSchemaVersion >= kBandFlatQSchemaVersion, "saves must stop the Q migration");

// Threading: internally locked for applyTlv/serializeTlv/save/factoryReset/setMountQuat
// (callable from BLE and App tasks; the mutex is created in begin(), before which the
// methods run unlocked). get() returns an unlocked reference: use it for pre-task setup
// and App-task scalar reads only; cross-task field writes must go through locked
// accessors. onChange fires synchronously on the caller's task AFTER the lock is
// released, with a snapshot of the new config; handlers must be quick and non-blocking
// (applyTlv is called from the NimBLE host task).
// Persistence (NVS namespace "gaia"): key "tlv" holds the config serialized with the
// same TLV registers as the BLE characteristic (loaded leniently: unknown/invalid
// records are skipped, cross-field invariants restored); the PIN ("pin") and the
// mounting quaternion ("mount") have no readable TLV register, so each gets its own
// key; "ver" is the schema the stored config was saved with (absent = before it existed),
// used by migrateStoredConfig. A pre-v6 whole-struct blob ("cfg") is discarded.
class ConfigStore {
 public:
  // Opens NVS and loads the stored config; keeps defaults on missing/mismatched data.
  // Returns false = NVS unavailable, running volatile (defaults, save() will fail).
  bool begin();
  GaiaConfig &get() { return cfg_; }
  bool save();
  void factoryReset();
  // Writes the mounting quaternion (w,x,y,z). Does not save and does not fire onChange.
  void setMountQuat(const float q[4]);
  void setOnChange(std::function<void(const GaiaConfig &)> cb) { onChange_ = std::move(cb); }

  // Validated TLV apply: commits to the live config only if the whole payload is valid.
  bool applyTlv(const uint8_t *data, size_t len);
  size_t serializeTlv(uint8_t *out, size_t cap);

 private:
  GaiaConfig cfg_;
  std::function<void(const GaiaConfig &)> onChange_;
  bool open_ = false;
  SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace gaia
