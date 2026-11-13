#include "Co2FrameParser.h"

namespace gaia {

static const uint8_t kHdr0 = 0x42, kHdr1 = 0x4D;

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

// Datasheet formula: BYTE15 = low byte of sum(BYTE0..BYTE14).
bool Co2FrameParser::checksumOk() const {
  uint8_t sum = 0;
  for (size_t i = 0; i < kFrameLen - 1; i++) sum += buf_[i];
  return sum == buf_[kFrameLen - 1];
}

Co2FrameParser::Result Co2FrameParser::feed(uint8_t byte) {
  buf_[n_++] = byte;
  alignToHeader();
  if (n_ < kFrameLen) return Result::None;

  if (checksumOk()) {
    ppm_ = (uint16_t)((buf_[kPpmHiIndex] << 8) | buf_[kPpmHiIndex + 1]);
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
