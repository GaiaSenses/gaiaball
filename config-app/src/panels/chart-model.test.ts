import { describe, expect, it } from 'vitest';
import {
  DEFAULT_WINDOW_S,
  effectiveWindowMs,
  micNote,
  HISTORY_S,
  MAX_DRAWN_POINTS,
  firstIndexAtOrAfter,
  formatDb,
  formatSigned,
  levelFraction,
  minMaxDecimate,
  SampleQueue,
  co2Meter,
  co2Range,
  loadWindowS,
  saveWindowS,
  sparkRange,
  thresholdRows,
} from './chart-model.ts';

describe('SampleQueue', () => {
  it('keeps every packet that arrived between two ticks and drain empties it', () => {
    const q = new SampleQueue<number>();
    for (let i = 0; i < 5; i++) q.push(i * 0.02, i);
    const out = q.drain();
    expect(out).toHaveLength(5);
    expect(out[0]).toEqual({ t: 0, v: 0 });
    expect(out[4].v).toBe(4);
    expect(q.drain()).toEqual([]);
  });
  it('clear drops pending samples', () => {
    const q = new SampleQueue<number>();
    q.push(1, 1);
    q.clear();
    expect(q.drain()).toEqual([]);
  });
});

describe('window persistence', () => {
  const store = (v: string | null) => ({ getItem: () => v });
  it('falls back to 10 for null storage, invalid values (including the old 8 and 12) and errors', () => {
    expect(loadWindowS(null)).toBe(10);
    expect(loadWindowS(store(null))).toBe(DEFAULT_WINDOW_S);
    expect(loadWindowS(store('7'))).toBe(10);
    expect(loadWindowS(store('8'))).toBe(10);
    expect(loadWindowS(store('12'))).toBe(10);
    expect(loadWindowS({ getItem: () => { throw new Error('blocked'); } })).toBe(10);
  });
  it('reads a valid value', () => {
    expect(loadWindowS(store('60'))).toBe(60);
  });
  it('saves, tolerating null and errors', () => {
    let saved = '';
    saveWindowS({ setItem: (_k, v) => { saved = v; } }, 5);
    expect(saved).toBe('5');
    expect(() => saveWindowS(null, 5)).not.toThrow();
    expect(() => saveWindowS({ setItem: () => { throw new Error('x'); } }, 5)).not.toThrow();
  });
});

describe('ranges', () => {
  it('co2Range', () => {
    expect(co2Range(2100)).toEqual([0, 5000]);
    expect(co2Range(8000)).toEqual([0, 8000]);
  });
  it('sparkRange', () => {
    expect(sparkRange('rate', 50)).toEqual([0, 60]);
    expect(sparkRange('loss', 50)).toEqual([0, 100]);
  });
  it('thresholdRows follows the active metric', () => {
    expect(thresholdRows(true)).toEqual({ rms: false, peak: true });
    expect(thresholdRows(false)).toEqual({ rms: true, peak: false });
  });
});

describe('chart history and decimation', () => {
  it('keeps the largest window of history', () => {
    expect(HISTORY_S).toBe(60);
  });

  it('finds where the window starts', () => {
    const xs = [0, 1, 2, 3, 4];
    expect(firstIndexAtOrAfter(xs, 2)).toBe(2);
    expect(firstIndexAtOrAfter(xs, 2.5)).toBe(3);
    expect(firstIndexAtOrAfter(xs, -1)).toBe(0);
    expect(firstIndexAtOrAfter(xs, 9)).toBe(5);
    expect(firstIndexAtOrAfter([], 1)).toBe(0);
  });

  it('leaves short lines untouched', () => {
    const r = minMaxDecimate([0, 1, 2], [5, null, 7], 10);
    expect(r).toEqual({ x: [0, 1, 2], y: [5, null, 7] });
  });

  it('bounds the drawn points and keeps a one-sample spike', () => {
    const n = 12000;
    const xs = Array.from({ length: n }, (_, i) => i / 200);
    const ys: (number | null)[] = Array.from({ length: n }, () => 0);
    ys[7777] = 1; // a breath pulse lasting one packet
    const r = minMaxDecimate(xs, ys, MAX_DRAWN_POINTS);
    expect(r.x.length).toBeLessThanOrEqual(MAX_DRAWN_POINTS);
    expect(Math.max(...(r.y as number[]))).toBe(1);
    expect(r.x).toContain(xs[7777]);
    // time order is preserved
    expect(r.x.every((v, i) => i === 0 || v > r.x[i - 1])).toBe(true);
  });

  it('a stretch of gaps stays a gap; mixed stretches use the values', () => {
    const xs = Array.from({ length: 8 }, (_, i) => i);
    const r = minMaxDecimate(xs, [null, null, null, null, 3, null, 1, 2], 4);
    expect(r.y[0]).toBeNull();
    expect(r.y.slice(1)).toEqual([3, 1]);
  });
});

