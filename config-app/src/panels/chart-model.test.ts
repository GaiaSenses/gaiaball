import { describe, expect, it } from 'vitest';
import {
  DEFAULT_WINDOW_S,
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
