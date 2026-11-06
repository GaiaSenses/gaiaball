#include "LedController.h"

#include <Arduino.h>

namespace gaia {

bool LedController::begin(ConfigStore &store) {
  const GaiaConfig &cfg = store.get();
  soundPin_ = cfg.soundLedPin;
  co2Pin_ = cfg.co2LedPin;
  // GPIO2 is a strapping pin: always drive LOW first (legacy behavior), never HIGH at boot.
  digitalWrite(soundPin_, LOW);
  pinMode(soundPin_, OUTPUT);
  digitalWrite(soundPin_, LOW);
  digitalWrite(co2Pin_, LOW);
  pinMode(co2Pin_, OUTPUT);
  digitalWrite(co2Pin_, LOW);
  began_ = true;
  return true;
}

void LedController::update(uint32_t /*nowMs*/, uint16_t realPpm, uint16_t alertPpm) {
  if (!began_) return;
  digitalWrite(co2Pin_, realPpm > alertPpm ? HIGH : LOW);
  digitalWrite(soundPin_, prio_.resolved() ? HIGH : LOW);
}

}  // namespace gaia
