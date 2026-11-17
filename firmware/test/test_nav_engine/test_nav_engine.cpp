#include <unity.h>
#include <cmath>
#include <cstdint>
#include "../../Gaiaball/src/logic/GaiaConfig.h"
#include "../../Gaiaball/src/logic/NavEngine.h"
#include "../../Gaiaball/src/logic/PacketCodec.h"
#include "../../Gaiaball/src/logic/Packets.h"
#include "../../Gaiaball/src/logic/QuatMath.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

static const double kD2R = M_PI / 180.0;
static const Quat kId{1.f, 0.f, 0.f, 0.f};
static const uint32_t kDtUs = 10000;  // 100 Hz samples
static const uint32_t kDtMs = 10;

static Quat rotZ(double deg) {
  return Quat{(float)std::cos(deg * kD2R / 2), 0.f, 0.f, (float)std::sin(deg * kD2R / 2)};
}
static Quat rotY(double deg) {
  return Quat{(float)std::cos(deg * kD2R / 2), 0.f, (float)std::sin(deg * kD2R / 2), 0.f};
}

// Precise angle between two orientations (quatAngleDeg's float acos reads ~0.04 deg
// steps; angularSpeedDegS over 1 s is the atan2 form).
static float angleDeg(const Quat &a, const Quat &b) { return angularSpeedDegS(a, b, 1.f); }

static void assertQuat(const Quat &e, const Quat &g, float tol) {
  // q and -q are the same rotation.
  const float s = (e.w * g.w + e.x * g.x + e.y * g.y + e.z * g.z) < 0.f ? -1.f : 1.f;
  TEST_ASSERT_FLOAT_WITHIN(tol, e.w, s * g.w);
  TEST_ASSERT_FLOAT_WITHIN(tol, e.x, s * g.x);
  TEST_ASSERT_FLOAT_WITHIN(tol, e.y, s * g.y);
  TEST_ASSERT_FLOAT_WITHIN(tol, e.z, s * g.z);
}

// Sample feeder: 100 Hz by default, micros and millis advancing together, epoch 0,
// valid.
struct Feed {
  NavEngine &e;
  uint32_t tUs;
  uint32_t ms;
  uint32_t dtUs = kDtUs;
  uint32_t dtMs = kDtMs;
  uint32_t epoch = 0;
  Feed(NavEngine &eng, uint32_t t0Us = 1000000, uint32_t t0Ms = 1000)
      : e(eng), tUs(t0Us), ms(t0Ms) {}
  bool step(const Quat &q, bool valid = true) {
    const bool due = e.update(tUs, q, valid, epoch, ms);
    tUs += dtUs;
    ms += dtMs;
    return due;
  }
};

// Joystick config with simple numbers: linear law, 1.5 deg/s per degree of tilt
// (90 deg/s at 60 deg), start 3 deg/s, stop 1.5 deg/s, hold 420 ms.
static GaiaConfig joyCfg() {
  GaiaConfig c;
  c.navMode = 2;
  c.imuNotifyHz = 100;
  c.navNotifyHz = 100;
  c.joyMaxSpeedDegS = 90;
  c.joyFullAngleDeg = 60;
  c.joyCurveX10 = 10;
  c.joyStartDegSX10 = 30;
  c.joyStopDegSX10 = 15;
  c.navStopMs = 420;
  return c;
}

static GaiaConfig mirrorCfg() {
  GaiaConfig c;
  c.navMode = 1;
  c.imuNotifyHz = 100;
  c.navNotifyHz = 100;
  c.navStartDegS = 20;
  c.navStopDegS = 5;
  c.navStopMs = 420;
  return c;
}

// --- divisor -----------------------------------------------------------------

void test_divisor_rounds_and_never_exceeds_imu_rate() {
  TEST_ASSERT_EQUAL_UINT8(1, navDivisor(50, 50));
  TEST_ASSERT_EQUAL_UINT8(4, navDivisor(200, 50));
  TEST_ASSERT_EQUAL_UINT8(20, navDivisor(200, 10));
  TEST_ASSERT_EQUAL_UINT8(2, navDivisor(100, 60));   // 1.67 -> 2
  TEST_ASSERT_EQUAL_UINT8(2, navDivisor(75, 50));    // 1.5 -> 2 (half up)
  TEST_ASSERT_EQUAL_UINT8(3, navDivisor(100, 30));   // 3.33 -> 3
  TEST_ASSERT_EQUAL_UINT8(2, navDivisor(200, 133));  // 1.504 -> 2
  TEST_ASSERT_EQUAL_UINT8(1, navDivisor(200, 134));  // 1.493 -> 1
  // Asked for more than the IMU produces: one packet per sample, never more.
  TEST_ASSERT_EQUAL_UINT8(1, navDivisor(50, 200));
  TEST_ASSERT_EQUAL_UINT8(1, navDivisor(10, 200));
  TEST_ASSERT_EQUAL_UINT8(1, navDivisor(50, 0));
  TEST_ASSERT_EQUAL_UINT8(1, navDivisor(0, 50));
}

