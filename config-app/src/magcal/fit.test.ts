import { describe, expect, it } from 'vitest';
import { fitMagCalibration, IDENTITY_MAG_CAL, applyMagCal, magCalToFixed } from './fit';

/** Points spread over a sphere (Fibonacci lattice): the shape a full figure-8 traces. */
function sphereDirections(n: number): [number, number, number][] {
  const out: [number, number, number][] = [];
  const golden = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < n; i++) {
    const z = 1 - (2 * i) / (n - 1);
    const r = Math.sqrt(Math.max(0, 1 - z * z));
    const a = golden * i;
    out.push([r * Math.cos(a), r * Math.sin(a), z]);
  }
  return out;
}

/** Earth field of `radius` uT seen through a hard-iron offset and per-axis stretch. */
function distorted(radius: number, center: [number, number, number], stretch: [number, number, number], n = 150) {
  return sphereDirections(n).map(([x, y, z]): [number, number, number] => [
    radius * x * stretch[0] + center[0],
    radius * y * stretch[1] + center[1],
    radius * z * stretch[2] + center[2],
  ]);
}

describe('fitMagCalibration', () => {
  it('recovers a pure hard-iron offset', () => {
    const fit = fitMagCalibration(distorted(48, [25, -15, 10], [1, 1, 1]));
    expect(fit).not.toBeNull();
    expect(fit!.cal.hardIronUt[0]).toBeCloseTo(25, 1);
    expect(fit!.cal.hardIronUt[1]).toBeCloseTo(-15, 1);
    expect(fit!.cal.hardIronUt[2]).toBeCloseTo(10, 1);
    for (let i = 0; i < 3; i++) expect(fit!.cal.softIron[i * 4]).toBeCloseTo(1, 2);
    expect(fit!.residualPct).toBeLessThan(1);
  });

  it('recovers a soft-iron stretch as inverse per-axis gains', () => {
    const fit = fitMagCalibration(distorted(50, [0, 0, 0], [1, 1.25, 0.8]));
    expect(fit).not.toBeNull();
    // Gains normalise to the geometric mean radius, so their product is 1.
    const [gx, gy, gz] = [fit!.cal.softIron[0], fit!.cal.softIron[4], fit!.cal.softIron[8]];
    expect(gx * gy * gz).toBeCloseTo(1, 2);
    expect(gy / gx).toBeCloseTo(1 / 1.25, 2);
    expect(gz / gx).toBeCloseTo(1 / 0.8, 2);
  });

  it('maps every sample back onto one sphere', () => {
    // A volume-preserving stretch (product of gains = 1) is the case where the fit can
    // also recover the true field strength: an overall scale is not observable from the
    // shape alone, only the ratios between the axes are.
    const stretch: [number, number, number] = [1.15, 0.9, 1 / (1.15 * 0.9)];
    const samples = distorted(45, [30, 12, -20], stretch);
    const fit = fitMagCalibration(samples)!;
    const norms = samples.map((s) => {
      const c = applyMagCal(fit.cal, s);
      return Math.hypot(c[0], c[1], c[2]);
    });
    const min = Math.min(...norms);
    const max = Math.max(...norms);
    expect((max - min) / fit.radiusUt).toBeLessThan(0.02);
    expect(fit.radiusUt).toBeCloseTo(45, 0);
  });

  it('reports how much of the sphere was covered', () => {
    const full = fitMagCalibration(distorted(50, [0, 0, 0], [1, 1, 1]))!;
    expect(full.octants).toBe(8);
    // Only the +z cap: half the octants at best, which is what warns the user.
    const cap = distorted(50, [0, 0, 0], [1, 1, 1], 400).filter((s) => s[2] > 10);
    const partial = fitMagCalibration(cap);
    if (partial !== null) expect(partial.octants).toBeLessThanOrEqual(4);
  });

  it('refuses too few samples', () => {
    expect(fitMagCalibration(distorted(50, [0, 0, 0], [1, 1, 1], 10))).toBeNull();
  });

  it('refuses a degenerate cloud', () => {
    const same: [number, number, number][] = Array.from({ length: 200 }, () => [10, 10, 10]);
    expect(fitMagCalibration(same)).toBeNull();
  });
});

describe('magCalToFixed', () => {
  it('encodes the identity calibration as zeros and 4096 on the diagonal', () => {
    const { hard, soft } = magCalToFixed(IDENTITY_MAG_CAL);
    expect([...hard]).toEqual([0, 0, 0]);
    expect([...soft]).toEqual([4096, 0, 0, 0, 4096, 0, 0, 0, 4096]);
  });

  it('round-trips through the wire scales', () => {
    const { hard, soft } = magCalToFixed({ hardIronUt: [12.3, -45.6, 0.7], softIron: [1.25, 0, 0, 0, 0.8, 0, 0, 0, 1] });
    expect(hard[0]).toBe(123);
    expect(hard[1]).toBe(-456);
    expect(hard[2]).toBe(7);
    expect(soft[0]).toBe(5120);
    expect(soft[4]).toBe(3277);
  });

  it('saturates instead of wrapping', () => {
    const { hard } = magCalToFixed({ hardIronUt: [1e6, -1e6, 0], softIron: [1, 0, 0, 0, 1, 0, 0, 0, 1] });
    expect(hard[0]).toBe(32767);
    expect(hard[1]).toBe(-32768);
  });
});
