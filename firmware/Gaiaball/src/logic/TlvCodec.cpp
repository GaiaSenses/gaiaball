#include "TlvCodec.h"
#include <string.h>

namespace gaia {
namespace {

enum class Kind : uint8_t { U8, U16, U32, Str, Pin, MagCal };

struct Reg {
  uint8_t id;
  Kind kind;
  uint32_t min, max;  // numeric range (inclusive); unused for strings
};

constexpr uint32_t kAnyU8 = 0xFF, kAnyU16 = 0xFFFF, kAnyU32 = 0xFFFFFFFFu;
constexpr uint32_t kMaxGpio = 33;  // ESP32 output-capable GPIOs: 0..33 (see isValidOutputGpio)
constexpr size_t kNameMax = 16;
constexpr size_t kPinLen = 4;
// Magnetometer calibration record: 3 x int16 hard iron + 9 x int16 soft iron.
constexpr size_t kMagCalLen = 24;

constexpr Reg kRegs[] = {
    {0x01, Kind::U8, 0, kAnyU8},      {0x02, Kind::U8, 10, 200},
    {0x03, Kind::U8, 0, 100},         {0x04, Kind::U16, 0, 1000},
    // IMU front-end, fusion, bias tracker and output smoothing (v7).
    {0x05, Kind::U8, 0, 7},           {0x06, Kind::U8, 0, 3},
    {0x07, Kind::U16, 0, 500},        {0x08, Kind::U8, 0, 90},
    {0x09, Kind::U8, 0, 90},          {0x0A, Kind::U8, 0, 60},
    {0x0B, Kind::U16, 1, 2000},       {0x0C, Kind::U16, 1, 5000},
    {0x0D, Kind::U8, 1, 60},          {0x0E, Kind::U8, 0, 2},
    {0x0F, Kind::U16, 1, 5000},
    {0x10, Kind::U8, 0, kAnyU8},
    {0x11, Kind::U16, 1, 1000},       {0x12, Kind::U16, 1, 1000},
    {0x13, Kind::U16, 0, kAnyU16},    {0x14, Kind::U8, 0, kAnyU8},
    {0x15, Kind::U32, 0, kAnyU32},    {0x16, Kind::U8, 0, 1},
    {0x17, Kind::U16, 1, 7000},       {0x18, Kind::U16, 10, 2000},
    {0x19, Kind::U16, 1, 7000},       {0x1A, Kind::U16, 10, 2000},
    {0x1B, Kind::U16, 1, 1000},       {0x1F, Kind::U8, 0, 1},
    {0x1C, Kind::U16, 0, 10000},      {0x1D, Kind::U8, 0, 100},
    {0x1E, Kind::MagCal, 0, 0},       {0x20, Kind::U8, 0, kAnyU8},
    {0x22, Kind::U16, 0, kAnyU16},
    {0x23, Kind::U16, 0, kAnyU16},    {0x24, Kind::U16, 0, kAnyU16},
    {0x25, Kind::U16, 0, kAnyU16},    {0x26, Kind::U8, 0, kAnyU8},
    {0x28, Kind::U16, 1, 1000},
    {0x30, Kind::U8, 0, kMaxGpio},    {0x31, Kind::U8, 0, kMaxGpio},
    {0x32, Kind::U16, 0, kAnyU16},
    {0x34, Kind::U8, 0, 2},           {0x36, Kind::U8, 0, 23},
    {0x40, Kind::Str, 0, 0},
    {kTlvPin, Kind::Pin, 0, 0},       {0x50, Kind::U8, 0, 1},
};
// Reserved ids (never reuse): 0x21 (was co2ChecksumVariant) and 0x27 (was co2Protocol)
// — both removed once the SC8-CO2-5K datasheet fixed the frame format; 0x33 (was
// statusLedPin; removed with the legacy LED command).
// 0x35 (imuDetected) is read-only status appended by BleService::onConfigRead; it is
// deliberately NOT a register here, so a client write of it is an unknown id.

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
    case 0x04: return &c.accelLpAlphaX1000;
    case 0x05: return &c.imuDlpfMode;
    case 0x06: return &c.gyroFsSel;
    case 0x07: return &c.fusionGainX100;
    case 0x08: return &c.accelRejectionDeg;
    case 0x09: return &c.magRejectionDeg;
    case 0x0A: return &c.rejectionTimeoutS;
    case 0x0B: return &c.biasRestThDpsX100;
    case 0x0C: return &c.biasMaxDpsX100;
    case 0x0D: return &c.biasTauS;
    case 0x0E: return &c.quatSmoothMode;
    case 0x0F: return &c.oneEuroFcMinX100;
    case 0x1C: return &c.oneEuroBetaX1000;
    case 0x1D: return &c.accelHampelKX10;
    case 0x10: return &c.audioEnabled;
    case 0x11: return &c.audioFastAlphaX1000;
    case 0x12: return &c.audioSlowAlphaX1000;
    case 0x13: return &c.debounceMs;
    case 0x14: return &c.potControlsThreshold;
    case 0x15: return &c.thresholdFixed;
    case 0x16: return &c.audioBpfEnabled;
    case 0x17: return &c.audioHpHz;
    case 0x18: return &c.audioHpQX100;
    case 0x19: return &c.audioLpHz;
    case 0x1A: return &c.audioLpQX100;
    case 0x1B: return &c.potEmaAlphaX1000;
    case 0x1F: return &c.audioRawStream;
    case 0x20: return &c.co2Enabled;
    case 0x22: return &c.co2StaleTimeoutMs;
    case 0x23: return &c.virtualPeakPpm;
    case 0x24: return &c.virtualDelayMs;
    case 0x25: return &c.virtualHoldMs;
    case 0x26: return &c.co2RawLogMode;
    case 0x28: return &c.co2EmaAlphaX1000;
    case 0x30: return &c.soundLedPin;
    case 0x31: return &c.co2LedPin;
    case 0x32: return &c.co2AlertPpm;
    case 0x34: return &c.imuChip;
    case 0x36: return &c.magAxisRemap;
    case 0x50: return &c.serialLogEnabled;
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

// The magnetometer calibration record is the only multi-value register: 12 int16 in
// a fixed order (hard iron x/y/z, then the soft-iron matrix row-major).
void writeMagCal(uint8_t *out, const GaiaConfig &c) {
  for (int i = 0; i < 3; i++) writeLe(out + 2 * i, 2, (uint16_t)c.magHardIron[i]);
  for (int i = 0; i < 9; i++) writeLe(out + 6 + 2 * i, 2, (uint16_t)c.magSoftIron[i]);
}

void readMagCal(const uint8_t *v, GaiaConfig &c) {
  for (int i = 0; i < 3; i++) c.magHardIron[i] = (int16_t)readLe(v + 2 * i, 2);
  for (int i = 0; i < 9; i++) c.magSoftIron[i] = (int16_t)readLe(v + 6 + 2 * i, 2);
}

// Output-capable ESP32 GPIOs; excludes SPI-flash 6..11 and nonexistent 20, 24, 28..31.
bool isValidOutputGpio(uint8_t pin) {
  static const uint8_t kValid[] = {0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16,
                                   17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
  for (uint8_t p : kValid)
    if (p == pin) return true;
  return false;
}

// Validates one record's value; returns true if acceptable.
bool validValue(const Reg &r, const uint8_t *v, size_t len) {
  switch (r.kind) {
    case Kind::Str:
      if (len < 1 || len > kNameMax) return false;
      for (size_t i = 0; i < len; i++)
        if (v[i] < 0x20 || v[i] > 0x7E) return false;
      return true;
    case Kind::MagCal: {
      if (len != kMagCalLen) return false;
      // A zero on the soft-iron diagonal would silently zero the magnetometer and
      // make Quat9 unusable: refuse the record instead of storing it.
      for (int d = 0; d < 3; d++) {
        const size_t off = 6 + 2 * (size_t)(4 * d);  // soft[0], soft[4], soft[8]
        if ((int16_t)readLe(v + off, 2) == 0) return false;
      }
      return true;
    }
    case Kind::Pin:
      if (len != kPinLen) return false;
      for (size_t i = 0; i < len; i++)
        if (v[i] < '0' || v[i] > '9') return false;
      return true;
    default: {
      if (len != numSize(r.kind)) return false;
      uint32_t x = readLe(v, len);
      if (r.id == 0x30 || r.id == 0x31) return x <= 0xFF && isValidOutputGpio(static_cast<uint8_t>(x));
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
    } else if (r.kind == Kind::MagCal) {
      vlen = kMagCalLen;
    } else {
      vlen = numSize(r.kind);
    }
    if (pos + 2 + vlen > cap) return 0;
    out[pos] = r.id;
    out[pos + 1] = static_cast<uint8_t>(vlen);
    if (r.kind == Kind::Str) {
      memcpy(out + pos + 2, c.bleName, vlen);
    } else if (r.kind == Kind::MagCal) {
      writeMagCal(out + pos + 2, c);
    } else {
      writeLe(out + pos + 2, vlen, loadNum(numField(c, r.id), r.kind));
    }
    pos += 2 + vlen;
  }
  return pos;
}

bool tlvApply(GaiaConfig &cfg, const uint8_t *data, size_t len) {
  if (len != 0 && data == nullptr) return false;
  // Pass 1: validate everything; cfg untouched. Two cross-field rules are checked on
  // the RESULTING state: (a) the sound and CO2 LEDs must sit on distinct GPIOs (one
  // pin = one function); (b) the audio band-pass must be a real band (lpHz > hpHz).
  uint8_t ledPins[2] = {cfg.soundLedPin, cfg.co2LedPin};
  uint16_t hpHz = cfg.audioHpHz, lpHz = cfg.audioLpHz;
  for (size_t pos = 0; pos < len;) {
    if (len - pos < 2) return false;
    const Reg *r = findReg(data[pos]);
    size_t vlen = data[pos + 1];
    if (r == nullptr || vlen > len - pos - 2) return false;
    if (!validValue(*r, data + pos + 2, vlen)) return false;
    if (r->id == 0x30) ledPins[0] = data[pos + 2];
    if (r->id == 0x31) ledPins[1] = data[pos + 2];
    if (r->id == 0x17) hpHz = (uint16_t)readLe(data + pos + 2, 2);
    if (r->id == 0x19) lpHz = (uint16_t)readLe(data + pos + 2, 2);
    pos += 2 + vlen;
  }
  if (ledPins[0] == ledPins[1]) return false;
  if (lpHz <= hpHz) return false;
  // Pass 2: commit (cannot fail).
  for (size_t pos = 0; pos < len;) {
    const Reg *r = findReg(data[pos]);
    size_t vlen = data[pos + 1];
    const uint8_t *v = data + pos + 2;
    if (r->kind == Kind::Str) {
      memset(cfg.bleName, 0, sizeof cfg.bleName);
      memcpy(cfg.bleName, v, vlen);
    } else if (r->kind == Kind::MagCal) {
      readMagCal(v, cfg);
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

size_t tlvApplyLenient(GaiaConfig &cfg, const uint8_t *data, size_t len) {
  if (len != 0 && data == nullptr) return 0;
  size_t applied = 0;
  for (size_t pos = 0; pos + 2 <= len;) {
    const Reg *r = findReg(data[pos]);
    const size_t vlen = data[pos + 1];
    if (vlen > len - pos - 2) break;  // truncated record: nothing usable follows
    const uint8_t *v = data + pos + 2;
    pos += 2 + vlen;
    if (r == nullptr || r->kind == Kind::Pin) continue;
    if (!validValue(*r, v, vlen)) continue;
    if (r->kind == Kind::Str) {
      memset(cfg.bleName, 0, sizeof cfg.bleName);
      memcpy(cfg.bleName, v, vlen);
    } else if (r->kind == Kind::MagCal) {
      readMagCal(v, cfg);
    } else {
      storeNum(numField(cfg, r->id), r->kind, readLe(v, vlen));
    }
    applied++;
  }
  // Re-establish the cross-field invariants a partial apply may have broken.
  const GaiaConfig defaults;
  if (cfg.soundLedPin == cfg.co2LedPin) {
    cfg.soundLedPin = defaults.soundLedPin;
    cfg.co2LedPin = defaults.co2LedPin;
  }
  if (cfg.audioLpHz <= cfg.audioHpHz) {
    cfg.audioHpHz = defaults.audioHpHz;
    cfg.audioLpHz = defaults.audioLpHz;
  }
  return applied;
}

}  // namespace gaia