void test_packet_cadence_every_n_samples() {
  GaiaConfig c = mirrorCfg();
  c.imuNotifyHz = 200;
  c.navNotifyHz = 50;
  NavEngine e;
  e.configure(c);
  TEST_ASSERT_EQUAL_UINT8(4, e.divisor());
  Feed f(e);
  const bool expect[9] = {true, false, false, false, true, false, false, false, true};
  for (int i = 0; i < 9; i++) TEST_ASSERT_EQUAL_MESSAGE(expect[i], f.step(kId), "sample index");
}

void test_divisor_recomputed_on_rate_change() {
  GaiaConfig c = mirrorCfg();
  c.imuNotifyHz = 200;
  c.navNotifyHz = 50;
  NavEngine e;
  e.configure(c);
  Feed f(e);
  TEST_ASSERT_TRUE(f.step(kId));
  TEST_ASSERT_FALSE(f.step(kId));
  c.navNotifyHz = 100;  // N 4 -> 2, count restarts: the next sample is due
  e.configure(c);
  TEST_ASSERT_EQUAL_UINT8(2, e.divisor());
  TEST_ASSERT_TRUE(f.step(kId));
  TEST_ASSERT_FALSE(f.step(kId));
  TEST_ASSERT_TRUE(f.step(kId));
  c.imuNotifyHz = 100;  // the IMU rate is the other input: N 2 -> 1
  e.configure(c);
  TEST_ASSERT_EQUAL_UINT8(1, e.divisor());
  for (int i = 0; i < 3; i++) TEST_ASSERT_TRUE(f.step(kId));
}

// --- off mode and keepalive ---------------------------------------------------

void test_mode_off_does_nothing() {
  GaiaConfig c = mirrorCfg();
  c.navMode = 0;
  NavEngine e;
  e.configure(c);
  TEST_ASSERT_EQUAL_UINT8(0, e.mode());
  Feed f(e);
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(f.step(rotZ(10.0 * i)));
  assertQuat(kId, e.globe(), 0.f);
  TEST_ASSERT_TRUE(e.keepaliveDue(f.ms));  // off: the keepalive always runs
}

void test_unknown_mode_is_off() {
  GaiaConfig c = mirrorCfg();
  c.navMode = 3;
  NavEngine e;
  e.configure(c);
  TEST_ASSERT_EQUAL_UINT8(0, e.mode());
  Feed f(e);
  TEST_ASSERT_FALSE(f.step(kId));
}

void test_keepalive_due_without_samples_or_after_500ms_silence() {
  NavEngine e;
  e.configure(mirrorCfg());
  TEST_ASSERT_TRUE(e.keepaliveDue(5000));  // no sample ever (IMU absent)...
  TEST_ASSERT_TRUE(e.keepaliveDue(100));   // ...even right after boot
  e.update(1000000, kId, true, 0, 5000);
  TEST_ASSERT_FALSE(e.keepaliveDue(5000));
  TEST_ASSERT_FALSE(e.keepaliveDue(5500));  // exactly 500 ms: not yet
  TEST_ASSERT_TRUE(e.keepaliveDue(5501));
  // millis() wrap between the sample and the check.
  e.update(2000000, kId, true, 0, 0xFFFFFF00u);
  TEST_ASSERT_FALSE(e.keepaliveDue(0x000000F0u));  // 496 ms later, across the wrap
  TEST_ASSERT_TRUE(e.keepaliveDue(0x00000200u));
}

void test_keepalive_packet_is_invalid_with_zero_pose() {
  GaiaConfig c = joyCfg();
  NavEngine e;
  e.configure(c);
  const NavPacket p = e.keepalivePacket(7, 123456);
  TEST_ASSERT_EQUAL_UINT8(kProtocolVersion, p.version);
  TEST_ASSERT_EQUAL_UINT8(kTypeNav, p.type);
  TEST_ASSERT_EQUAL_UINT16(7, p.seq);
  TEST_ASSERT_EQUAL_UINT32(123456, p.timestampUs);
  TEST_ASSERT_EQUAL_UINT8(2, p.mode);  // current mode, even while silent
  TEST_ASSERT_EQUAL_UINT8(0, p.flags);
  TEST_ASSERT_EQUAL_INT32(0, p.latE7);
  TEST_ASSERT_EQUAL_INT32(0, p.lonE7);
  TEST_ASSERT_EQUAL_INT16(0, p.bearingX100);
  TEST_ASSERT_EQUAL_UINT16(0, p.speedDegSX10);
  c.navMode = 0;
  e.configure(c);
  TEST_ASSERT_EQUAL_UINT8(0, e.keepalivePacket(8, 0).mode);
}

// --- mirror -------------------------------------------------------------------

void test_mirror_identity_reports_reference_point() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  const uint32_t t = f.tUs;
  TEST_ASSERT_TRUE(f.step(kId));
  const NavPacket p = e.packet(42);
  TEST_ASSERT_EQUAL_UINT8(kTypeNav, p.type);
  TEST_ASSERT_EQUAL_UINT16(42, p.seq);
  TEST_ASSERT_EQUAL_UINT32(t, p.timestampUs);
  TEST_ASSERT_EQUAL_UINT8(1, p.mode);
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, p.flags);
  TEST_ASSERT_INT32_WITHIN(2000, -229055560, p.latE7);  // Campinas, within 0.0002 deg
  TEST_ASSERT_INT32_WITHIN(2000, -470608330, p.lonE7);
  TEST_ASSERT_INT_WITHIN(1, 0, p.bearingX100);
  TEST_ASSERT_EQUAL_UINT16(0, p.speedDegSX10);
}

