#include <unity.h>
#include <cmath>
#include <cstdint>
#include "../../Gaiaball/src/logic/MotionSettle.h"
#include "../../Gaiaball/src/logic/NavMath.h"
#include "../../Gaiaball/src/logic/QuatMath.h"

using namespace gaia;

void setUp() {}
void tearDown() {}

// Frames (binding): reference X = toward the user, Y = ball's left (user's right),
// Z = up; globe x -> (0,0), y -> (0,90E), z -> north pole. Rotations are
// right-handed. The principal-axis quaternions are built by hand so the golden
// cases do not depend on quatFromAxisAngle.
static const double kD2R = M_PI / 180.0;
static const float kCampLat = -22.905556f;
static const float kCampLon = -47.060833f;
static const Quat kId{1.f, 0.f, 0.f, 0.f};

static Quat rotZ(double deg) {
  return Quat{(float)std::cos(deg * kD2R / 2), 0.f, 0.f, (float)std::sin(deg * kD2R / 2)};
}
static Quat rotY(double deg) {
  return Quat{(float)std::cos(deg * kD2R / 2), 0.f, (float)std::sin(deg * kD2R / 2), 0.f};
}
static Quat rotX(double deg) {
  return Quat{(float)std::cos(deg * kD2R / 2), (float)std::sin(deg * kD2R / 2), 0.f, 0.f};
}

static void pointOf(double latDeg, double lonDeg, float out[3]) {
  const double f = latDeg * kD2R, l = lonDeg * kD2R;
  out[0] = (float)(std::cos(f) * std::cos(l));
  out[1] = (float)(std::cos(f) * std::sin(l));
  out[2] = (float)std::sin(f);
}
static void eastOf(double lonDeg, float out[3]) {
  const double l = lonDeg * kD2R;
  out[0] = (float)-std::sin(l);
  out[1] = (float)std::cos(l);
  out[2] = 0.f;
}
static void northOf(double latDeg, double lonDeg, float out[3]) {
  const double f = latDeg * kD2R, l = lonDeg * kD2R;
  out[0] = (float)(-std::sin(f) * std::cos(l));
  out[1] = (float)(-std::sin(f) * std::sin(l));
  out[2] = (float)std::cos(f);
}

static void assertVec(float ex, float ey, float ez, const float got[3], float tol) {
  TEST_ASSERT_FLOAT_WITHIN(tol, ex, got[0]);
  TEST_ASSERT_FLOAT_WITHIN(tol, ey, got[1]);
  TEST_ASSERT_FLOAT_WITHIN(tol, ez, got[2]);
}

static NavPose poseAtOrigin(const Quat &G) {
  return navPoseFromGlobe(G, navRefRotation(0.f, 0.f));
}

// ---------------------------------------------------------------- navRefRotation

void test_ref_origin_is_identity() {
  const Quat r0 = navRefRotation(0.f, 0.f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, quatAngleDeg(kId, r0));
}

// R0 maps the reference point to +X, local east to +Y and local north to +Z. The
// references cover the trace > 0 branch, the z-dominant branch and both poles. The
// y-dominant branch is unreachable for this matrix family, and the x-dominant branch
// is only reached through float rounding at an exact pole with |lon| > 90, which the
// (90, 180) and (-90, 135) points exercise.
void test_ref_maps_local_basis() {
  const float refs[][2] = {{kCampLat, kCampLon}, {0.f, 0.f},    {0.f, 180.f},  {0.f, -90.f},
                           {90.f, 0.f},         {-90.f, 30.f}, {45.f, -135.f}, {-60.f, 120.f},
                           {10.f, 170.f},       {90.f, 180.f}, {-90.f, 135.f}};
  for (const auto &r : refs) {
    const Quat r0 = navRefRotation(r[0], r[1]);
    float p[3], e[3], n[3], out[3];
    pointOf(r[0], r[1], p);
    eastOf(r[1], e);
    northOf(r[0], r[1], n);
    quatRotateVector(r0, p, out);
    assertVec(1.f, 0.f, 0.f, out, 1e-5f);
    quatRotateVector(r0, e, out);
    assertVec(0.f, 1.f, 0.f, out, 1e-5f);
    quatRotateVector(r0, n, out);
    assertVec(0.f, 0.f, 1.f, out, 1e-5f);
  }
}

// ------------------------------------------------------------ navPoseFromGlobe

