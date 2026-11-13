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
//    on G's measured angular speed with navStartDegS / navStopDegS / navStopMs.
//  - joystick (navMode 2): the tilt (axis-angle of q) commands a speed S(theta); the
//    settle machine runs on that COMMANDED speed with joyStartDegSX10 / joyStopDegSX10
//    (/10) and navStopMs, and G is integrated only while not settled (frozen at rest, so
//    G's own speed could never restart it). The reported speed is 0 while settled.
//  - off (navMode 0, or any unknown value): update() does nothing; keepaliveDue() holds.
//
// Reset to G = identity with the settle machine at rest: on the first sample, when the
// sample's zeroEpoch changes (zero, mounting or orientation-mode change on the IMU
// side), and when configure() sees a navigation field change (mode, reference point,
// joystick law or settle parameters). The first sample after a reset has dt = 0 (no
// speed, no integration). dt between samples comes from their uint32 micros() stamps,
// wrap-safe.
class NavEngine {
 public:
  // Without an IMU sample for longer than this, the 1 Hz keepalive takes over.
  static constexpr uint32_t kKeepaliveSilenceMs = 500;

  // Picks up the navigation fields of cfg plus imuNotifyHz (for the divisor). Acts only
  // on real changes, so it is safe to call on every config apply: a navigation field
  // change resets the motion (see above); imuNotifyHz / navNotifyHz recompute the
  // divisor and restart its count (the next sample is due). R0 is recomputed only when
  // the reference point changes.
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
  // Instantaneous speed (measured in mirror, commanded in joystick) above the stop
  // threshold: NavPacket bit2, no hold time.
  bool moving() const { return moving_; }
  // Reported speed: mirror = measured; joystick = commanded, 0 while settled.
  float speedDegS() const { return speed_; }
  uint8_t flags() const;

 private:
  // The configuration fields that shape the motion; any change resets it.
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

  bool sampleSeen_ = false;
  uint32_t lastSampleMs_ = 0;
};

}  // namespace gaia