describe('levelFraction', () => {
  it('maps -90..0 dBFS onto 0..1 and clamps outside it', () => {
    expect(levelFraction(-90)).toBe(0);
    expect(levelFraction(0)).toBe(1);
    expect(levelFraction(-45)).toBeCloseTo(0.5, 9);
    expect(levelFraction(-120)).toBe(0);
    expect(levelFraction(6)).toBe(1);
    expect(levelFraction(Number.NaN)).toBe(0);
  });
  it('takes another floor for the potentiometer travel (-60..0)', () => {
    expect(levelFraction(-30, -60)).toBeCloseTo(0.5, 9);
  });
});

describe('reading formatters', () => {
  it('signed readings carry an explicit sign and no thousands separator', () => {
    expect(formatSigned(93.4)).toBe('+93');
    expect(formatSigned(-180)).toBe('\u2212180');
    expect(formatSigned(1009)).toBe('+1009');
    expect(formatSigned(-4000)).toBe('\u22124000');
    expect(formatSigned(0.4)).toBe('0');
    expect(formatSigned(-0.4)).toBe('0');
    expect(formatSigned(Number.NaN)).toBe('—');
  });
  it('the widest values fit the boxes the cards reserve (4 and 5 characters)', () => {
    expect(formatSigned(-180).length).toBe(4);
    expect(formatSigned(-4000).length).toBe(5);
    expect(formatDb(-120).length).toBe(4);
  });
  it('dBFS uses the typographic minus and never shows -0', () => {
    expect(formatDb(-57.4)).toBe('\u221257');
    expect(formatDb(-0.3)).toBe('0');
  });
});

describe('co2Meter', () => {
  const form = (triggerPpm: number | null, virtualPeakPpm: number | null = 2100) => ({ triggerPpm, virtualPeakPpm });

  it('marks the trigger from the form even with no ball (demo)', () => {
    expect(co2Meter(form(1200), null)).toEqual({ max: 5000, triggerPpm: 1200, pending: false });
  });
  it('follows the form while it is edited and flags a value the ball does not have yet', () => {
    expect(co2Meter(form(1500), form(1200))).toEqual({ max: 5000, triggerPpm: 1500, pending: true });
    expect(co2Meter(form(1200), form(1200))).toEqual({ max: 5000, triggerPpm: 1200, pending: false });
  });
  it('falls back to the ball when the form field is empty, and to no mark without either', () => {
    expect(co2Meter(form(null), form(1200))).toEqual({ max: 5000, triggerPpm: 1200, pending: false });
    expect(co2Meter(null, form(900))).toEqual({ max: 5000, triggerPpm: 900, pending: false });
    expect(co2Meter(null, null)).toEqual({ max: 5000, triggerPpm: null, pending: false });
  });
  it('stretches the scale to the virtual peak, like the chart', () => {
    expect(co2Meter(form(1200, 8000), null).max).toBe(8000);
    expect(co2Meter(form(1200, null), form(1200, 6000)).max).toBe(6000);
  });
});

describe('microphone window footnote (TLV 0x2F)', () => {
  it('says the window the ball reports, rounded like the firmware', () => {
    expect(effectiveWindowMs(50)).toBe(50);
    expect(effectiveWindowMs(52)).toBe(50);
    expect(effectiveWindowMs(53)).toBe(55);
    expect(micNote(effectiveWindowMs(120))).toMatch(/^RMS é a média de 120 ms;/);
  });

  it('firmware without the register still has the old fixed 20 ms window', () => {
    expect(effectiveWindowMs(null)).toBe(20);
  });

  it('clamps like the firmware', () => {
    expect(effectiveWindowMs(0)).toBe(5);
    expect(effectiveWindowMs(9999)).toBe(200);
  });

  it('before any read it names the default instead of a number it does not know', () => {
    expect(micNote(null)).toMatch(/50 ms por padrão/);
  });
});