void test_pose_rz_moves_longitude_negative() {
  const NavPose p = poseAtOrigin(rotZ(25.0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p.latDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, -25.f, p.lonDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p.bearingDeg);
}

void test_pose_ry_moves_latitude_positive() {
  const NavPose p = poseAtOrigin(rotY(30.0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.f, p.latDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p.lonDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p.bearingDeg);
}

void test_pose_rx_sets_bearing() {
  const NavPose p = poseAtOrigin(rotX(40.0));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p.latDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, p.lonDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.f, p.bearingDeg);
}

void test_pose_bearing_half_turn_is_plus_180() {
  // Half a turn about +X: screen up shows south. (-180, 180] keeps it at +180.
  const NavPose p = poseAtOrigin(Quat{0.f, 1.f, 0.f, 0.f});
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.f, p.bearingDeg);
  TEST_ASSERT_TRUE(p.bearingDeg > -180.f);
  TEST_ASSERT_TRUE(p.bearingDeg <= 180.f);
  // A hair past half a turn: atan2 lands on -pi (u.east = -2e-8, u.north = -1),
  // which is -180 in float and must fold to +180.
  const NavPose q = poseAtOrigin(Quat{-1e-8f, 1.f, 0.f, 0.f});
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 180.f, q.bearingDeg);
  TEST_ASSERT_TRUE(q.bearingDeg > -180.f);
}

void test_pose_identity_shows_campinas() {
  const NavPose p = navPoseFromGlobe(kId, navRefRotation(kCampLat, kCampLon));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -22.905556f, p.latDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -47.060833f, p.lonDeg);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.f, p.bearingDeg);
}

// Any non-trivial G: rebuilding the screen-centre point from (lat, lon) and
// rotating it by C = G (x) R0 lands on +X; rebuilding the screen-up direction
// from the bearing (cos b * north + sin b * east) lands on +Z.
void test_pose_round_trip_composed() {
  const Quat G = quatMultiply(quatMultiply(rotZ(-63.0), rotX(31.0)), rotY(-17.0));
  const Quat R0 = navRefRotation(kCampLat, kCampLon);
  const Quat C = quatMultiply(G, R0);
  const NavPose p = navPoseFromGlobe(G, R0);
  // Not a trivial pose.
  TEST_ASSERT_TRUE(std::fabs(p.latDeg - kCampLat) > 1.f);
  TEST_ASSERT_TRUE(std::fabs(p.bearingDeg) > 1.f);

  float pt[3], out[3];
  pointOf(p.latDeg, p.lonDeg, pt);
  quatRotateVector(C, pt, out);
  assertVec(1.f, 0.f, 0.f, out, 1e-4f);

  float n[3], e[3], up[3];
  northOf(p.latDeg, p.lonDeg, n);
  eastOf(p.lonDeg, e);
  const double b = p.bearingDeg * kD2R;
  for (int i = 0; i < 3; i++) up[i] = (float)(std::cos(b) * n[i] + std::sin(b) * e[i]);
  quatRotateVector(C, up, out);
  assertVec(0.f, 0.f, 1.f, out, 1e-4f);
}

// --------------------------------------------------------------------- joystick

void test_joy_speed_law() {
  const JoyParams lin{90.f, 60.f, 1.f};
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 45.f, joySpeedDegS(30.f, lin));
  const JoyParams cubic{90.f, 60.f, 3.f};
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 11.25f, joySpeedDegS(30.f, cubic));
  // Saturates at and beyond the full-speed angle.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.f, joySpeedDegS(60.f, cubic));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 90.f, joySpeedDegS(170.f, cubic));
  // theta <= 0 commands nothing.
  TEST_ASSERT_EQUAL_FLOAT(0.f, joySpeedDegS(0.f, cubic));
  TEST_ASSERT_EQUAL_FLOAT(0.f, joySpeedDegS(-5.f, cubic));
}

void test_joy_integrates_rz30_to_rz45() {
  const JoyParams lin{90.f, 60.f, 1.f};
  const Quat qDev = rotZ(30.0);  // 30 of 60 deg, linear: 45 deg/s
  Quat G = kId;
  for (int i = 0; i < 100; i++) G = joystickStep(G, qDev, 0.01f, lin);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 45.f, quatAngleDeg(kId, G));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.f, quatAngleDeg(rotZ(45.0), G));  // about +Z
  const float n = std::sqrt(G.w * G.w + G.x * G.x + G.y * G.y + G.z * G.z);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.f, n);
}

