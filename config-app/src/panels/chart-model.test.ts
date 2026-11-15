import { describe, expect, it } from 'vitest';
import {
  DEFAULT_WINDOW_S,
  HISTORY_S,
  MAX_DRAWN_POINTS,
  firstIndexAtOrAfter,
  minMaxDecimate,
  SampleQueue,
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
