import { describe, expect, it } from 'vitest';
import {
  DEFAULT_SPECTRUM_SCALE, SPECTRUM_SCALE_KEY, SPEC_DBFS_RANGE, SPEC_GAIN_RANGE, bandKey, loadSpectrumScale,
  saveSpectrumScale, specXRange,
} from './spectrum-model';

const store = (v: string | null) => ({ getItem: () => v });

describe('spectrum scale', () => {
  it('log starts at 10 Hz, linear at 0, both end at 1000 Hz', () => {
    expect(specXRange('log')).toEqual([10, 1000]);
    expect(specXRange('linear')).toEqual([0, 1000]);
  });

  it('defaults to log for missing, unknown and unreadable storage', () => {
    expect(DEFAULT_SPECTRUM_SCALE).toBe('log');
    expect(loadSpectrumScale(null)).toBe('log');
    expect(loadSpectrumScale(store(null))).toBe('log');
    expect(loadSpectrumScale(store('dB'))).toBe('log');
    expect(loadSpectrumScale({ getItem: () => { throw new Error('blocked'); } })).toBe('log');
    expect(loadSpectrumScale(store('linear'))).toBe('linear');
  });

  it('saves under its key and survives blocked storage', () => {
    const saved: Record<string, string> = {};
    saveSpectrumScale({ setItem: (k, v) => { saved[k] = v; } }, 'linear');
    expect(saved[SPECTRUM_SCALE_KEY]).toBe('linear');
    expect(() => saveSpectrumScale({ setItem: () => { throw new Error('blocked'); } }, 'log')).not.toThrow();
  });
});

describe('spectrum axes', () => {
  it('both y axes span 120 dB, so 20 dB ticks share the grid (0 dB gain on -20 dBFS)', () => {
    expect(SPEC_DBFS_RANGE[1] - SPEC_DBFS_RANGE[0]).toBe(120);
    expect(SPEC_GAIN_RANGE[1] - SPEC_GAIN_RANGE[0]).toBe(120);
    expect(Math.abs(SPEC_GAIN_RANGE[0] % 20)).toBe(0);
  });
});

describe('bandKey', () => {
  const band = { enabled: true, hp: { hz: 10, qX100: 71, slopeDbOct: 12 }, lp: { hz: 200, qX100: 71, slopeDbOct: 12 } };

  it('changes with every parameter that moves the response', () => {
    const k = bandKey(band);
    expect(bandKey({ ...band })).toBe(k);
    expect(bandKey({ ...band, enabled: false })).not.toBe(k);
    expect(bandKey({ ...band, hp: { ...band.hp, slopeDbOct: 24 } })).not.toBe(k);
    expect(bandKey({ ...band, lp: { ...band.lp, qX100: 500 } })).not.toBe(k);
    expect(bandKey({ ...band, lp: { ...band.lp, hz: 300 } })).not.toBe(k);
  });
});
