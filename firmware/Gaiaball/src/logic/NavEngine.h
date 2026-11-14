#pragma once
#include <stdint.h>

#include "GaiaConfig.h"
#include "MotionSettle.h"
#include "NavMath.h"
#include "Packets.h"
#include "QuatMath.h"

namespace gaia {

// NavPacket divisor: one packet every N IMU samples, N = max(1, round(imuHz / navHz)).
// Counting samples keeps the spacing regular, and N >= 1 means the packet rate never
// exceeds the IMU rate. A zero rate counts as "no divisor" (N = 1).
uint8_t navDivisor(uint8_t imuHz, uint8_t navHz);

// Globe navigation driven by the IMU sample stream (see NavMath.h for the frames and
// docs/protocol.md for the NavPacket). Pure logic, no I/O: the App task owns one, feeds
// it every IMU sample (before the IMU notify throttle) and sends what it builds.
//
//  - mirror (navMode 1): the globe G is the ball's orientation; the settle machine runs
//    on G's measured angular speed with navStartDegS / navStopDegS / navStopMs. That
//    speed is taken over a ~100 ms window (kMirrorWindowUs): the angle between the
//    current G and the newest G at least that old, over the real time between them,
//    so an 8-12 Hz hand tremor averages out and a hand-held ball can settle (right
//    after a reset, the oldest sample held stands in). While settled, a drift of
//    kMirrorDriftStartDeg from the pose captured when it settled also leaves settled,
//    so a slow, deliberate rotation below navStartDegS still reaches the clients.
//  - joystick (navMode 2): the tilt (axis-angle of q) commands a speed S(theta); the
//    settle machine runs on that COMMANDED speed with joyStartDegSX10 / joyStopDegSX10
//    (/10) and navStopMs, and G is integrated only while not settled (frozen at rest, so
//    G's own speed could never restart it). The reported speed is 0 while settled.
//  - off (navMode 0, or any unknown value): update() does nothing; keepaliveDue() holds.
//
// Reset to G = identity with the settle machine at rest: on the first sample, when the
// sample's zeroEpoch changes (zero, mounting or orientation-mode change on the IMU
// side), and when configure() sees a new navMode or reference point. The joystick law
// and the settle thresholds / hold time apply live, without moving the globe (a
// sensitivity slider must not snap it back). The first sample after a reset has dt = 0
// (no speed, no integration). dt between samples comes from their uint32 micros()
// stamps, wrap-safe.
class NavEngine {
 public:
  // Without an IMU sample for longer than this, the 1 Hz keepalive takes over.
  static constexpr uint32_t kKeepaliveSilenceMs = 500;
  // Mirror speed window: the speed compares G with the newest sample at least this old.
  static constexpr uint32_t kMirrorWindowUs = 100000;
  // Past mirror poses kept for the window: 100 ms at the top IMU rate (200 Hz) is 20
  // samples back; 32 leaves room for timing jitter.
  static constexpr uint8_t kMirrorRingSize = 32;
  // Mirror, while settled: this much drift from the settle pose leaves settled.
  // Provisional, to be tuned on the bench.
  static constexpr float kMirrorDriftStartDeg = 3.0f;

  // Picks up the navigation fields of cfg plus imuNotifyHz (for the divisor). Acts only
  // on real changes, so it is safe to call on every config apply: a mode or reference
  // change resets the motion (see above); joystick and settle parameters apply live;
  // imuNotifyHz / navNotifyHz recompute the divisor and restart its count (the next
  // sample is due). R0 is recomputed only when the reference point changes.
  void configure(const GaiaConfig &cfg);

  // One IMU sample (q = the treated orientation, quatValid = kImuQuatValid, nowMs =
  // millis() for the settle hold time). Returns true when a NavPacket is due: every
  // divisor() samples, counted whether or not anyone is subscribed. Mode off returns
  // false and leaves the navigation state untouched.
  bool update(uint32_t tUs, const Quat &q, bool quatValid, uint32_t zeroEpoch, uint32_t nowMs);

  // The packet for the last update(): pose from navPoseFromGlobe(G, R0), timestamp of
  // that sample, flags valid / settled / moving.
  NavPacket packet(uint16_t seq) const;
  // valid = 0 and every pose field 0; mode = the current mode.
  NavPacket keepalivePacket(uint16_t seq, uint32_t tUs) const;
  // Navigation off, no sample ever, or none for more than kKeepaliveSilenceMs.
  bool keepaliveDue(uint32_t nowMs) const;

  uint8_t mode() const { return cfg_.mode; }
  uint8_t divisor() const { return divisor_; }
  const Quat &globe() const { return g_; }
  bool settled() const { return settle_.settled(); }
  // Reported speed above the active stop threshold: NavPacket bit2, no hold time. A
  // settled joystick reports speed 0, so it never reads as moving.
  bool moving() const { return moving_; }
  // Reported speed: mirror = measured; joystick = commanded, 0 while settled.
  float speedDegS() const { return speed_; }
  uint8_t flags() const;

 private:
  // The configuration fields that shape the motion. A change of mode or reference
  // resets it; the others apply live.
  struct MotionCfg {
    uint8_t mode = 0;
    int32_t refLatE6 = 0, refLonE6 = 0;
    uint16_t joyMaxSpeedDegS = 0;
    uint8_t joyFullAngleDeg = 0, joyCurveX10 = 0;
    uint8_t navStartDegS = 0, navStopDegS = 0;
    uint16_t navStopMs = 0;
    uint16_t joyStartDegSX10 = 0, joyStopDegSX10 = 0;
  };
  static bool sameMotion(const MotionCfg &a, const MotionCfg &b);
  void resetMotion();
  // Mirror: records (g_, tUs) and returns the windowed speed of g_.
  float mirrorSpeedDegS(uint32_t tUs);

  struct PoseSample {
    Quat q;
    uint32_t tUs;
  };

  bool configured_ = false;
  MotionCfg cfg_{};
  uint8_t imuHz_ = 0, navHz_ = 0;
  uint8_t divisor_ = 1;
  uint8_t count_ = 0;
  JoyParams joy_{90.f, 60.f, 3.f};
  float stopDegS_ = 5.f;
  Quat r0_{1, 0, 0, 0};

  MotionSettle settle_;
  Quat g_{1, 0, 0, 0};
  bool haveEpoch_ = false;
  uint32_t epoch_ = 0;
  bool haveT_ = false;
  uint32_t lastTUs_ = 0;
  float speed_ = 0.f;
  bool moving_ = false;
  bool valid_ = false;

  // Mirror only: past poses for the speed window (ringHead_ = next write) and the pose
  // the ball last settled at (for the drift rule).
  PoseSample ring_[kMirrorRingSize] = {};
  uint8_t ringHead_ = 0;
  uint8_t ringCount_ = 0;
  bool haveSettlePose_ = false;
  Quat settlePose_{1, 0, 0, 0};

  bool sampleSeen_ = false;
  uint32_t lastSampleMs_ = 0;
};

}  // namespace gaia
