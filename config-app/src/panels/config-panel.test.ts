import { describe, expect, it } from 'vitest';
import { BASIC_FIELD_IDS, CONFIG_FIELDS, FIELD_DEFAULTS } from './config-panel';
import { encodeTlv, TLV } from '../protocol/tlv';

describe('config fields', () => {
  it('every field except the PIN has a default to pre-fill', () => {
    const missing = CONFIG_FIELDS.filter((f) => f.id !== TLV.pin && !FIELD_DEFAULTS.has(f.id));
    expect(missing.map((f) => f.label)).toEqual([]);
    // The PIN is write-only: showing a value would suggest it had been read back.
    expect(FIELD_DEFAULTS.has(TLV.pin)).toBe(false);
  });

  it('no default is left over from a removed field', () => {
    const ids = new Set(CONFIG_FIELDS.map((f) => f.id));
    expect([...FIELD_DEFAULTS.keys()].filter((id) => !ids.has(id))).toEqual([]);
  });

  it('every default sits inside the field range and encodes as TLV', () => {
    for (const f of CONFIG_FIELDS) {
      const d = FIELD_DEFAULTS.get(f.id);
      if (d === undefined) continue;
      if (typeof d === 'number') {
        if (f.min !== undefined) expect(d, f.label).toBeGreaterThanOrEqual(f.min);
        if (f.max !== undefined) expect(d, f.label).toBeLessThanOrEqual(f.max);
      }
      expect(() => encodeTlv([{ id: f.id, value: d }]), f.label).not.toThrow();
    }
  });

  it('a select default is one of its options', () => {
    for (const f of CONFIG_FIELDS) {
      if (f.kind !== 'select') continue;
      const d = FIELD_DEFAULTS.get(f.id);
      expect(f.options!.map(([v]) => v), f.label).toContain(d);
    }
  });

  it('splits the panel into a short basic tab and the tuning tab', () => {
    const basic = CONFIG_FIELDS.filter((f) => BASIC_FIELD_IDS.has(f.id));
    const advanced = CONFIG_FIELDS.filter((f) => !BASIC_FIELD_IDS.has(f.id));
    expect(basic.length + advanced.length).toBe(CONFIG_FIELDS.length);
    // The point of the split: the first screen stays readable.
    expect(basic.length).toBeLessThan(advanced.length);
    expect(basic.length).toBeLessThanOrEqual(16);
    // Operating the globe must not require the advanced tab.
    for (const id of [TLV.orientationMode, TLV.imuNotifyHz, TLV.thresholdFixed, TLV.bleName, TLV.pin]) {
      expect(BASIC_FIELD_IDS.has(id)).toBe(true);
    }
    // Signal-chain tuning must not clutter the first screen.
    for (const id of [TLV.fusionGainX100, TLV.biasTauS, TLV.imuDlpfMode, TLV.oneEuroBetaX1000, TLV.audioHpQX100]) {
      expect(BASIC_FIELD_IDS.has(id)).toBe(false);
    }
  });

  it('every basic id is a real field', () => {
    const ids = new Set(CONFIG_FIELDS.map((f) => f.id));
    expect([...BASIC_FIELD_IDS].filter((id) => !ids.has(id))).toEqual([]);
  });
});
