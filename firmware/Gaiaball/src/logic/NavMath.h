#pragma once
#include "QuatMath.h"

namespace gaia {

// Globe navigation math. Frames (shared with the config-app and the site):
//  - reference frame (ball zeroed): X = ball front, toward the user; Z = up;
//    Y = the ball's left = the user's right (right-handed);
//  - globe frame E (unit sphere): x -> (lat 0, lon 0), y -> (lat 0, lon 90E),
//    z -> north pole.
// The globe orientation G rotates the globe in the reference frame; the globe
// as seen on screen is C = G (x) R0, where R0 = navRefRotation(reference point).
// The screen centre is +X, screen up is +Z.

struct NavPose {
  float latDeg, lonDeg, bearingDeg;
};

struct JoyParams {
  float maxSpeedDegS;  // speed at and beyond fullAngleDeg
  float fullAngleDeg;  // tilt that commands full speed
  float curve;         // response exponent (1 = linear)
};

// R0: maps the reference point p to +X, its local east to +Y and local north to
// +Z, so identity G shows the reference point at the centre with north up.
Quat navRefRotation(float latDeg, float lonDeg);

// Screen-centre latitude/longitude and Mapbox bearing (degrees clockwise from
// north of the direction pointing up on screen, in (-180, 180]).
NavPose navPoseFromGlobe(const Quat &G, const Quat &R0);

// Commanded joystick speed for a tilt of thetaDeg:
// maxSpeed * min(theta / fullAngle, 1)^curve; theta <= 0 commands 0.
float joySpeedDegS(float thetaDeg, const JoyParams &p);

// One joystick integration step: (u, theta) = shortest axis-angle of qDev,
// G' = normalize(delta (x) G), delta = rotation about u by joySpeedDegS(theta) * dt.
// dtS is clamped to [0, 0.05] s. Returns G unchanged when theta == 0 (or the
// step is 0).
Quat joystickStep(const Quat &G, const Quat &qDev, float dtS, const JoyParams &p);

// Shortest-path angle between a and b, in degrees, divided by dtS; dtS <= 0
// gives 0. Same quantity as quatAngleDeg(a, b) / dtS, but computed as
// 2*atan2(|r.xyz|, |r.w|) of r = a* (x) b: quatAngleDeg takes the float acos of
// a dot product, which reads any step below ~0.04 deg as 0 (about 8 deg/s of
// resolution at 200 Hz, above the settle stop threshold). quatAngleDeg itself is
// left as is for QuatSmoother/ImuService.
float angularSpeedDegS(const Quat &a, const Quat &b, float dtS);

}  // namespace gaia
