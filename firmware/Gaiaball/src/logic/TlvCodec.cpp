#include "TlvCodec.h"
#include <string.h>

namespace gaia {
namespace {

enum class Kind : uint8_t { U8, U16, U32, Str, Pin };

struct Reg {
  uint8_t id;
  Kind kind;
  uint32_t min, max;  // numeric range (inclusive); unused for strings
};

constexpr uint32_t kAnyU8 = 0xFF, kAnyU16 = 0xFFFF, kAnyU32 = 0xFFFFFFFFu;
constexpr uint32_t kMaxGpio = 33;  // ESP32 output-capable GPIOs: 0..33
constexpr size_t kNameMax = 16;
constexpr size_t kPinLen = 4;

constexpr Reg kRegs[] = {
    {0x01, Kind::U8, 0, kAnyU8},      {0x02, Kind::U8, 10, 60},
    {0x03, Kind::U8, 0, 100},         {0x10, Kind::U8, 0, kAnyU8},
    {0x11, Kind::U16, 1, 1000},       {0x12, Kind::U16, 1, 1000},
    {0x13, Kind::U16, 0, kAnyU16},    {0x14, Kind::U8, 0, kAnyU8},
    {0x15, Kind::U32, 0, kAnyU32},    {0x20, Kind::U8, 0, kAnyU8},
    {0x21, Kind::U8, 0, kAnyU8},      {0x22, Kind::U16, 0, kAnyU16},
    {0x23, Kind::U16, 0, kAnyU16},    {0x24, Kind::U16, 0, kAnyU16},
    {0x25, Kind::U16, 0, kAnyU16},    {0x26, Kind::U8, 0, kAnyU8},
    {0x30, Kind::U8, 0, kMaxGpio},    {0x31, Kind::U8, 0, kMaxGpio},
    {0x32, Kind::U16, 0, kAnyU16},    {0x40, Kind::Str, 0, 0},
    {kTlvPin, Kind::Pin, 0, 0},
};

const Reg *findReg(uint8_t id) {
  for (const Reg &r : kRegs)
    if (r.id == id) return &r;
  return nullptr;
}

// Pointer to the numeric field for `id` (nullptr for strings / unknown).
void *numField(GaiaConfig &c, uint8_t id) {
  switch (id) {
    case 0x01: return &c.orientationMode;
    case 0x02: return &c.imuNotifyHz;
    case 0x03: return &c.slerpAmount;
    case 0x10: return &c.audioEnabled;
    case 0x11: return &c.audioFastAlphaX1000;
    case 0x12: return &c.audioSlowAlphaX1000;
    case 0x13: return &c.debounceMs;
    case 0x14: return &c.potControlsThreshold;
    case 0x15: return &c.thresholdFixed;
    case 0x20: return &c.co2Enabled;
    case 0x21: return &c.co2ChecksumVariant;
    case 0x22: return &c.co2StaleTimeoutMs;
    case 0x23: return &c.virtualPeakPpm;
    case 0x24: return &c.virtualDelayMs;
    case 0x25: return &c.virtualHoldMs;
    case 0x26: return &c.co2RawLogMode;
    case 0x30: return &c.soundLedPin;
    case 0x31: return &c.co2LedPin;
    case 0x32: return &c.co2AlertPpm;
    default: return nullptr;
  }
}

size_t numSize(Kind k) { return k == Kind::U8 ? 1 : k == Kind::U16 ? 2 : 4; }

uint32_t readLe(const uint8_t *p, size_t n) {
  uint32_t v = 0;
  for (size_t i = 0; i < n; i++) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}

void writeLe(uint8_t *p, size_t n, uint32_t v) {
  for (size_t i = 0; i < n; i++) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

void storeNum(void *f, Kind k, uint32_t v) {
  switch (k) {
    case Kind::U8: *static_cast<uint8_t *>(f) = static_cast<uint8_t>(v); break;
    case Kind::U16: *static_cast<uint16_t *>(f) = static_cast<uint16_t>(v); break;
    default: *static_cast<uint32_t *>(f) = v; break;
  }
}

uint32_t loadNum(const void *f, Kind k) {
  switch (k) {
    case Kind::U8: return *static_cast<const uint8_t *>(f);
    case Kind::U16: return *static_cast<const uint16_t *>(f);
    default: return *static_cast<const uint32_t *>(f);
  }
}

// Validates one record's value; returns true if acceptable.
bool validValue(const Reg &r, const uint8_t *v, size_t len) {
  switch (r.kind) {
    case Kind::Str:
      if (len < 1 || len > kNameMax) return false;
      for (size_t i = 0; i < len; i++)
        if (v[i] < 0x20 || v[i] > 0x7E) return false;
      return true;
    case Kind::Pin:
      if (len != kPinLen) return false;
      for (size_t i = 0; i < len; i++)
        if (v[i] < '0' || v[i] > '9') return false;
      return true;
    default: {
      if (len != numSize(r.kind)) return false;
      uint32_t x = readLe(v, len);
      return x >= r.min && x <= r.max;
    }
  }
}

}  // namespace

size_t tlvSerialize(const GaiaConfig &cfg, uint8_t *out, size_t cap) {
  GaiaConfig &c = const_cast<GaiaConfig &>(cfg);  // read-only use via numField
  size_t pos = 0;
  for (const Reg &r : kRegs) {
    if (r.id == kTlvPin) continue;  // PIN never leaves the device
    size_t vlen;
    if (r.kind == Kind::Str) {
      vlen = strnlen(c.bleName, kNameMax);
    } else {
      vlen = numSize(r.kind);
    }
    if (pos + 2 + vlen > cap) return 0;
    out[pos] = r.id;
    out[pos + 1] = static_cast<uint8_t>(vlen);
    if (r.kind == Kind::Str) {
      memcpy(out + pos + 2, c.bleName, vlen);
    } else {
      writeLe(out + pos + 2, vlen, loadNum(numField(c, r.id), r.kind));
    }
    pos += 2 + vlen;
  }
  return pos;
}

bool tlvApply(GaiaConfig &cfg, const uint8_t *data, size_t len) {
  if (len != 0 && data == nullptr) return false;
  // Pass 1: validate everything; cfg untouched.
  for (size_t pos = 0; pos < len;) {
    if (len - pos < 2) return false;
    const Reg *r = findReg(data[pos]);
    size_t vlen = data[pos + 1];
    if (r == nullptr || vlen > len - pos - 2) return false;
    if (!validValue(*r, data + pos + 2, vlen)) return false;
    pos += 2 + vlen;
  }
  // Pass 2: commit (cannot fail).
  for (size_t pos = 0; pos < len;) {
    const Reg *r = findReg(data[pos]);
    size_t vlen = data[pos + 1];
    const uint8_t *v = data + pos + 2;
    if (r->kind == Kind::Str) {
      memset(cfg.bleName, 0, sizeof cfg.bleName);
      memcpy(cfg.bleName, v, vlen);
    } else if (r->kind == Kind::Pin) {
      memcpy(cfg.pin, v, kPinLen);
      cfg.pin[kPinLen] = 0;
    } else {
      storeNum(numField(cfg, r->id), r->kind, readLe(v, vlen));
    }
    pos += 2 + vlen;
  }
  return true;
}

}  // namespace gaia