void test_joy_cubic_curve_rate() {
  const JoyParams cubic{90.f, 60.f, 3.f};
  Quat G = kId;
  for (int i = 0; i < 100; i++) G = joystickStep(G, rotZ(30.0), 0.01f, cubic);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 11.25f, quatAngleDeg(kId, G));
}

// The step is applied in the reference frame: G' = delta (x) G (left multiply).
void test_joy_step_left_multiplies() {
  const JoyParams lin{90.f, 60.f, 1.f};
  const Quat G0 = rotX(20.0);
  const Quat G1 = joystickStep(G0, rotZ(30.0), 0.05f, lin);  // 45 deg/s * 0.05 s
  const Quat expect = quatMultiply(rotZ(2.25), G0);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, quatAngleDeg(expect, G1));
  // -q is the same device orientation: same step. Opposite tilt: opposite step.
  const Quat qNeg{-rotZ(30.0).w, 0.f, 0.f, -rotZ(30.0).z};
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f, quatAngleDeg(expect, joystickStep(G0, qNeg, 0.05f, lin)));
  const Quat back = quatMultiply(rotZ(-2.25), G0);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.f,
                           quatAngleDeg(back, joystickStep(G0, rotZ(-30.0), 0.05f, lin)));
}

void test_joy_dt_clamped() {
  const JoyParams lin{90.f, 60.f, 1.f};
  const Quat qDev = rotZ(30.0);
  const Quat big = joystickStep(kId, qDev, 1.0f, lin);
  const Quat cap = joystickStep(kId, qDev, 0.05f, lin);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.f, quatAngleDeg(cap, big));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 2.25f, quatAngleDeg(kId, big));
  // Negative dt clamps to 0: no motion.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.f, quatAngleDeg(kId, joystickStep(kId, qDev, -0.5f, lin)));
}

void test_joy_zero_theta_leaves_globe_unchanged() {
  const JoyParams lin{90.f, 60.f, 1.f};
  const Quat G = quatMultiply(rotZ(12.0), rotY(-7.0));
  const Quat r = joystickStep(G, kId, 0.02f, lin);
  TEST_ASSERT_EQUAL_FLOAT(G.w, r.w);
  TEST_ASSERT_EQUAL_FLOAT(G.x, r.x);
  TEST_ASSERT_EQUAL_FLOAT(G.y, r.y);
  TEST_ASSERT_EQUAL_FLOAT(G.z, r.z);
}

void test_angular_speed() {
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 100.f, angularSpeedDegS(rotZ(0.0), rotZ(10.0), 0.1f));
  TEST_ASSERT_EQUAL_FLOAT(0.f, angularSpeedDegS(rotZ(0.0), rotZ(10.0), 0.f));
  TEST_ASSERT_EQUAL_FLOAT(0.f, angularSpeedDegS(rotZ(0.0), rotZ(10.0), -0.1f));
}

// Per-sample rotations far below quatAngleDeg's ~0.04 deg float-acos floor must
// still read: the settle detector compares against a few deg/s at up to 200 Hz.
void test_angular_speed_small_angles() {
  // 0.01 deg in 20 ms = 0.5 deg/s (quatAngleDeg reads 0 here).
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, angularSpeedDegS(kId, rotZ(0.01), 0.02f));
  // Same step from a non-trivial orientation, about another axis.
  const Quat a = quatMultiply(rotZ(-63.0), rotX(31.0));
  const Quat b = quatMultiply(rotY(0.01), a);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, angularSpeedDegS(a, b, 0.02f));
  // 0.001 deg still reads (0.05 deg/s), not 0.
  const float tiny = angularSpeedDegS(kId, rotZ(0.001), 0.02f);
  TEST_ASSERT_TRUE(tiny > 0.f);
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.05f, tiny);
}

void test_angular_speed_sign_invariant() {
  // q and -q are the same orientation: same speed, and the short way round.
  const Quat a = quatMultiply(rotZ(12.0), rotY(-7.0));
  const Quat b = quatMultiply(rotX(3.0), a);
  const Quat nb{-b.w, -b.x, -b.y, -b.z};
  const Quat na{-a.w, -a.x, -a.y, -a.z};
  const float s = angularSpeedDegS(a, b, 0.1f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 30.f, s);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, s, angularSpeedDegS(a, nb, 0.1f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, s, angularSpeedDegS(na, b, 0.1f));
  // A 350 deg relative turn is 10 deg the short way.
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 100.f, angularSpeedDegS(kId, rotZ(350.0), 0.1f));
}