void test_mirror_follows_q_measures_speed_and_settles() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  f.step(kId);
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.speedDegS());
  // 1 deg per 10 ms = 100 deg/s >= start (20): moving.
  f.step(rotZ(1));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.f, e.speedDegS());
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_TRUE(e.moving());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavMoving, e.flags());
  TEST_ASSERT_UINT16_WITHIN(5, 1000, e.packet(0).speedDegSX10);
  for (int d = 2; d <= 5; d++) f.step(rotZ(d));
  assertQuat(rotZ(5), e.globe(), 0.f);  // G = q exactly
  // Still. The speed is measured over a 100 ms window (against the newest sample at
  // least 100 ms old), so the turn stays in it for 100 ms after its last sample: the
  // speed decays (83 ... 10 deg/s) and the moving bit holds until rotZ(5) itself is
  // the reference.
  const uint32_t tLast = f.ms - kDtMs;  // stamp of the rotZ(5) sample
  while (f.ms - tLast < NavEngine::kMirrorWindowUs / 1000u) {
    f.step(rotZ(5));
    TEST_ASSERT_FALSE(e.settled());
    TEST_ASSERT_TRUE(e.moving());
  }
  f.step(rotZ(5));  // 100 ms after rotZ(5): the window holds no motion
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, e.speedDegS());
  TEST_ASSERT_FALSE(e.moving());  // instantaneous: below stop right away
  // From here, speed 0 <= stop (5); 420 ms of continuous low time settles.
  const uint32_t t0 = f.ms - kDtMs;  // first low sample
  while (f.ms - t0 <= 410) {
    f.step(rotZ(5));
    TEST_ASSERT_FALSE(e.settled());
    TEST_ASSERT_FALSE(e.moving());
  }
  f.step(rotZ(5));  // now - t0 == 420
  TEST_ASSERT_TRUE(e.settled());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, e.flags());
}

// Hand tremor (8-12 Hz) on a held ball. Sample to sample, a +-0.3 deg 10 Hz shake
// reads 5-18 deg/s at 50 Hz, above the stop threshold (5) on most samples, so a
// single-sample speed would never settle. Over the 100 ms window (one whole 10 Hz
// period) it reads ~0: the ball settles one window plus navStopMs after the turn.
void test_mirror_tremor_settles_within_stop_ms() {
  GaiaConfig c = mirrorCfg();
  c.imuNotifyHz = 50;
  c.navNotifyHz = 50;
  NavEngine e;
  e.configure(c);
  Feed f(e);
  f.dtUs = 20000;
  f.dtMs = 20;
  for (int d = 0; d <= 10; d += 2) f.step(rotZ(d));  // 100 deg/s turn: moving
  TEST_ASSERT_FALSE(e.settled());

  const uint32_t t0 = f.ms;  // first tremor sample (phase 0 = rotZ(10))
  const uint32_t settleBy = NavEngine::kMirrorWindowUs / 1000u + c.navStopMs;
  Quat prev = rotZ(10);
  float maxSampleDegS = 0.f;
  bool settled = false;
  for (int i = 0; i < 75; i++) {  // 1.5 s
    const Quat q = rotZ(10.0 + 0.3 * std::sin(2.0 * M_PI * 10.0 * (i * 0.02)));
    const float single = angleDeg(prev, q) / 0.02f;
    if (single > maxSampleDegS) maxSampleDegS = single;
    prev = q;
    const uint32_t t = f.ms;
    f.step(q);
    if (e.settled()) {
      if (!settled)
        TEST_ASSERT_TRUE_MESSAGE(t - t0 <= settleBy, "settled later than window + navStopMs");
      settled = true;
    } else {
      TEST_ASSERT_FALSE_MESSAGE(settled, "a settled ball moved again under tremor");
    }
  }
  TEST_ASSERT_TRUE(maxSampleDegS > (float)c.navStopDegS);  // the shake is real
  TEST_ASSERT_TRUE(settled);
}

// A slow, deliberate rotation below navStartDegS: the speed alone never leaves
// settled, but 3 deg of drift from the pose the ball settled at does.
void test_mirror_slow_rotation_unsettles_after_drift() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  // 5 deg/s from rest at 100 Hz: 0.05 deg per sample, 3 deg at sample 60.
  for (int i = 0; i <= 58; i++) {  // up to 2.9 deg
    f.step(rotZ(0.05 * i));
    TEST_ASSERT_TRUE(e.settled());
  }
  TEST_ASSERT_TRUE(e.speedDegS() < (float)mirrorCfg().navStartDegS);
  for (int i = 59; i <= 61; i++) f.step(rotZ(0.05 * i));  // 3.05 deg
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_EQUAL_HEX8(0, e.packet(0).flags & kNavSettled);
}

