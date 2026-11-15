import { describe, expect, it } from 'vitest';
import {
  DEFAULT_SPECTRUM_SCALE, SPECTRUM_SCALE_KEY, bandShading, loadSpectrumScale, saveSpectrumScale, specXRange,
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

describe('bandShading', () => {
  const lin = [0, 1000] as const;
  const log = [10, 1000] as const;

  it('shades below the high-pass and above the low-pass', () => {
    expect(bandShading({ enabled: true, hpHz: 10, lpHz: 200 }, lin)).toEqual([{ x0: 0, x1: 10 }, { x0: 200, x1: 1000 }]);
  });

  it('nothing below a high-pass at the left edge of the log axis', () => {
    expect(bandShading({ enabled: true, hpHz: 10, lpHz: 200 }, log)).toEqual([{ x0: 200, x1: 1000 }]);
  });

  it('cutoffs outside the axis are clamped or dropped', () => {
    expect(bandShading({ enabled: true, hpHz: 5, lpHz: 2000 }, log)).toEqual([]);
    expect(bandShading({ enabled: true, hpHz: 1500, lpHz: 3000 }, lin)).toEqual([{ x0: 0, x1: 1000 }]);
  });

  it('no shading with the band-pass off', () => {
    expect(bandShading({ enabled: false, hpHz: 10, lpHz: 200 }, lin)).toEqual([]);
  });

  it('a band with LP <= HP passes nothing', () => {
    expect(bandShading({ enabled: true, hpHz: 300, lpHz: 100 }, lin)).toEqual([{ x0: 0, x1: 1000 }]);
  });
});
