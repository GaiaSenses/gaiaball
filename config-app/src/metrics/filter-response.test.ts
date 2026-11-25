import { describe, expect, it } from 'vitest';
import {
  AUDIO_FS_HZ, SLOPES_DB_OCT, bandGainDb, cutGainDb, isValidSlope, responseCurve, sectionQs, type BandSpec, type CutSpec,
} from './filter-response';

const cut = (hz: number, qX100: number, slopeDbOct: number): CutSpec => ({ hz, qX100, slopeDbOct });

describe('sectionQs', () => {
  it('are the Butterworth Qs, highest first, at q = 1/√2', () => {
    const q = Math.SQRT1_2;
    expect(sectionQs(1, q)).toEqual([]);
    expect(sectionQs(2, q)[0]).toBeCloseTo(0.7071, 4);
    expect(sectionQs(3, q)[0]).toBeCloseTo(1.0, 4);
    const q4 = sectionQs(4, q);
    expect(q4[0]).toBeCloseTo(1.3066, 4);
    expect(q4[1]).toBeCloseTo(0.5412, 4);
    const q8 = sectionQs(8, q);
    expect(q8.map((v) => +v.toFixed(4))).toEqual([2.5629, 0.9, 0.6013, 0.5098]);
  });

  it('only the highest-Q section follows the person’s Q', () => {
    const flat = sectionQs(4, Math.SQRT1_2), peaky = sectionQs(4, 5);
    expect(peaky[0] / flat[0]).toBeCloseTo(5 / Math.SQRT1_2, 6);
    expect(peaky[1]).toBe(flat[1]);
  });
});

describe('cutGainDb', () => {
  it('Q is the gain exactly at the cutoff for every slope with a 2nd-order section', () => {
    for (const s of SLOPES_DB_OCT) {
      for (const qX100 of [71, 100, 500]) {
        const want = s === 6 ? -3.0103 : 20 * Math.log10(qX100 / 100);
        expect(cutGainDb('lp', cut(200, qX100, s), 200)).toBeCloseTo(want, 3);
        expect(cutGainDb('hp', cut(200, qX100, s), 200)).toBeCloseTo(want, 3);
      }
    }
  });

  it('falls by the slope per octave once on the asymptote', () => {
    for (const s of SLOPES_DB_OCT) {
      // HP at 1.6 kHz, octave 1/8 -> 1/16 of the cutoff (little warping down there).
      const d = cutGainDb('hp', cut(1600, 71, s), 200) - cutGainDb('hp', cut(1600, 71, s), 100);
      expect(d).toBeGreaterThan(s - 0.3);
      expect(d).toBeLessThan(s + 0.3);
    }
  });

  it('matches the firmware’s measured response', () => {
    // The firmware's FilterCascade.cpp compiled on the host and measured with sines (I/Q
    // demodulation), 2026-11-25. Over a 463-point grid (6 cuts × 6 slopes × 5–1000 Hz) the
    // largest difference from this model was 0.03 dB.
    expect(cutGainDb('lp', cut(200, 71, 12), 400)).toBeCloseTo(-12.3131, 2);
    expect(cutGainDb('lp', cut(200, 71, 48), 400)).toBeCloseTo(-48.27, 2);
    expect(cutGainDb('hp', cut(10, 71, 12), 5)).toBeCloseTo(-12.2879, 2);
    expect(cutGainDb('hp', cut(10, 500, 24), 10)).toBeCloseTo(13.9805, 2);
    expect(cutGainDb('lp', cut(200, 500, 36), 300)).toBeCloseTo(-19.8177, 2);
  });

  it('passband is flat with Q 0.71 (Butterworth) and peaks with a high Q', () => {
    expect(Math.abs(cutGainDb('lp', cut(200, 71, 24), 50))).toBeLessThan(0.01);
    expect(cutGainDb('lp', cut(200, 500, 12), 198)).toBeGreaterThan(13.9);
  });

  it('applies the firmware clamps: cutoff 1..7200 Hz, Q 0.1..20, unknown slope = 12', () => {
    expect(cutGainDb('hp', cut(0, 71, 12), 100)).toBeCloseTo(cutGainDb('hp', cut(1, 71, 12), 100), 9);
    expect(cutGainDb('lp', cut(8000, 71, 12), 100)).toBeCloseTo(cutGainDb('lp', cut(7200, 71, 12), 100), 9);
    expect(cutGainDb('lp', cut(200, 5000, 12), 200)).toBeCloseTo(20 * Math.log10(20), 6);
    expect(cutGainDb('lp', cut(200, 71, 30), 400)).toBeCloseTo(cutGainDb('lp', cut(200, 71, 12), 400), 9);
  });

  it('edges: LP passes DC and kills Nyquist, HP the other way round', () => {
    expect(cutGainDb('lp', cut(200, 71, 12), 0)).toBe(0);
    expect(cutGainDb('hp', cut(200, 71, 12), 0)).toBe(-Infinity);
    expect(cutGainDb('lp', cut(200, 71, 12), AUDIO_FS_HZ / 2)).toBe(-Infinity);
    expect(cutGainDb('hp', cut(200, 71, 12), AUDIO_FS_HZ / 2)).toBe(0);
  });
});

describe('band', () => {
  const band: BandSpec = { enabled: true, hp: cut(10, 71, 12), lp: cut(200, 71, 12) };

  it('is the sum of both cuts, and 0 dB when off', () => {
    expect(bandGainDb(band, 50)).toBeCloseTo(cutGainDb('hp', band.hp, 50) + cutGainDb('lp', band.lp, 50), 9);
    expect(bandGainDb({ ...band, enabled: false }, 5)).toBe(0);
  });

  it('isValidSlope accepts only the standard slopes', () => {
    for (const s of SLOPES_DB_OCT) expect(isValidSlope(s)).toBe(true);
    for (const s of [0, 7, 30, 42, 60]) expect(isValidSlope(s)).toBe(false);
  });

  it('responseCurve spans the axis, log-spaced on a log axis', () => {
    const log = responseCurve(band, [10, 1000], true, 5);
    expect(log.x.map((v) => Math.round(v))).toEqual([10, 32, 100, 316, 1000]);
    const lin = responseCurve(band, [0, 1000], false, 3);
    expect(lin.x).toEqual([0, 500, 1000]);
    expect(lin.y[0]).toBeNull(); // HP at 0 Hz is -inf: a gap, not a number
    expect(lin.y[1]).toBeCloseTo(bandGainDb(band, 500), 9);
  });
});