static void settleAt(NavEngine &e, Feed &f, const Quat &q) {
  for (int i = 0; i < 80 && !e.settled(); i++) f.step(q);  // window + 420 ms < 800 ms
  TEST_ASSERT_TRUE(e.settled());
}

// The drift is measured from the pose captured when the ball last settled: each
// settle recaptures it (else the first slow turn would leave settled at once).
void test_mirror_settle_pose_recaptured_on_each_settle() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  for (int d = 0; d <= 30; d += 3) f.step(rotZ(d));  // 300 deg/s turn away from rest
  TEST_ASSERT_FALSE(e.settled());
  settleAt(e, f, rotZ(30));
  for (int i = 0; i < 20; i++) f.step(rotZ(30));  // 30 deg from the boot pose: stays
  TEST_ASSERT_TRUE(e.settled());

  // Slow turn from 30 deg: settled up to 32.9, out by 33.05.
  for (int i = 1; i <= 58; i++) {
    f.step(rotZ(30 + 0.05 * i));
    TEST_ASSERT_TRUE(e.settled());
  }
  for (int i = 59; i <= 61; i++) f.step(rotZ(30 + 0.05 * i));
  TEST_ASSERT_FALSE(e.settled());

  // Settles again where it stopped (33.05) and measures the next drift from there.
  settleAt(e, f, rotZ(33.05));
  for (int i = 1; i <= 58; i++) {
    f.step(rotZ(33.05 + 0.05 * i));
    TEST_ASSERT_TRUE(e.settled());
  }
  for (int i = 59; i <= 61; i++) f.step(rotZ(33.05 + 0.05 * i));
  TEST_ASSERT_FALSE(e.settled());
}

void test_mirror_dt_is_wrap_safe() {
  NavEngine e;
  e.configure(mirrorCfg());
  const uint32_t t0 = 0xFFFFFFFFu - 9999u;  // 10 ms before the micros() wrap
  e.update(t0, kId, true, 0, 1000);
  e.update(t0 + 20000u, rotZ(2), true, 0, 1020);  // wraps to 10000: dt = 20 ms
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 100.f, e.speedDegS());
}

void test_mirror_epoch_change_reads_no_spurious_speed() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  for (int i = 0; i < 5; i++) f.step(rotZ(30));
  TEST_ASSERT_TRUE(e.settled());
  // The IMU applied a zero: q jumps to identity, but that is a new reference, not
  // a 3000 deg/s swing.
  f.epoch = 1;
  f.step(kId);
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.speedDegS());
  TEST_ASSERT_TRUE(e.settled());
  assertQuat(kId, e.globe(), 0.f);
}

void test_valid_flag_follows_quat_valid() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  f.step(kId, false);
  TEST_ASSERT_EQUAL_HEX8(kNavSettled, e.flags());
  f.step(kId, true);
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, e.flags());
}

// --- joystick -----------------------------------------------------------------

void test_joystick_integrates_commanded_speed() {
  NavEngine e;
  e.configure(joyCfg());
  Feed f(e);
  // 60 deg about +Y commands 90 deg/s; the first sample after a reset has dt = 0.
  f.step(rotY(60));
  TEST_ASSERT_FALSE(e.settled());
  assertQuat(kId, e.globe(), 0.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.f, e.speedDegS());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavMoving, e.flags());
  TEST_ASSERT_EQUAL_UINT16(900, e.packet(0).speedDegSX10);
  for (int i = 0; i < 10; i++) f.step(rotY(60));  // 10 x 0.9 deg
  assertQuat(rotY(9), e.globe(), 2e-5f);
}

void test_joystick_settles_on_commanded_speed_and_freezes_globe() {
  NavEngine e;
  e.configure(joyCfg());
  Feed f(e);
  // A tilt inside the hysteresis band (1.5 deg -> 2.25 deg/s, stop 1.5 < 2.25 < start
  // 3) never starts the motion: G stays put, reported speed 0, not moving.
  for (int i = 0; i < 50; i++) f.step(rotY(1.5));
  TEST_ASSERT_TRUE(e.settled());
  assertQuat(kId, e.globe(), 0.f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.speedDegS());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, e.flags());

  // 10 deg -> 15 deg/s: starts, and G integrates on that same sample.
  f.step(rotY(10));
  TEST_ASSERT_FALSE(e.settled());
  assertQuat(rotY(0.15), e.globe(), 2e-6f);
  f.step(rotY(10));
  assertQuat(rotY(0.30), e.globe(), 2e-6f);

  // 0.5 deg -> 0.75 deg/s <= stop: still integrating during the 420 ms hold...
  const Quat before = e.globe();
  const uint32_t t0 = f.ms;
  while (f.ms - t0 <= 410) f.step(rotY(0.5));
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_FALSE(e.moving());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.75f, e.speedDegS());
  TEST_ASSERT_TRUE(angleDeg(before, e.globe()) > 0.2f);  // ~42 x 0.0075 deg
  // ...then settles: G is frozen on the settling sample and after it.
  f.step(rotY(0.5));
  TEST_ASSERT_TRUE(e.settled());
  const Quat frozen = e.globe();
  for (int i = 0; i < 20; i++) f.step(rotY(1.5));  // back in the band: stays settled
  TEST_ASSERT_TRUE(e.settled());
  assertQuat(frozen, e.globe(), 0.f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.speedDegS());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, e.flags());

  // The commanded speed (not G's, which is 0) restarts it.
  f.step(rotY(10));
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_TRUE(angleDeg(frozen, e.globe()) > 0.1f);
}

