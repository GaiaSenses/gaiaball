#include "ConfigStore.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../logic/TlvCodec.h"
#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kNs = "gaia";
constexpr const char *kKeyCfg = "cfg";
constexpr const char *kKeyVer = "ver";
constexpr const char *kTag = "cfg";
Preferences g_prefs;

// Scoped lock on the ConfigStore mutex; no-op when begin() never created it.
class Lock {
 public:
  explicit Lock(SemaphoreHandle_t m) : m_(m) {
    if (m_) xSemaphoreTake(m_, portMAX_DELAY);
  }
  ~Lock() {
    if (m_) xSemaphoreGive(m_);
  }
  Lock(const Lock &) = delete;
  Lock &operator=(const Lock &) = delete;

 private:
  SemaphoreHandle_t m_;
};
}  // namespace

bool ConfigStore::begin() {
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) GB_LOGE(kTag, "mutex alloc failed; running unlocked");
  Lock lock(mutex_);
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
  bool ok;
  {
    Lock lock(mutex_);
    if (!open_) return false;
    ok = g_prefs.putBytes(kKeyCfg, &cfg_, sizeof(cfg_)) == sizeof(cfg_) &&
         g_prefs.putUChar(kKeyVer, kConfigSchemaVersion) == sizeof(uint8_t);
  }
  if (!ok) GB_LOGE(kTag, "save failed");
  return ok;
}

void ConfigStore::factoryReset() {
  {
    Lock lock(mutex_);
    if (open_) g_prefs.clear();
    cfg_ = GaiaConfig();
  }
  GB_LOGW(kTag, "factory reset");
}

void ConfigStore::setMountQuat(const float q[4]) {
  Lock lock(mutex_);
  for (int i = 0; i < 4; i++) cfg_.mountQuat[i] = q[i];
}

bool ConfigStore::applyTlv(const uint8_t *data, size_t len) {
  GaiaConfig snapshot;
  {
    Lock lock(mutex_);
    snapshot = cfg_;
    if (!tlvApply(snapshot, data, len)) return false;
    cfg_ = snapshot;
  }
  // Fired outside the lock so handlers may call back into the store.
  if (onChange_) onChange_(snapshot);
  return true;
}

size_t ConfigStore::serializeTlv(uint8_t *out, size_t cap) {
  Lock lock(mutex_);
  return tlvSerialize(cfg_, out, cap);
}

}  // namespace gaia
