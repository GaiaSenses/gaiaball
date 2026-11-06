#pragma once
#include <stddef.h>
#include <stdint.h>

namespace gaia {

// Layout of the CO2 sensor UART frame. Protocol of the "SC8" is unconfirmed,
// so everything is configurable. Header is always 0x42 0x4D.
//  checksumVariant 0: sum of bytes [0 .. frameLen-3] (excludes the checksum)
//  checksumVariant 1: sum of bytes [0 .. frameLen-2] (legacy firmware)
// The checksum byte sits at index frameLen-1.
struct Co2FrameSpec {
  uint8_t frameLen = 16;
  uint8_t ppmHiIndex = 6;
  uint8_t checksumVariant = 0;
};

// Byte-at-a-time parser. On a failed frame it rescans the buffered bytes for
// the next header instead of discarding them, so it always resyncs.
class Co2FrameParser {
 public:
  enum class Result : uint8_t { None, Frame, ChecksumError };
  static constexpr size_t kMaxFrame = 32;

  Co2FrameParser() { configure(Co2FrameSpec()); }
  // Out-of-range values are clamped; resets the stream state (counters kept).
  void configure(const Co2FrameSpec &spec);
  Result feed(uint8_t byte);
  uint16_t ppm() const { return ppm_; }
  uint32_t frameCount() const { return frames_; }
  uint32_t errorCount() const { return errors_; }

 private:
  Co2FrameSpec spec_;
  uint8_t buf_[kMaxFrame];
  size_t n_ = 0;
  uint16_t ppm_ = 0;
  uint32_t frames_ = 0, errors_ = 0;

  void dropFront(size_t k);
  void alignToHeader();
  bool checksumOk() const;
};

}  // namespace gaia