// The moving bit follows the REPORTED speed: a settled joystick reports 0, so a tilt
// between stop and start reads valid | settled only, on the wire too.
void test_joystick_settled_in_band_is_not_moving() {
  NavEngine e;
  e.configure(joyCfg());
  Feed f(e);
  for (int i = 0; i < 10; i++) f.step(rotY(1.5));  // commands 2.25 deg/s: 1.5 < 2.25 < 3
  const NavPacket p = e.packet(0);
  TEST_ASSERT_EQUAL_HEX8(0x03, p.flags);
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, p.flags);
  TEST_ASSERT_EQUAL_UINT16(0, p.speedDegSX10);
  TEST_ASSERT_FALSE(e.moving());
}

void test_joystick_epoch_change_resets_globe() {
  NavEngine e;
  e.configure(joyCfg());
  Feed f(e);
  for (int i = 0; i < 20; i++) f.step(rotY(30));
  TEST_ASSERT_TRUE(angleDeg(kId, e.globe()) > 1.f);
  f.epoch = 7;  // any change of the epoch, not just +1
  f.step(rotY(30));
  assertQuat(kId, e.globe(), 0.f);  // reset, and dt = 0 on this sample
  f.step(rotY(30));
  TEST_ASSERT_TRUE(angleDeg(kId, e.globe()) > 0.1f);
}

// --- configuration changes -------------------------------------------------------

static void moveAway(NavEngine &e, Feed &f) {
  for (int i = 0; i < 20; i++) f.step(rotY(30));
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_TRUE(angleDeg(kId, e.globe()) > 1.f);
}

void test_reference_change_resets_motion() {
  for (int field = 0; field < 2; field++) {
    GaiaConfig c = joyCfg();
    NavEngine e;
    e.configure(c);
    Feed f(e);
    moveAway(e, f);
    if (field == 0)
      c.navRefLatE6 += 1;
    else
      c.navRefLonE6 -= 1;
    e.configure(c);
    assertQuat(kId, e.globe(), 0.f);
    TEST_ASSERT_TRUE(e.settled());
  }
}

// Joystick law and settle parameters apply live: neither the globe nor the settle
// state moves when they change (a sensitivity slider must not snap the globe back).
void test_parameter_changes_keep_globe_and_settle_state() {
  for (int field = 0; field < 8; field++) {
    GaiaConfig c = joyCfg();
    NavEngine e;
    e.configure(c);
    Feed f(e);
    moveAway(e, f);
    const Quat g = e.globe();
    switch (field) {
      case 0: c.joyMaxSpeedDegS = 91; break;
      case 1: c.joyFullAngleDeg = 61; break;
      case 2: c.joyCurveX10 = 11; break;
      case 3: c.joyStartDegSX10 = 31; break;
      case 4: c.joyStopDegSX10 = 16; break;
      case 5: c.navStopMs = 421; break;
      case 6: c.navStartDegS = 21; break;
      case 7: c.navStopDegS = 6; break;
    }
    e.configure(c);
    assertQuat(g, e.globe(), 0.f);
    TEST_ASSERT_FALSE(e.settled());
    f.step(rotY(30));  // keeps integrating from where it was (dt not cleared)
    TEST_ASSERT_TRUE(angleDeg(g, e.globe()) > 0.3f);
  }
}

void test_joystick_law_change_takes_effect_on_next_sample() {
  GaiaConfig c = joyCfg();
  NavEngine e;
  e.configure(c);
  Feed f(e);
  moveAway(e, f);  // 30 deg tilt: 45 deg/s
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.f, e.speedDegS());
  const Quat g = e.globe();
  c.joyMaxSpeedDegS = 180;  // twice the gain: 90 deg/s at the same tilt
  e.configure(c);
  f.step(rotY(30));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 90.f, e.speedDegS());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.9f, angleDeg(g, e.globe()));  // 90 deg/s x 10 ms
}

void test_settle_thresholds_take_effect_live() {
  GaiaConfig c = joyCfg();
  NavEngine e;
  e.configure(c);
  Feed f(e);
  moveAway(e, f);  // 45 deg/s, moving
  // Raise the band above the commanded speed: 45 <= stop 50 now counts as low time.
  c.joyStartDegSX10 = 600;
  c.joyStopDegSX10 = 500;
  e.configure(c);
  TEST_ASSERT_FALSE(e.settled());
  const uint32_t t0 = f.ms;
  while (f.ms - t0 <= 410) {
    f.step(rotY(30));
    TEST_ASSERT_FALSE(e.settled());
    TEST_ASSERT_FALSE(e.moving());  // reported 45 is not above the new stop (50)
  }
  f.step(rotY(30));
  TEST_ASSERT_TRUE(e.settled());
  const Quat frozen = e.globe();
  f.step(rotY(30));  // 45 < start 60: stays frozen
  assertQuat(frozen, e.globe(), 0.f);
}

