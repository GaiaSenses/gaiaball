// Legacy Euler convention — faithful port of firmware quatToEulerLegacy()
// (firmware/Gaiaball/src/logic/QuatMath.cpp; spec in firmware/docs/protocol.md).
// Axis remap (qw=w, qx=y, qy=x, qz=-z) then aerospace roll/pitch/yaw formulas.
// Reference vector: a 90° yaw quaternion maps to (roll 0, pitch 0, yaw -90).

const RAD_TO_DEG = 180 / Math.PI;

export interface EulerDeg {
  rollDeg: number;
  pitchDeg: number;
  yawDeg: number;
}

export function quatToEulerLegacy(q: { w: number; x: number; y: number; z: number }): EulerDeg {
  const qw = q.w;
  const qx = q.y;
  const qy = q.x;
  const qz = -q.z;
  let sinp = 2 * (qw * qy - qx * qz);
  sinp = Math.min(1, Math.max(-1, sinp));
  return {
    rollDeg: Math.atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy)) * RAD_TO_DEG,
    pitchDeg: Math.asin(sinp) * RAD_TO_DEG,
    yawDeg: Math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) * RAD_TO_DEG,
  };
}
