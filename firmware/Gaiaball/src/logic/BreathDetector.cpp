#include "BreathDetector.h"

#include "AudioFeatures.h"

namespace gaia {

void BreathDetector::configure(const BreathParams &p) {
  const bool modeChanged = (p.mode != p_.mode);
  p_ = p;
  if (p_.rearmDb > 20) p_.rearmDb = 20;
  rearmRatio_ = dbToAmplitudeRatio((float)p_.rearmDb);
  if (modeChanged) {
    fast_ = slow_ = 0.0f;
    seeded_ = false;
    armed_ = true;
    hasFired_ = false;
    qualifying_ = false;
    status_ = 0;
  }
}

float BreathDetector::triggerLevel() const {
  return p_.mode == BreathMode::Adaptive ? slow_ + p_.thresholdCounts : p_.thresholdCounts;
}

bool BreathDetector::update(const float level[2], uint32_t nowMs, const BreathGate &gate) {
  const float thr = p_.thresholdCounts;
  bool over;
  bool belowRearm;
  if (p_.mode == BreathMode::Adaptive) {
    const float in = level[0] > level[1] ? level[0] : level[1];
    if (!seeded_) {
      fast_ = slow_ = in;
      seeded_ = true;
    } else {
      fast_ += p_.fastAlpha * (in - fast_);
      slow_ = p_.slowAlpha * slow_ + (1.0f - p_.slowAlpha) * in;
    }
    const float diff = fast_ - slow_;
    over = diff > thr;
    belowRearm = diff < thr * rearmRatio_;
  } else {
    over = level[0] > thr || level[1] > thr;
    const float r = thr * rearmRatio_;
    belowRearm = level[0] < r && level[1] < r;
  }
  const bool coherent = over && gate.coherence >= p_.coherenceMax;
  status_ = (uint8_t)((over ? kBreathOver : 0) | (coherent ? kBreathCoherent : 0) |
                      (gate.moving ? kBreathMoving : 0));
  if (!armed_) {
    qualifying_ = false;
    if (belowRearm) armed_ = true;
    return false;
  }
  if (!over || coherent || (p_.stillOnly && gate.moving)) {  // any break restarts the duration
    qualifying_ = false;
    return false;
  }
  if (!qualifying_) {
    qualifying_ = true;
    qualifyStartMs_ = nowMs;
  }
  if ((uint32_t)(nowMs - qualifyStartMs_) < p_.minDurationMs ||
      (hasFired_ && (uint32_t)(nowMs - lastFireMs_) < p_.debounceMs)) {
    status_ = (uint8_t)(status_ | kBreathWaiting);
    return false;
  }
  armed_ = false;
  hasFired_ = true;
  lastFireMs_ = nowMs;
  qualifying_ = false;
  return true;
}

}  // namespace gaia