void test_mode_change_resets_motion() {
  GaiaConfig c = joyCfg();
  NavEngine e;
  e.configure(c);
  Feed f(e);
  moveAway(e, f);
  c.navMode = 0;
  e.configure(c);
  assertQuat(kId, e.globe(), 0.f);
  TEST_ASSERT_FALSE(f.step(rotY(30)));  // off: untouched
  assertQuat(kId, e.globe(), 0.f);
  c.navMode = 2;
  e.configure(c);
  f.step(rotY(30));
  assertQuat(kId, e.globe(), 0.f);  // fresh start: dt = 0
}

void test_unrelated_config_changes_keep_state() {
  GaiaConfig c = joyCfg();
  c.imuNotifyHz = 200;
  c.navNotifyHz = 50;
  NavEngine e;
  e.configure(c);
  Feed f(e);
  for (int i = 0; i < 18; i++) f.step(rotY(30));  // count now at 2 of 4
  const Quat g = e.globe();
  c.co2AlertPpm = 1500;
  c.audioHpHz = 20;
  e.configure(c);
  assertQuat(g, e.globe(), 0.f);
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_FALSE(f.step(rotY(30)));  // count kept: samples 18 and 19 are not due
  TEST_ASSERT_FALSE(f.step(rotY(30)));
  TEST_ASSERT_TRUE(f.step(rotY(30)));
  // A rate change re-times the packets but leaves the globe alone.
  const Quat g2 = e.globe();
  c.navNotifyHz = 100;
  e.configure(c);
  assertQuat(g2, e.globe(), 0.f);
  TEST_ASSERT_FALSE(e.settled());
}

void test_reference_change_moves_identity_pose() {
  GaiaConfig c = mirrorCfg();
  NavEngine e;
  e.configure(c);
  c.navRefLatE6 = 10000000;   // 10 N
  c.navRefLonE6 = 20000000;   // 20 E
  e.configure(c);
  Feed f(e);
  f.step(kId);
  const NavPacket p = e.packet(0);
  TEST_ASSERT_INT32_WITHIN(2000, 100000000, p.latE7);
  TEST_ASSERT_INT32_WITHIN(2000, 200000000, p.lonE7);
  TEST_ASSERT_INT_WITHIN(1, 0, p.bearingX100);
}

// --- anchor (command 0x17) -------------------------------------------------------

static const NavAnchor kSaoPaulo{-23550520, -46633308, 1234};  // bearing 12.34
static const NavAnchor kParis{48856600, 2352200, -9000};       // bearing -90.00

// The packet shows the anchor: E7 = E6 x 10 within float rounding (0.0002 deg, the
// same tolerance as the configured reference point above), bearing within 0.01 deg.
static void assertShowsAnchor(const NavPacket &p, const NavAnchor &a) {
  TEST_ASSERT_INT32_WITHIN(2000, a.latE6 * 10, p.latE7);
  TEST_ASSERT_INT32_WITHIN(2000, a.lonE6 * 10, p.lonE7);
  TEST_ASSERT_INT_WITHIN(1, a.bearingX100, p.bearingX100);
}

static void assertShowsCampinas(const NavPacket &p) {
  TEST_ASSERT_INT32_WITHIN(2000, -229055560, p.latE7);
  TEST_ASSERT_INT32_WITHIN(2000, -470608330, p.lonE7);
  TEST_ASSERT_INT_WITHIN(1, 0, p.bearingX100);
}

void test_anchor_first_packet_mirror() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  for (int i = 0; i < 10; i++) f.step(rotZ(3.0 * i));  // the ball turning
  TEST_ASSERT_EQUAL_UINT8(0, e.anchorSeq());
  e.stageAnchor(kSaoPaulo);
  f.epoch = 1;  // the zero that 0x17 requests: the IMU output restarts at identity
  TEST_ASSERT_TRUE(f.step(kId));
  const NavPacket p = e.packet(0);
  assertShowsAnchor(p, kSaoPaulo);
  TEST_ASSERT_EQUAL_UINT8(1, p.anchorSeq);
  TEST_ASSERT_EQUAL_UINT8(1, e.anchorSeq());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, p.flags);
  TEST_ASSERT_EQUAL_UINT16(0, p.speedDegSX10);
}

void test_anchor_first_packet_joystick() {
  NavEngine e;
  e.configure(joyCfg());
  Feed f(e);
  moveAway(e, f);  // the globe integrated away from the reference
  e.stageAnchor(kParis);
  f.epoch = 1;
  // Still tilted on the first sample of the new epoch: the globe restarts at identity
  // with dt = 0, so this packet is exactly the anchor.
  TEST_ASSERT_TRUE(f.step(rotY(30)));
  const NavPacket p = e.packet(0);
  assertShowsAnchor(p, kParis);
  TEST_ASSERT_EQUAL_UINT8(1, p.anchorSeq);
  // The joystick then steers away from the anchor as usual.
  f.step(rotY(30));
  TEST_ASSERT_TRUE(angleDeg(kId, e.globe()) > 0.1f);
}

