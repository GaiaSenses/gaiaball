import { describe, expect, it } from 'vitest';

import { quatToEulerLegacy } from './euler';

const S2 = Math.SQRT1_2;

describe('quatToEulerLegacy', () => {
  it('matches the protocol.md reference vector: 90° yaw -> (0, 0, -90)', () => {
    const e = quatToEulerLegacy({ w: S2, x: 0, y: 0, z: S2 });
    expect(e.rollDeg).toBeCloseTo(0, 4);
    expect(e.pitchDeg).toBeCloseTo(0, 4);
    expect(e.yawDeg).toBeCloseTo(-90, 4);
  });

  it('identity quaternion -> all zeros', () => {
    const e = quatToEulerLegacy({ w: 1, x: 0, y: 0, z: 0 });
    expect(e.rollDeg).toBeCloseTo(0, 6);
    expect(e.pitchDeg).toBeCloseTo(0, 6);
    expect(e.yawDeg).toBeCloseTo(0, 6);
  });

  it('clamps asin input at gimbal lock instead of returning NaN', () => {
    // 90° rotation that drives the remapped sin(pitch) term to ±1.
    const e = quatToEulerLegacy({ w: S2, x: S2, y: 0, z: 0 });
    expect(Number.isFinite(e.pitchDeg)).toBe(true);
    expect(Math.abs(e.pitchDeg)).toBeCloseTo(90, 3);
  });
});
