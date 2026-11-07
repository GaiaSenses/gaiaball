#include "Co2FrameParser.h"

namespace gaia {

static const uint8_t kHdr0 = 0x42, kHdr1 = 0x4D;

void Co2FrameParser::configure(const Co2FrameSpec &spec) {
  spec_ = spec;
  if (spec_.frameLen < 4) spec_.frameLen = 4;
  if (spec_.frameLen > kMaxFrame) spec_.frameLen = kMaxFrame;
  if (spec_.checksumVariant > 1) spec_.checksumVariant = 0;
  // ppm (2 bytes) must lie after the header and before the checksum byte.
  if (spec_.ppmHiIndex < 2) spec_.ppmHiIndex = 2;
  if (spec_.ppmHiIndex > spec_.frameLen - 3) spec_.ppmHiIndex = spec_.frameLen - 3;
  n_ = 0;
}

void Co2FrameParser::dropFront(size_t k) {
  for (size_t i = k; i < n_; i++) buf_[i - k] = buf_[i];
  n_ -= k;
}

// Drops leading bytes until the buffer starts with a (possibly partial)
// header: empty, "42", or "42 4D ...".
void Co2FrameParser::alignToHeader() {
  while (n_ > 0) {
    if (buf_[0] != kHdr0) { dropFront(1); continue; }
    if (n_ >= 2 && buf_[1] != kHdr1) { dropFront(1); continue; }
    return;
  }
}

bool Co2FrameParser::checksumOk() const {
  size_t last = spec_.frameLen - 1;           // checksum index
  size_t end = spec_.checksumVariant == 0 ? last - 1 : last;  // inclusive-exclusive
  uint8_t sum = 0;
  for (size_t i = 0; i < end; i++) sum += buf_[i];
  return sum == buf_[last];
}

Co2FrameParser::Result Co2FrameParser::feed(uint8_t byte) {
  buf_[n_++] = byte;
  alignToHeader();
  if (n_ < spec_.frameLen) return Result::None;

  if (checksumOk()) {
    ppm_ = (uint16_t)((buf_[spec_.ppmHiIndex] << 8) | buf_[spec_.ppmHiIndex + 1]);
    frames_++;
    n_ = 0;
    return Result::Frame;
  }
  errors_++;
  // Skip only this false header; bytes after it may hold a real frame start.
  dropFront(1);
  alignToHeader();
  return Result::ChecksumError;
}

}  // namespace gaia
