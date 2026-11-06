#include "ConfigStore.h"

#include <Preferences.h>

#include "../logic/TlvCodec.h"
#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kNs = "gaia";
constexpr const char *kKeyCfg = "cfg";
constexpr const char *kKeyVer = "ver";
constexpr const char *kTag = "cfg";
Preferences g_prefs;
}  // namespace

bool ConfigStore::begin() {
  open_ = g_prefs.begin(kNs, false);
  if (!open_) {
    GB_LOGE(kTag, "NVS open failed; using defaults");
    return false;
  }
  if (g_prefs.getUChar(kKeyVer, 0) == kConfigSchemaVersion &&
      g_prefs.getBytesLength(kKeyCfg) == sizeof(GaiaConfig)) {
    GaiaConfig loaded;
    g_prefs.getBytes(kKeyCfg, &loaded, sizeof(loaded));
    // Guard against a non-terminated string from corrupted storage.
    loaded.bleName[sizeof(loaded.bleName) - 1] = 0;
    loaded.pin[sizeof(loaded.pin) - 1] = 0;
    cfg_ = loaded;
    GB_LOGI(kTag, "loaded from NVS");
  } else {
    GB_LOGI(kTag, "no valid stored config; using defaults");
  }
  return true;
}

bool ConfigStore::save() {
  if (!open_) return false;
  const bool ok = g_prefs.putBytes(kKeyCfg, &cfg_, sizeof(cfg_)) == sizeof(cfg_) &&
                  g_prefs.putUChar(kKeyVer, kConfigSchemaVersion) == sizeof(uint8_t);
  if (!ok) GB_LOGE(kTag, "save failed");
  return ok;
}

void ConfigStore::factoryReset() {
  if (open_) g_prefs.clear();
  cfg_ = GaiaConfig();
  GB_LOGW(kTag, "factory reset");
}

bool ConfigStore::applyTlv(const uint8_t *data, size_t len) {
  GaiaConfig copy = cfg_;
  if (!tlvApply(copy, data, len)) return false;
  cfg_ = copy;
  if (onChange_) onChange_(cfg_);
  return true;
}

size_t ConfigStore::serializeTlv(uint8_t *out, size_t cap) { return tlvSerialize(cfg_, out, cap); }

}  // namespace gaia
