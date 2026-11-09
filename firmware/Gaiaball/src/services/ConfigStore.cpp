#include "ConfigStore.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "../logic/TlvCodec.h"
#include "Logger.h"

namespace gaia {

namespace {
constexpr const char *kNs = "gaia";
constexpr const char *kKeyLegacyBlob = "cfg";  // pre-v6 whole-struct blob, discarded
constexpr const char *kKeyTlv = "tlv";
constexpr const char *kKeyPin = "pin";
constexpr const char *kKeyMount = "mount";
constexpr const char *kKeyVer = "ver";
constexpr const char *kTag = "cfg";
// Worst-case tlvSerialize output is ~190 B (43 registers, incl. the 24-byte
// magnetometer calibration record); headroom for new ids.
constexpr size_t kTlvBufCap = 384;
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
  // Pre-v6 schema stored the whole struct as one blob; its layout is not portable
  // across versions, so it is discarded rather than migrated (no deployed devices).
  if (g_prefs.isKey(kKeyLegacyBlob)) {
    g_prefs.remove(kKeyLegacyBlob);
    GB_LOGW(kTag, "legacy blob config discarded (pre-TLV schema)");
  }
  const size_t tlvLen = g_prefs.getBytesLength(kKeyTlv);
  if (tlvLen > 0 && tlvLen <= kTlvBufCap) {
    uint8_t buf[kTlvBufCap];
    g_prefs.getBytes(kKeyTlv, buf, tlvLen);
    const size_t applied = tlvApplyLenient(cfg_, buf, tlvLen);
    GB_LOGI(kTag, "loaded from NVS (%u TLV records)", (unsigned)applied);
  } else {
    GB_LOGI(kTag, "no stored config; using defaults");
  }
  // PIN and mounting quaternion have no readable TLV register: own keys.
  if (g_prefs.getBytesLength(kKeyPin) == sizeof(cfg_.pin) - 1) {
    char pin[sizeof(cfg_.pin)] = {};
    g_prefs.getBytes(kKeyPin, pin, sizeof(cfg_.pin) - 1);
    bool digits = true;
    for (size_t i = 0; i < sizeof(cfg_.pin) - 1; i++)
      if (pin[i] < '0' || pin[i] > '9') digits = false;
    if (digits) memcpy(cfg_.pin, pin, sizeof(cfg_.pin));
  }
  if (g_prefs.getBytesLength(kKeyMount) == sizeof(cfg_.mountQuat))
    g_prefs.getBytes(kKeyMount, cfg_.mountQuat, sizeof(cfg_.mountQuat));
  return true;
}

bool ConfigStore::save() {
  bool ok;
  {
    Lock lock(mutex_);
    if (!open_) return false;
    uint8_t buf[kTlvBufCap];
    const size_t n = tlvSerialize(cfg_, buf, sizeof buf);
    ok = n > 0 && g_prefs.putBytes(kKeyTlv, buf, n) == n &&
         g_prefs.putBytes(kKeyPin, cfg_.pin, sizeof(cfg_.pin) - 1) == sizeof(cfg_.pin) - 1 &&
         g_prefs.putBytes(kKeyMount, cfg_.mountQuat, sizeof(cfg_.mountQuat)) ==
             sizeof(cfg_.mountQuat) &&
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
