#include "LedController.h"

#include <Arduino.h>

namespace gaia {

namespace {
void initLowOutput(uint8_t pin) {
  // GPIO2 is a strapping pin: always drive LOW first (legacy behavior), never HIGH
  // at boot. Harmless for the other pin, so applied uniformly.
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}
}  // namespace

bool LedController::begin(ConfigStore &store) {
  const GaiaConfig &cfg = store.get();
  soundPin_ = cfg.soundLedPin;
  co2Pin_ = cfg.co2LedPin;
  initLowOutput(soundPin_);
  initLowOutput(co2Pin_);
  began_ = true;
  return true;
}

void LedController::update(uint32_t /*nowMs*/, uint16_t realPpm, uint16_t alertPpm) {
  if (!began_) return;
  digitalWrite(co2Pin_, realPpm > alertPpm ? HIGH : LOW);
  digitalWrite(soundPin_, soundOn_ ? HIGH : LOW);
}

}  // namespace gaia
