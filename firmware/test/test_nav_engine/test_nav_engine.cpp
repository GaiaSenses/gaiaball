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

// Sample feeder: 100 Hz, micros and millis advancing together, epoch 0, valid.
struct Feed {
  NavEngine &e;
  uint32_t tUs;
  uint32_t ms;
  uint32_t epoch = 0;
  Feed(NavEngine &eng, uint32_t t0Us = 1000000, uint32_t t0Ms = 1000)
      : e(eng), tUs(t0Us), ms(t0Ms) {}
  bool step(const Quat &q, bool valid = true) {
    const bool due = e.update(tUs, q, valid, epoch, ms);
    tUs += kDtUs;
    ms += kDtMs;
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
  // Still: speed 0 <= stop (5); 420 ms of continuous low time settles.
  const uint32_t t0 = f.ms;
  while (f.ms - t0 <= 410) {
    f.step(rotZ(5));
    TEST_ASSERT_FALSE(e.settled());
    TEST_ASSERT_FALSE(e.moving());  // instantaneous: below stop right away
  }
  f.step(rotZ(5));  // now - t0 == 420
  TEST_ASSERT_TRUE(e.settled());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled, e.flags());
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
  // 3) never starts the motion: G stays put, reported speed 0, moving bit set.
  for (int i = 0; i < 50; i++) f.step(rotY(1.5));
  TEST_ASSERT_TRUE(e.settled());
  assertQuat(kId, e.globe(), 0.f);
  TEST_ASSERT_EQUAL_FLOAT(0.f, e.speedDegS());
  TEST_ASSERT_EQUAL_HEX8(kNavValid | kNavSettled | kNavMoving, e.flags());

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

  // The commanded speed (not G's, which is 0) restarts it.
  f.step(rotY(10));
  TEST_ASSERT_FALSE(e.settled());
  TEST_ASSERT_TRUE(angleDeg(frozen, e.globe()) > 0.1f);
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

void test_navigation_field_changes_reset_motion() {
  // Every field that shapes the motion: mode, reference, joystick law, settle.
  for (int field = 0; field < 10; field++) {
    GaiaConfig c = joyCfg();
    NavEngine e;
    e.configure(c);
    Feed f(e);
    moveAway(e, f);
    switch (field) {
      case 0: c.navRefLatE6 += 1; break;
      case 1: c.navRefLonE6 -= 1; break;
      case 2: c.joyMaxSpeedDegS = 91; break;
      case 3: c.joyFullAngleDeg = 61; break;
      case 4: c.joyCurveX10 = 11; break;
      case 5: c.joyStartDegSX10 = 31; break;
      case 6: c.joyStopDegSX10 = 16; break;
      case 7: c.navStopMs = 421; break;
      case 8: c.navStartDegS = 21; break;
      case 9: c.navStopDegS = 6; break;
    }
    e.configure(c);
    assertQuat(kId, e.globe(), 0.f);
    TEST_ASSERT_TRUE(e.settled());
  }
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
  RUN_TEST(test_mirror_dt_is_wrap_safe);
  RUN_TEST(test_mirror_epoch_change_reads_no_spurious_speed);
  RUN_TEST(test_valid_flag_follows_quat_valid);
  RUN_TEST(test_joystick_integrates_commanded_speed);
  RUN_TEST(test_joystick_settles_on_commanded_speed_and_freezes_globe);
  RUN_TEST(test_joystick_epoch_change_resets_globe);
  RUN_TEST(test_navigation_field_changes_reset_motion);
  RUN_TEST(test_mode_change_resets_motion);
  RUN_TEST(test_unrelated_config_changes_keep_state);
  RUN_TEST(test_reference_change_moves_identity_pose);
  return UNITY_END();
}