// IMU samples still queued from the old epoch keep the old reference: no packet mixes
// the old orientation with the new reference.
void test_anchor_waits_for_the_new_epoch() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  f.step(kId);
  e.stageAnchor(kSaoPaulo);
  for (int i = 0; i < 3; i++) {
    f.step(kId);  // epoch 0: queued before the zero took effect
    const NavPacket p = e.packet(0);
    assertShowsCampinas(p);
    TEST_ASSERT_EQUAL_UINT8(0, p.anchorSeq);
  }
  f.epoch = 1;
  f.step(kId);
  assertShowsAnchor(e.packet(0), kSaoPaulo);
  TEST_ASSERT_EQUAL_UINT8(1, e.anchorSeq());
}

// The latest anchor staged before the epoch changes wins, and is applied once.
void test_anchor_restaged_before_epoch_applies_latest_once() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  f.step(kId);
  e.stageAnchor(kSaoPaulo);
  e.stageAnchor(kParis);
  f.epoch = 1;
  f.step(kId);
  assertShowsAnchor(e.packet(0), kParis);
  TEST_ASSERT_EQUAL_UINT8(1, e.anchorSeq());
}

// The first packet after the anchor is due at once, whatever the divisor's phase.
void test_anchor_sample_is_due_regardless_of_divisor() {
  GaiaConfig c = mirrorCfg();
  c.imuNotifyHz = 200;
  c.navNotifyHz = 50;  // one packet every 4 samples
  NavEngine e;
  e.configure(c);
  Feed f(e);
  TEST_ASSERT_TRUE(f.step(kId));
  TEST_ASSERT_FALSE(f.step(kId));  // count at 2 of 4
  e.stageAnchor(kSaoPaulo);
  f.epoch = 1;
  TEST_ASSERT_TRUE(f.step(kId));
  assertShowsAnchor(e.packet(0), kSaoPaulo);
  // ... and the cadence restarts from there.
  TEST_ASSERT_FALSE(f.step(kId));
  TEST_ASSERT_FALSE(f.step(kId));
  TEST_ASSERT_FALSE(f.step(kId));
  TEST_ASSERT_TRUE(f.step(kId));
}

// A later zero (0x10: epoch change, nothing staged) returns to the anchor, not to the
// configured reference.
void test_anchor_survives_a_later_zero() {
  for (int mode = 1; mode <= 2; mode++) {
    GaiaConfig c = mode == 1 ? mirrorCfg() : joyCfg();
    NavEngine e;
    e.configure(c);
    Feed f(e);
    f.step(kId);
    e.stageAnchor(kParis);
    f.epoch = 1;
    f.step(kId);
    for (int i = 0; i < 20; i++) f.step(rotY(30));
    TEST_ASSERT_TRUE(angleDeg(kId, e.globe()) > 1.f);
    f.epoch = 2;
    f.step(kId);
    const NavPacket p = e.packet(0);
    assertShowsAnchor(p, kParis);
    TEST_ASSERT_EQUAL_UINT8(1, p.anchorSeq);  // no new anchor applied
  }
}

// A real change of the configured reference (TLV 0x62) wins over the anchor: both the
// active and a pending one are dropped.
void test_anchor_cleared_by_reference_change() {
  GaiaConfig c = mirrorCfg();
  NavEngine e;
  e.configure(c);
  Feed f(e);
  f.step(kId);
  e.stageAnchor(kSaoPaulo);
  f.epoch = 1;
  f.step(kId);
  assertShowsAnchor(e.packet(0), kSaoPaulo);
  c.navRefLatE6 = 10000000;  // 10 N
  c.navRefLonE6 = 20000000;  // 20 E
  e.configure(c);
  f.step(kId);
  NavPacket p = e.packet(0);
  TEST_ASSERT_INT32_WITHIN(2000, 100000000, p.latE7);
  TEST_ASSERT_INT32_WITHIN(2000, 200000000, p.lonE7);
  TEST_ASSERT_INT_WITHIN(1, 0, p.bearingX100);
  f.epoch = 2;  // a later zero keeps the configured reference
  f.step(kId);
  TEST_ASSERT_INT32_WITHIN(2000, 100000000, e.packet(0).latE7);

  // Pending: staged, then the reference changes before the zero takes effect.
  e.stageAnchor(kParis);
  c.navRefLatE6 = -22905556;
  c.navRefLonE6 = -47060833;
  e.configure(c);
  f.epoch = 3;
  f.step(kId);
  p = e.packet(0);
  assertShowsCampinas(p);
  TEST_ASSERT_EQUAL_UINT8(1, p.anchorSeq);  // Paris was never applied
}

