#include "NavEngine.h"

#include "PacketCodec.h"

namespace gaia {

namespace {
const float kRadToDeg = 57.295779513082320876798f;
constexpr uint8_t kModeOff = 0;
constexpr uint8_t kModeMirror = 1;
constexpr uint8_t kModeJoystick = 2;
}  // namespace

uint8_t navDivisor(uint8_t imuHz, uint8_t navHz) {
  if (navHz == 0) return 1;
  // Integer round-half-up of imuHz / navHz; at most 255 (navHz >= 1).
  const unsigned n = ((unsigned)imuHz + navHz / 2u) / navHz;
  return n < 1 ? 1 : (uint8_t)n;
}

bool NavEngine::sameMotion(const MotionCfg &a, const MotionCfg &b) {
  return a.mode == b.mode && a.refLatE6 == b.refLatE6 && a.refLonE6 == b.refLonE6 &&
         a.joyMaxSpeedDegS == b.joyMaxSpeedDegS && a.joyFullAngleDeg == b.joyFullAngleDeg &&
         a.joyCurveX10 == b.joyCurveX10 && a.navStartDegS == b.navStartDegS &&
         a.navStopDegS == b.navStopDegS && a.navStopMs == b.navStopMs &&
         a.joyStartDegSX10 == b.joyStartDegSX10 && a.joyStopDegSX10 == b.joyStopDegSX10;
}

void NavEngine::configure(const GaiaConfig &cfg) {
  if (!configured_ || cfg.imuNotifyHz != imuHz_ || cfg.navNotifyHz != navHz_) {
    imuHz_ = cfg.imuNotifyHz;
    navHz_ = cfg.navNotifyHz;
    divisor_ = navDivisor(imuHz_, navHz_);
    count_ = 0;  // the next sample is due: no stale count from the old spacing
  }

  MotionCfg next;
  next.mode = cfg.navMode <= kModeJoystick ? cfg.navMode : kModeOff;
  next.refLatE6 = cfg.navRefLatE6;
  next.refLonE6 = cfg.navRefLonE6;
  next.joyMaxSpeedDegS = cfg.joyMaxSpeedDegS;
  next.joyFullAngleDeg = cfg.joyFullAngleDeg;
  next.joyCurveX10 = cfg.joyCurveX10;
  next.navStartDegS = cfg.navStartDegS;
  next.navStopDegS = cfg.navStopDegS;
  next.navStopMs = cfg.navStopMs;
  next.joyStartDegSX10 = cfg.joyStartDegSX10;
  next.joyStopDegSX10 = cfg.joyStopDegSX10;
  if (configured_ && sameMotion(next, cfg_)) return;

  const bool refChanged =
      !configured_ || next.refLatE6 != cfg_.refLatE6 || next.refLonE6 != cfg_.refLonE6;
  const bool modeChanged = !configured_ || next.mode != cfg_.mode;
  cfg_ = next;
  configured_ = true;
  if (refChanged) {
    r0_ = navRefRotation((float)((double)cfg_.refLatE6 * 1e-6),
                         (float)((double)cfg_.refLonE6 * 1e-6));
  }

  joy_.maxSpeedDegS = (float)cfg_.joyMaxSpeedDegS;
  joy_.fullAngleDeg = (float)cfg_.joyFullAngleDeg;
  joy_.curve = (float)cfg_.joyCurveX10 / 10.f;
  // Mirror settles on the measured globe speed, joystick on the commanded speed.
  float startDegS;
  if (cfg_.mode == kModeJoystick) {
    startDegS = (float)cfg_.joyStartDegSX10 / 10.f;
    stopDegS_ = (float)cfg_.joyStopDegSX10 / 10.f;
  } else {
    startDegS = (float)cfg_.navStartDegS;
    stopDegS_ = (float)cfg_.navStopDegS;
  }
  // Live: MotionSettle::configure keeps the current state, and the joystick law takes
  // effect on the next sample. Only a new mode or reference point restarts the globe.
  settle_.configure(startDegS, stopDegS_, cfg_.navStopMs);
  if (modeChanged || refChanged) resetMotion();
}

void NavEngine::resetMotion() {
  g_ = Quat{1, 0, 0, 0};
  settle_.reset();
  haveT_ = false;  // the next sample has no predecessor: dt = 0
  speed_ = 0.f;
  moving_ = false;
}

bool NavEngine::update(uint32_t tUs, const Quat &q, bool quatValid, uint32_t zeroEpoch,
                       uint32_t nowMs) {
  sampleSeen_ = true;
  lastSampleMs_ = nowMs;
  if (cfg_.mode == kModeOff) return false;

  if (!haveEpoch_ || zeroEpoch != epoch_) {
    haveEpoch_ = true;
    epoch_ = zeroEpoch;
    resetMotion();
  }
  const float dtS = haveT_ ? (float)(uint32_t)(tUs - lastTUs_) * 1e-6f : 0.f;
  haveT_ = true;
  lastTUs_ = tUs;

  float instDegS;
  if (cfg_.mode == kModeMirror) {
    const Quat prev = g_;
    g_ = q;
    instDegS = angularSpeedDegS(prev, g_, dtS);
    settle_.update(instDegS, nowMs);
    speed_ = instDegS;
  } else {
    float axis[3], thetaRad;
    quatToAxisAngle(q, axis, thetaRad);
    instDegS = joySpeedDegS(thetaRad * kRadToDeg, joy_);
    settle_.update(instDegS, nowMs);
    // Frozen while settled: only the commanded speed can restart it.
    if (!settle_.settled()) g_ = joystickStep(g_, q, dtS, joy_);
    speed_ = settle_.settled() ? 0.f : instDegS;
  }
  // From the REPORTED speed: a settled joystick (speed 0) never reads as moving.
  moving_ = speed_ > stopDegS_;
  valid_ = quatValid;

  const bool due = count_ == 0;
  if (++count_ >= divisor_) count_ = 0;
  return due;
}

uint8_t NavEngine::flags() const {
  uint8_t f = 0;
  if (valid_) f |= kNavValid;
  if (settle_.settled()) f |= kNavSettled;
  if (moving_) f |= kNavMoving;
  return f;
}

NavPacket NavEngine::packet(uint16_t seq) const {
  const NavPose p = navPoseFromGlobe(g_, r0_);
  return makeNavPacket(seq, lastTUs_, cfg_.mode, flags(), navLatE7(p.latDeg), navLonE7(p.lonDeg),
                       navBearingX100(p.bearingDeg), navSpeedX10(speed_));
}

NavPacket NavEngine::keepalivePacket(uint16_t seq, uint32_t tUs) const {
  return makeNavPacket(seq, tUs, cfg_.mode, 0, 0, 0, 0, 0);
}

bool NavEngine::keepaliveDue(uint32_t nowMs) const {
  if (cfg_.mode == kModeOff || !sampleSeen_) return true;
  // Signed: a sample stamped a hair after nowMs reads as "fresh", not as 49 days old.
  return (int32_t)(nowMs - lastSampleMs_) > (int32_t)kKeepaliveSilenceMs;
}

}  // namespace gaia
