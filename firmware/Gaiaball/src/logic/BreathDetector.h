#pragma once
#include <stdint.h>

namespace gaia {

enum class BreathMode : uint8_t { Absolute = 0, Adaptive = 1 };

struct BreathParams {
  BreathMode mode = BreathMode::Absolute;
  // Absolute: the line. Adaptive: how far above the slow baseline the fast envelope must rise.
  float thresholdCounts = 0.0f;
  uint8_t rearmDb = 6;  // 0..20 whole dB below the line before the detector re-arms
  uint32_t debounceMs = 3000;
  float fastAlpha = 0.25f;   // adaptive only: weight of the new sample
  float slowAlpha = 0.995f;  // adaptive only: near 1 = slow baseline
  // Discriminators (v10): what tells a breath from other loud low sound. Each is off at
  // its default here (the config defaults turn them on, see GaiaConfig).
  // Coherence: fire only while the left/right coherence is BELOW this (a breath is
  // turbulence, different at each microphone; sound from a distance is the same wave at
  // both). Anything above 1 never blocks.
  float coherenceMax = 2.0f;
  // Duration: the fire condition (over the line, not coherent) must hold without a break
  // for this long. Speech pauses between syllables and a knock is over in tens of
  // milliseconds; a breath is sustained. It delays every fire by the same amount.
  uint32_t minDurationMs = 0;
  // Stillness: artists blow only on a ball held still. When set, nothing fires while the
  // IMU says the ball is moving (StillnessDetector: turned, knocked or shaken, tremor
  // excluded), and a moving stretch breaks the duration.
  bool stillOnly = false;
};

// Per-update inputs from outside the audio levels.
struct BreathGate {
  float coherence = 0.0f;  // AudioLevels::coherence, 0..1
  bool moving = false;     // StillnessDetector
};

// status() bits, published on the SensorsPacket (breathGate) for the config-app.
constexpr uint8_t kBreathOver = 1u << 0;      // level over the fire line
constexpr uint8_t kBreathCoherent = 1u << 1;  // over, but held back by the coherence
constexpr uint8_t kBreathMoving = 1u << 2;    // the IMU says the ball moves (blocks if stillOnly)
constexpr uint8_t kBreathWaiting = 1u << 3;   // qualifies, waiting for minDurationMs or the debounce

// Breath ("sopro") detector on linear level values (counts) of the two microphones.
//  Absolute: fires when EITHER level exceeds the line.
//  Adaptive: on max(L, R), fires when fast - slow exceeds the threshold
//            (fast/slow EMAs seeded by the first update).
// After a fire it stays disarmed until the level (absolute) or fast - slow (adaptive)
// falls below threshold * 10^(-rearmDb/20), so one sustained breath yields one event.
// A fire also needs the discriminators of BreathParams: coherence below coherenceMax,
// the ball still (stillOnly), and the whole condition held for minDurationMs. Debounce is wrap-safe. Not thread-safe: use from a single task.
class BreathDetector {
 public:
  // Resets the state when the mode changes; otherwise keeps it so live re-tuning does
  // not cause a spurious fire.
  void configure(const BreathParams &p);
  // True on the firing edge only.
  bool update(const float level[2], uint32_t nowMs, const BreathGate &gate);
  bool update(const float level[2], uint32_t nowMs) { return update(level, nowMs, BreathGate()); }
  // 1 from the fire until the re-arm.
  bool recognized() const { return !armed_; }
  // The fire line in counts: absolute, thresholdCounts; adaptive, slow baseline +
  // thresholdCounts.
  float triggerLevel() const;
  // kBreath* bits as of the last update.
  uint8_t status() const { return status_; }

 private:
  BreathParams p_;
  float rearmRatio_ = 0.70794576f;  // 10^(-3/20)
  bool qualifying_ = false;
  uint32_t qualifyStartMs_ = 0;
  uint8_t status_ = 0;
  float fast_ = 0.0f;
  float slow_ = 0.0f;
  bool seeded_ = false;
  bool armed_ = true;
  bool hasFired_ = false;
  uint32_t lastFireMs_ = 0;
};

}  // namespace gaia