// configure() runs on every config apply: anything but a reference change keeps the
// anchor, including a navMode change (which only restarts the globe).
void test_anchor_kept_by_other_config_changes() {
  GaiaConfig c = mirrorCfg();
  NavEngine e;
  e.configure(c);
  Feed f(e);
  f.step(kId);
  e.stageAnchor(kSaoPaulo);
  f.epoch = 1;
  f.step(kId);
  e.configure(c);  // same config re-applied
  c.co2AlertPpm = 1500;
  c.navStartDegS = 25;
  c.navNotifyHz = 25;
  e.configure(c);
  f.step(kId);
  assertShowsAnchor(e.packet(0), kSaoPaulo);
  c.navMode = 2;  // mirror -> joystick
  e.configure(c);
  f.step(rotY(30));  // first sample after the mode reset: G = identity
  assertShowsAnchor(e.packet(0), kSaoPaulo);
  // The same reference written again is not a change either.
  c.navRefLatE6 = -22905556;
  c.navRefLonE6 = -47060833;
  e.configure(c);
  f.step(kId);
  TEST_ASSERT_EQUAL_UINT8(1, e.anchorSeq());
  c.navMode = 1;
  e.configure(c);
  f.step(kId);
  assertShowsAnchor(e.packet(0), kSaoPaulo);
}

void test_anchor_seq_counts_and_wraps() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  f.step(kId);
  for (int i = 1; i <= 257; i++) {
    e.stageAnchor(i % 2 ? kSaoPaulo : kParis);
    f.epoch = (uint32_t)i;
    f.step(kId);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)i, e.anchorSeq());  // ... 255, 0, 1
    TEST_ASSERT_EQUAL_UINT8((uint8_t)i, e.packet(0).anchorSeq);
  }
}

// Navigation off: nothing is applied until it is back on; the zero that came with the
// anchor is then the epoch change that applies it.
void test_anchor_staged_while_off_applies_when_on() {
  GaiaConfig c = mirrorCfg();
  NavEngine e;
  e.configure(c);
  Feed f(e);
  f.step(kId);
  c.navMode = 0;
  e.configure(c);
  e.stageAnchor(kSaoPaulo);
  f.epoch = 1;
  f.step(kId);
  TEST_ASSERT_EQUAL_UINT8(0, e.anchorSeq());
  c.navMode = 1;
  e.configure(c);
  f.step(kId);
  assertShowsAnchor(e.packet(0), kSaoPaulo);
  TEST_ASSERT_EQUAL_UINT8(1, e.anchorSeq());
}

void test_keepalive_carries_anchor_seq() {
  NavEngine e;
  e.configure(mirrorCfg());
  Feed f(e);
  f.step(kId);
  TEST_ASSERT_EQUAL_UINT8(0, e.keepalivePacket(0, 0).anchorSeq);
  e.stageAnchor(kSaoPaulo);
  f.epoch = 1;
  f.step(kId);
  TEST_ASSERT_EQUAL_UINT8(1, e.keepalivePacket(0, 0).anchorSeq);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_divisor_rounds_and_never_exceeds_imu_rate);
  RUN_TEST(test_packet_cadence_every_n_samples);
  RUN_TEST(test_divisor_recomputed_on_rate_change);
  RUN_TEST(test_mode_off_does_nothing);
  RUN_TEST(test_unknown_mode_is_off);
  RUN_TEST(test_keepalive_due_without_samples_or_after_500ms_silence);
  RUN_TEST(test_keepalive_packet_is_invalid_with_zero_pose);
  RUN_TEST(test_mirror_identity_reports_reference_point);
  RUN_TEST(test_mirror_follows_q_measures_speed_and_settles);
  RUN_TEST(test_mirror_tremor_settles_within_stop_ms);
  RUN_TEST(test_mirror_slow_rotation_unsettles_after_drift);
  RUN_TEST(test_mirror_settle_pose_recaptured_on_each_settle);
  RUN_TEST(test_mirror_dt_is_wrap_safe);
  RUN_TEST(test_mirror_epoch_change_reads_no_spurious_speed);
  RUN_TEST(test_valid_flag_follows_quat_valid);
  RUN_TEST(test_joystick_integrates_commanded_speed);
  RUN_TEST(test_joystick_settles_on_commanded_speed_and_freezes_globe);
  RUN_TEST(test_joystick_settled_in_band_is_not_moving);
  RUN_TEST(test_joystick_epoch_change_resets_globe);
  RUN_TEST(test_reference_change_resets_motion);
  RUN_TEST(test_parameter_changes_keep_globe_and_settle_state);
  RUN_TEST(test_joystick_law_change_takes_effect_on_next_sample);
  RUN_TEST(test_settle_thresholds_take_effect_live);
  RUN_TEST(test_mode_change_resets_motion);
  RUN_TEST(test_unrelated_config_changes_keep_state);
  RUN_TEST(test_reference_change_moves_identity_pose);
  RUN_TEST(test_anchor_first_packet_mirror);
  RUN_TEST(test_anchor_first_packet_joystick);
  RUN_TEST(test_anchor_waits_for_the_new_epoch);
  RUN_TEST(test_anchor_restaged_before_epoch_applies_latest_once);
  RUN_TEST(test_anchor_sample_is_due_regardless_of_divisor);
  RUN_TEST(test_anchor_survives_a_later_zero);
  RUN_TEST(test_anchor_cleared_by_reference_change);
  RUN_TEST(test_anchor_kept_by_other_config_changes);
  RUN_TEST(test_anchor_seq_counts_and_wraps);
  RUN_TEST(test_anchor_staged_while_off_applies_when_on);
  RUN_TEST(test_keepalive_carries_anchor_seq);
  return UNITY_END();
}