// ----------------------------------------------------------------- MotionSettle

void test_settle_starts_settled() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  TEST_ASSERT_TRUE(m.settled());
  TEST_ASSERT_FALSE(m.moving());
}

void test_settle_below_start_never_moves() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  for (uint32_t t = 0; t < 5000; t += 10) m.update(19.9f, t);
  TEST_ASSERT_TRUE(m.settled());
}

void test_settle_start_threshold_and_middle_band() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  m.update(20.f, 0);  // >= start
  TEST_ASSERT_TRUE(m.moving());
  TEST_ASSERT_FALSE(m.settled());
  // Between stop and start: keeps moving indefinitely.
  for (uint32_t t = 10; t < 5000; t += 10) m.update(10.f, t);
  TEST_ASSERT_TRUE(m.moving());
}

void test_settle_needs_continuous_low_time() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  m.update(50.f, 0);
  m.update(5.f, 100);  // <= stop: low timer starts at 100
  m.update(1.f, 300);
  m.update(4.f, 519);
  TEST_ASSERT_TRUE(m.moving());
  m.update(4.f, 520);  // 420 ms of low speed
  TEST_ASSERT_TRUE(m.settled());
  TEST_ASSERT_FALSE(m.moving());
}

void test_settle_spike_resets_low_timer() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  m.update(50.f, 0);
  m.update(2.f, 100);
  m.update(6.f, 400);  // above stop (still below start): timer resets
  m.update(2.f, 410);  // low again from 410
  m.update(2.f, 520);  // would have settled without the spike
  m.update(2.f, 829);
  TEST_ASSERT_TRUE(m.moving());
  m.update(2.f, 830);
  TEST_ASSERT_TRUE(m.settled());
  // Once settled, needs >= start again to move.
  m.update(19.f, 900);
  TEST_ASSERT_TRUE(m.settled());
  m.update(21.f, 910);
  TEST_ASSERT_TRUE(m.moving());
}

void test_settle_millis_wraparound() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  const uint32_t t0 = 0xFFFFFF00u;
  m.update(30.f, t0 - 10);
  m.update(1.f, t0);
  m.update(1.f, t0 + 419u);  // wraps past zero
  TEST_ASSERT_TRUE(m.moving());
  m.update(1.f, t0 + 420u);
  TEST_ASSERT_TRUE(m.settled());
}

void test_settle_reset() {
  MotionSettle m;
  m.configure(20.f, 5.f, 420);
  m.update(30.f, 0);
  m.update(1.f, 10);
  TEST_ASSERT_TRUE(m.moving());
  m.reset();
  TEST_ASSERT_TRUE(m.settled());
  // A fresh move needs a fresh, full low period.
  m.update(30.f, 20);
  m.update(1.f, 430);
  TEST_ASSERT_TRUE(m.moving());
  m.update(1.f, 850);
  TEST_ASSERT_TRUE(m.settled());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_ref_origin_is_identity);
  RUN_TEST(test_ref_maps_local_basis);
  RUN_TEST(test_pose_rz_moves_longitude_negative);
  RUN_TEST(test_pose_ry_moves_latitude_positive);
  RUN_TEST(test_pose_rx_sets_bearing);
  RUN_TEST(test_pose_bearing_half_turn_is_plus_180);
  RUN_TEST(test_pose_identity_shows_campinas);
  RUN_TEST(test_pose_round_trip_composed);
  RUN_TEST(test_joy_speed_law);
  RUN_TEST(test_joy_integrates_rz30_to_rz45);
  RUN_TEST(test_joy_cubic_curve_rate);
  RUN_TEST(test_joy_step_left_multiplies);
  RUN_TEST(test_joy_dt_clamped);
  RUN_TEST(test_joy_zero_theta_leaves_globe_unchanged);
  RUN_TEST(test_angular_speed);
  RUN_TEST(test_angular_speed_small_angles);
  RUN_TEST(test_angular_speed_sign_invariant);
  RUN_TEST(test_settle_starts_settled);
  RUN_TEST(test_settle_below_start_never_moves);
  RUN_TEST(test_settle_start_threshold_and_middle_band);
  RUN_TEST(test_settle_needs_continuous_low_time);
  RUN_TEST(test_settle_spike_resets_low_timer);
  RUN_TEST(test_settle_millis_wraparound);
  RUN_TEST(test_settle_reset);
  return UNITY_END();
}
