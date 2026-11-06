#pragma once
#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "../logic/GaiaConfig.h"

namespace gaia {

// Bump when GaiaConfig layout changes; mismatched stored data falls back to defaults.
constexpr uint8_t kConfigSchemaVersion = 1;

// Persists the whole GaiaConfig as one NVS blob (namespace "gaia", keys "cfg" + "ver").
class ConfigStore {
 public:
  // Opens NVS and loads the stored config; keeps defaults on missing/mismatched data.
  bool begin();
  GaiaConfig &get() { return cfg_; }
  bool save();
  void factoryReset();
  void setOnChange(std::function<void(const GaiaConfig &)> cb) { onChange_ = std::move(cb); }

  // Validated TLV apply: commits to the live config only if the whole payload is valid.
  bool applyTlv(const uint8_t *data, size_t len);
  size_t serializeTlv(uint8_t *out, size_t cap);

 private:
  GaiaConfig cfg_;
  std::function<void(const GaiaConfig &)> onChange_;
  bool open_ = false;
};

}  // namespace gaia
