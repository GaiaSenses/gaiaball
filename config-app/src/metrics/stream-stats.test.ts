import { describe, expect, it } from 'vitest';
import { StreamStats } from './stream-stats';

describe('StreamStats', () => {
  it('hz ~ 50 on a 50 Hz stream', () => {
    const s = new StreamStats();
    for (let i = 0; i < 200; i++) s.push(i, i * 20000, i * 20);
    expect(s.hz()).toBeCloseTo(50, 0);
    expect(s.jitterMs()).toBeCloseTo(0, 6);
    expect(s.lossPct()).toBe(0);
  });
  it('jitter reflects irregular arrivals', () => {
    const s = new StreamStats();
    let t = 0;
    for (let i = 0; i < 100; i++) { t += i % 2 ? 30 : 10; s.push(i, 0, t); }
    expect(s.jitterMs()).toBeGreaterThan(5);
  });
  it('no loss across 65535 -> 0 wrap', () => {
    const s = new StreamStats();
    [65533, 65534, 65535, 0, 1, 2].forEach((q, i) => s.push(q, 0, i * 20));
    expect(s.lossPct()).toBe(0);
  });
  it('counts loss across wrap', () => {
    const s = new StreamStats();
    s.push(65534, 0, 0);
    s.push(1, 0, 20); // 65535, 0 missing
    expect(s.lossPct()).toBeCloseTo(50, 5); // 2 lost of 4 expected
  });
  it('ageMs and reset', () => {
    const s = new StreamStats();
    s.push(0, 0, 1000);
    expect(s.ageMs(1250)).toBe(250);
    s.reset();
    expect(s.hz()).toBe(0);
    expect(s.ageMs(5)).toBe(Infinity);
  });
});
