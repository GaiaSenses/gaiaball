#include "S8Modbus.h"

namespace gaia {

uint16_t modbusCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc & 1u) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001) : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

const uint8_t kS8ReadCo2Request[kS8RequestLen] = {0xFE, 0x04, 0x00, 0x03,
                                                  0x00, 0x01, 0xD5, 0xC5};

S8ResponseParser::Result S8ResponseParser::scan(uint8_t byte) {
  // Header bytes are fixed (FE 04 02); checking them as they arrive keeps the
  // buffer aligned to a plausible frame start.
  static const uint8_t kHeader[3] = {0xFE, 0x04, 0x02};
  if (fill_ < sizeof kHeader && byte != kHeader[fill_]) {
    const bool hadCandidate = fill_ > 0;
    fill_ = 0;
    if (byte == kHeader[0]) buf_[fill_++] = byte;  // this byte may start a frame
    return hadCandidate ? Result::Error : Result::NeedMore;
  }
  buf_[fill_++] = byte;
  if (fill_ < kResponseLen) return Result::NeedMore;

  fill_ = 0;
  const uint16_t crc = modbusCrc16(buf_, kResponseLen - 2);
  const uint16_t rx = static_cast<uint16_t>(buf_[5]) | static_cast<uint16_t>(buf_[6]) << 8;
  if (crc != rx) return Result::Error;
  ppm_ = static_cast<uint16_t>(static_cast<uint16_t>(buf_[3]) << 8 | buf_[4]);
  return Result::Frame;
}

S8ResponseParser::Result S8ResponseParser::feed(uint8_t byte) {
  const Result r = scan(byte);
  if (r == Result::Frame) frames_++;
  if (r == Result::Error) errors_++;
  return r;
}

}  // namespace gaia
