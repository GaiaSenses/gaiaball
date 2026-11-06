#pragma once
#include <cstdint>

namespace gaia {

// Arbitrates the single physical LED between independent sources. Each source
// holds its own on/off state; the level is that of the highest-priority source
// that is currently ON (Error > SoundEvent > Co2Alert > BleCommand).
class LedPriority {
 public:
  enum class Source : uint8_t { BleCommand = 0, Co2Alert = 1, SoundEvent = 2, Error = 3 };

  void set(Source s, bool on) {
    uint8_t bit = (uint8_t)(1u << (uint8_t)s);
    if (on) mask_ |= bit; else mask_ &= (uint8_t)~bit;
  }

  // Physical LED level. Because the output is binary, "highest ON source wins"
  // reduces to "any source ON"; with none on the LED is off.
  bool resolved() const { return mask_ != 0; }

 private:
  uint8_t mask_ = 0;
};

}  // namespace gaia
