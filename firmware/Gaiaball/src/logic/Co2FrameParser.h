#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gaia {

// Parser for the SC8-CO2-5K (Shenzhen Shenchen Technology, sold as "YYS SC8")
// active-output UART frame, per datasheet SC8-CO2-5K V1.4, section 8:
//   16 bytes: 0x42 0x4D <data...>; CO2 ppm = BYTE6*256 + BYTE7;
//   BYTE15 = low byte of (BYTE0 + BYTE1 + ... + BYTE14).
// Datasheet example: 42 4D 0C 51 09 A2 07 2B 01 35 05 81 20 08 00 AD -> 1835 ppm.
// The sensor emits one frame per second after power-on. Sending it ANY query
// stops the active output until the next power cycle, so the firmware never
// transmits on this UART.
//
// Byte-at-a-time parser. On a failed frame it rescans the buffered bytes for
// the next header instead of discarding them, so it always resyncs.
class Co2FrameParser {
 public:
  enum class Result : uint8_t { None, Frame, ChecksumError };
  static constexpr size_t kFrameLen = 16;
  static constexpr size_t kPpmHiIndex = 6;

  Result feed(uint8_t byte);
  uint16_t ppm() const { return ppm_; }
  uint32_t frameCount() const { return frames_; }
  uint32_t errorCount() const { return errors_; }

 private:
  uint8_t buf_[kFrameLen];
  size_t n_ = 0;
  uint16_t ppm_ = 0;
  uint32_t frames_ = 0, errors_ = 0;

  void dropFront(size_t k);
  void alignToHeader();
  bool checksumOk() const;
};

}  // namespace gaia
