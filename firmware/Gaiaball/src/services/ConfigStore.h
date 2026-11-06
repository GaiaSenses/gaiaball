#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "../logic/GaiaConfig.h"

namespace gaia {

// Bump when GaiaConfig layout changes; mismatched stored data falls back to defaults.
constexpr uint8_t kConfigSchemaVersion = 2;  // v2: GaiaConfig.mountQuat

// Threading: internally locked for applyTlv/serializeTlv/save/factoryReset/setMountQuat
// (callable from BLE and App tasks; the mutex is created in begin(), before which the
// methods run unlocked). get() returns an unlocked reference: use it for pre-task setup
// and App-task scalar reads only; cross-task field writes must go through locked
// accessors. onChange fires synchronously on the caller's task AFTER the lock is
// released, with a snapshot of the new config; handlers must be quick and non-blocking
// (applyTlv is called from the NimBLE host task).
// Persists the whole GaiaConfig as one NVS blob (namespace "gaia", keys "cfg" + "ver").
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
