import { describe, expect, it } from 'vitest';
import {
  BASIC_FIELD_IDS, CONFIG_FIELDS, FIELD_DEFAULTS, dlpfOptions, imuRebootPending, imuStatusText, isPerBall, resolveOptions,
} from './config-panel';
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
      expect(resolveOptions(f, new Map()).map(([v]) => v), f.label).toContain(d);
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

  it('lists imuChip in Básico and magAxisRemap in Avançado, both with a tooltip', () => {
    expect(BASIC_FIELD_IDS.has(TLV.imuChip)).toBe(true);
    expect(BASIC_FIELD_IDS.has(TLV.magAxisRemap)).toBe(false);
    for (const id of [TLV.imuChip, TLV.magAxisRemap]) {
      const f = CONFIG_FIELDS.find((x) => x.id === id)!;
      expect(f.kind).toBe('select');
      expect(f.info.length).toBeGreaterThan(40);
    }
    expect(CONFIG_FIELDS.find((x) => x.id === TLV.magAxisRemap)!.info).toMatch(/calibra/i);
  });

  it('FIELD_DEFAULTS has an entry for every new Field (factory ball: IMU off, remap +Y +X -Z)', () => {
    expect(FIELD_DEFAULTS.get(TLV.imuChip)).toBe(0);
    expect(FIELD_DEFAULTS.get(TLV.magAxisRemap)).toBe(8);
    expect(FIELD_DEFAULTS.has(TLV.imuDetected)).toBe(false); // read-only, never a Field
    expect(CONFIG_FIELDS.some((f) => f.id === TLV.imuDetected)).toBe(false);
  });

  it('offers 24 magnetometer axis options matching MagAxisRemap.cpp anchors', () => {
    const f = CONFIG_FIELDS.find((x) => x.id === TLV.magAxisRemap)!;
    const o = resolveOptions(f, new Map());
    expect(o).toHaveLength(24);
    const at = (i: number) => o.find(([v]) => v === i)![1];
    expect(at(0)).toBe('+X +Y +Z');
    expect(at(1)).toBe('+X -Y -Z');
    expect(at(4)).toBe('+X +Z -Y');
    expect(at(8)).toBe('+Y +X -Z');
    expect(at(12)).toBe('+Y +Z +X');
    expect(at(23)).toBe('-Z -Y -X');
  });
});

describe('presets', () => {
  const field = (id: number) => CONFIG_FIELDS.find((f) => f.id === id)!;
  it('keeps per-ball registers out of presets (PIN, mag calibration, imuChip, magAxisRemap)', () => {
    expect(isPerBall(field(TLV.imuChip))).toBe(true);
    expect(isPerBall(field(TLV.magAxisRemap))).toBe(true);
    expect(isPerBall(field(TLV.pin))).toBe(true);
    expect(isPerBall({ id: TLV.magCal, label: 'x', kind: 'number', info: 'x' })).toBe(true);
    expect(isPerBall(field(TLV.imuNotifyHz))).toBe(false);
    expect(isPerBall(field(TLV.imuDlpfMode))).toBe(false);
  });
});

describe('DLPF labels follow the chip', () => {
  it('labels the cut-offs of the selected chip', () => {
    expect(dlpfOptions(2)).toContainEqual([3, '41 Hz']); // MPU-9250
    expect(dlpfOptions(1)).not.toContainEqual([3, '41 Hz']); // ICM-20948
    expect(dlpfOptions(1)).toContainEqual([3, '51,2 Hz']);
    expect(dlpfOptions(1)).toContainEqual([0, 'automático']);
    expect(dlpfOptions(1).map(([i]) => i)).not.toContain(7); // 361,4 Hz: aliasing, not offered
    expect(dlpfOptions(2).map(([i]) => i)).not.toContain(7); // 3600 Hz
    expect(dlpfOptions(2)).toContainEqual([0, 'automático']);
  });
  it('the DLPF field resolves its options from the configuration', () => {
    const f = CONFIG_FIELDS.find((x) => x.id === TLV.imuDlpfMode)!;
    expect(resolveOptions(f, new Map([[TLV.imuChip, 1]]))).toEqual(dlpfOptions(1));
    expect(resolveOptions(f, new Map([[TLV.imuChip, 2]]))).toEqual(dlpfOptions(2));
    expect(f.info).not.toMatch(/do MPU-9250/);
  });
});

describe('reboot banner', () => {
  it('shows only while the selection differs from what the device reports', () => {
    expect(imuRebootPending(2, 0, undefined)).toBe(true);
    expect(imuRebootPending(0, 0, undefined)).toBe(false); // back to the value read
    expect(imuRebootPending(2, undefined, undefined)).toBe(false); // nothing read yet
  });
  it('stays after Aplicar: the stored value now equals the selection but the boot chip does not', () => {
    expect(imuRebootPending(2, 2, 0x05)).toBe(true); // booted ICM, now GY-91
    expect(imuRebootPending(1, 1, 0x05)).toBe(false);
  });
});

describe('IMU status line', () => {
  it.each([
    [undefined, 0, 'não configurado'],
    [0x0E, 2, 'GY-91 (MPU-9250) ok'],
    [0x0D, 1, 'ICM-20948 ok'],
    [0x05, 1, 'ICM-20948 ok (sem magnetômetro)'], // chip found, AK09916 not
    [0x06, 2, 'GY-91 (MPU-9250) ok (sem magnetômetro)'],
    [0x01, 1, 'ICM-20948 selecionado, não encontrado'],
    [0x09, 1, 'ICM-20948 selecionado, não encontrado'], // mag bit without chip: the chip verdict wins
    [0x05, 2, 'trocado, aguardando reinício'], // boot = ICM + found, current selection = GY-91
  ])('descreve o estado do IMU (%s, %s)', (detected, selected, want) => {
    expect(imuStatusText(detected, selected)).toBe(want);
  });
  it('does not accuse a good ball of a missing sensor right after a switch', () => {
    expect(imuStatusText(0x05, 2)).not.toMatch(/não encontrado/);
    expect(imuStatusText(0x05, 0)).toBe('trocado, aguardando reinício'); // switched back to off, still running ICM
  });
});
