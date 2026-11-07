#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gaia {

// Senseair S8 Modbus client logic (host-testable, no I/O).
//
// The board's CO2 sensor (KiCAD value "Sensor_CO2_SC8", footprint Senseair_S8_Down)
// is a Senseair S8: a Modbus RTU slave at 9600 8N1 that NEVER transmits on its own.
// The master must poll it; a passive frame listener (the legacy firmware's approach)
// reads nothing from this sensor, ever.
//
// Reference: Senseair "Modbus on Senseair S8" (document SE-0119). CO2 ppm lives in
// input register IR4 (zero-based address 0x0003), read with function 0x04 to the
// "any sensor" address 0xFE. CRC is standard Modbus-RTU CRC-16 (poly 0xA001,
// init 0xFFFF), transmitted low byte first.

// Standard Modbus-RTU CRC-16 over `data`. Verified against the SE-0119 example
// frame FE 04 00 00 00 04 -> 0xC6E5 (sent on the wire as E5 C6).
uint16_t modbusCrc16(const uint8_t *data, size_t len);

// "Read CO2" request: FE 04 00 03 00 01 D5 C5 (addr FE, fn 04, IR4, 1 register).
constexpr size_t kS8RequestLen = 8;
extern const uint8_t kS8ReadCo2Request[kS8RequestLen];

// Incremental parser for the 7-byte response FE 04 02 <hi> <lo> <crcLo> <crcHi>.
// Byte-at-a-time resync: on any mismatch the offending byte is re-examined as a
// potential frame start, so a response embedded in line noise is still found.
class S8ResponseParser {
 public:
  enum class Result : uint8_t { NeedMore, Frame, Error };

  // Feeds one received byte. Returns Frame when a CRC-valid response completes
  // (ppm() is then valid); Error once per discarded candidate frame.
  Result feed(uint8_t byte);

  uint16_t ppm() const { return ppm_; }
  uint32_t frameCount() const { return frames_; }
  uint32_t errorCount() const { return errors_; }
  void reset() { fill_ = 0; }

 private:
  static constexpr size_t kResponseLen = 7;
  Result scan(uint8_t byte);

  uint8_t buf_[kResponseLen] = {0};
  size_t fill_ = 0;
  uint16_t ppm_ = 0;
  uint32_t frames_ = 0, errors_ = 0;
};

}  // namespace gaia
