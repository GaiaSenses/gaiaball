import { describe, expect, it } from 'vitest';

import { computeSpectrum } from './fft';

describe('computeSpectrum', () => {
  it('finds a full-scale sine at its bin with ~0 dBFS', () => {
    // fs=2000, size=512 -> binHz=3.90625. Sine exactly on bin 20 (78.125 Hz).
    const fs = 2000;
    const size = 512;
    const f = (20 * fs) / size;
    const s = new Int16Array(size);
    for (let i = 0; i < size; i++) s[i] = Math.round(32767 * Math.sin((2 * Math.PI * f * i) / fs));
    const sp = computeSpectrum(s, size, fs);
    let peak = 0;
    for (let k = 1; k < sp.dbfs.length; k++) if (sp.dbfs[k] > sp.dbfs[peak]) peak = k;
    expect(peak).toBe(20);
    expect(sp.freqsHz[20]).toBeCloseTo(78.125, 3);
    expect(sp.dbfs[20]).toBeGreaterThan(-1);
    expect(sp.dbfs[20]).toBeLessThanOrEqual(0.1);
    // Leakage floor far from the peak is way down.
    expect(sp.dbfs[200]).toBeLessThan(sp.dbfs[20] - 60);
  });

  it('-20 dBFS sine lands ~-20 dB', () => {
    const fs = 2000;
    const size = 512;
    const f = (40 * fs) / size;
    const s = new Int16Array(size);
    for (let i = 0; i < size; i++) s[i] = Math.round(3276.8 * Math.sin((2 * Math.PI * f * i) / fs));
    const sp = computeSpectrum(s, size, fs);
    expect(sp.dbfs[40]).toBeCloseTo(-20, 0);
  });

  it('silence returns the floor everywhere', () => {
    const sp = computeSpectrum(new Int16Array(512), 512, 2000);
    for (const v of sp.dbfs) expect(v).toBe(-120);
  });

  it('rejects non-power-of-two sizes', () => {
    expect(() => computeSpectrum(new Int16Array(100), 100, 2000)).toThrow(RangeError);
  });
});
