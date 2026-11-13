import { describe, expect, it } from 'vitest';
import {
  BASIC_FIELD_IDS, CONFIG_FIELDS, FIELD_DEFAULTS, dlpfOptions, imuRebootPending, imuStatusText, isPerBall, navRefError, resolveOptions,
  stopRuleError, toWireRegs,
} from './config-panel';
import { decodeNavRef, decodeTlv, encodeTlv, navRefToString, parseNavRefString, TLV } from '../protocol/tlv';

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
      expect(() => encodeTlv(toWireRegs([{ id: f.id, value: d }])), f.label).not.toThrow();
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
    // 20, not 16: three joystick fields (max speed, full-scale angle, curve) are only visible in joystick mode.
    expect(basic.length).toBeLessThanOrEqual(20);
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

describe('navigation fields', () => {
  const field = (id: number) => CONFIG_FIELDS.find((f) => f.id === id)!;
  const NEW_IDS = [
    TLV.co2TriggerPpm, TLV.co2TriggerHystPpm, TLV.navMode, TLV.navNotifyHz, TLV.navRef, TLV.joyMaxSpeedDegS,
    TLV.joyFullAngleDeg, TLV.joyCurveX10, TLV.navStartDegS, TLV.navStopDegS, TLV.navStopMs,
    TLV.joyStartDegSX10, TLV.joyStopDegSX10,
  ];
  it('every new register is a field with a pt-BR tooltip and a firmware default', () => {
    for (const id of NEW_IDS) {
      expect(CONFIG_FIELDS.some((f) => f.id === id), String(id)).toBe(true);
      expect(field(id).info.length, field(id).label).toBeGreaterThan(60);
      expect(FIELD_DEFAULTS.has(id), field(id).label).toBe(true);
    }
  });
  it('defaults match the firmware table', () => {
    const want: [number, number | string][] = [
      [TLV.co2TriggerPpm, 1200], [TLV.co2TriggerHystPpm, 50], [TLV.navMode, 1], [TLV.navNotifyHz, 50],
      [TLV.navRef, '-22.905556,-47.060833'], [TLV.joyMaxSpeedDegS, 90], [TLV.joyFullAngleDeg, 60],
      [TLV.joyCurveX10, 30], [TLV.navStartDegS, 20], [TLV.navStopDegS, 5], [TLV.navStopMs, 420],
      [TLV.joyStartDegSX10, 10], [TLV.joyStopDegSX10, 5],
    ];
    for (const [id, v] of want) expect(FIELD_DEFAULTS.get(id), String(id)).toBe(v);
  });
  it('navMode offers off / mirror / joystick', () => {
    expect(resolveOptions(field(TLV.navMode), new Map())).toEqual([[0, 'Desligado'], [1, 'Espelho'], [2, 'Joystick']]);
  });
  it('placement: operating fields in Básico, tuning in Avançado', () => {
    for (const id of [TLV.navMode, TLV.joyMaxSpeedDegS, TLV.joyFullAngleDeg, TLV.joyCurveX10, TLV.co2TriggerPpm])
      expect(BASIC_FIELD_IDS.has(id), field(id).label).toBe(true);
    for (const id of [TLV.navNotifyHz, TLV.navRef, TLV.navStartDegS, TLV.navStopDegS, TLV.navStopMs,
      TLV.joyStartDegSX10, TLV.joyStopDegSX10, TLV.co2TriggerHystPpm])
      expect(BASIC_FIELD_IDS.has(id), field(id).label).toBe(false);
  });
  it('joystick sensitivity is visible only in joystick mode', () => {
    for (const id of [TLV.joyMaxSpeedDegS, TLV.joyFullAngleDeg, TLV.joyCurveX10]) {
      const vis = field(id).visible!;
      expect(vis(new Map([[TLV.navMode, 2]]))).toBe(true);
      expect(vis(new Map([[TLV.navMode, 1]]))).toBe(false);
      expect(vis(new Map([[TLV.navMode, 0]]))).toBe(false);
      expect(vis(new Map())).toBe(false);
    }
    expect(field(TLV.navMode).visible).toBeUndefined();
  });
  it('x10 registers are displayed in deg/s (scale 10)', () => {
    for (const id of [TLV.joyCurveX10, TLV.joyStartDegSX10, TLV.joyStopDegSX10]) expect(field(id).scale).toBe(10);
  });
  it('navRef never travels in a preset', () => {
    expect(isPerBall(field(TLV.navRef))).toBe(true);
    expect(isPerBall(field(TLV.navMode))).toBe(false);
  });
});

describe('stop < start rules', () => {
  const w = (id: number, value: number) => ({ id, value });
  it('passes when nothing relevant changes', () => {
    expect(stopRuleError([w(TLV.navNotifyHz, 50)], new Map())).toBeNull();
  });
  it('blocks mirror stop >= start, using the resulting state', () => {
    expect(stopRuleError([w(TLV.navStopDegS, 20)], new Map())).toMatch(/Espelho.*menor/); // default start 20
    expect(stopRuleError([w(TLV.navStopDegS, 19)], new Map())).toBeNull();
    // only the start moves: the stop comes from the snapshot
    expect(stopRuleError([w(TLV.navStartDegS, 5)], new Map<number, number | string>([[TLV.navStopDegS, 8]]))).toMatch(/Espelho/);
    expect(stopRuleError([w(TLV.navStartDegS, 30), w(TLV.navStopDegS, 25)], new Map())).toBeNull();
  });
  it('blocks joystick stop >= start with values in deg/s', () => {
    const e = stopRuleError([w(TLV.joyStopDegSX10, 10)], new Map()); // default start 10
    expect(e).toMatch(/Joystick.*menor/);
    expect(e).toContain('1,0');
    expect(stopRuleError([w(TLV.joyStopDegSX10, 9)], new Map())).toBeNull();
    expect(stopRuleError([w(TLV.joyStartDegSX10, 3)], new Map<number, number | string>([[TLV.joyStopDegSX10, 5]]))).toMatch(/Joystick/);
  });
});

describe('navRef string <-> bytes', () => {
  it('round-trips through the canonical string, the 8-byte record and the TLV', () => {
    for (const s of ['-22.905556,-47.060833', '0.000000,0.000000', '90.000000,-180.000000', '-90.000000,180.000000', '12.345679,-0.000001']) {
      const [wire] = toWireRegs([{ id: TLV.navRef, value: s }]);
      const enc = encodeTlv([wire]);
      const back = decodeNavRef(decodeTlv(new DataView(enc.buffer)).get(TLV.navRef)!)!;
      expect(navRefToString(back)).toBe(s);
    }
  });
  it('non-navRef registers pass through toWireRegs untouched', () => {
    const r = { id: TLV.navMode, value: 2 };
    expect(toWireRegs([r])[0]).toBe(r);
  });
  it('a typed value with extra decimals canonicalises to 6 and stays stable', () => {
    const ref = parseNavRefString('-22.9055559,-47.0608331')!;
    const once = navRefToString(ref);
    expect(once).toBe('-22.905556,-47.060833');
    expect(navRefToString(parseNavRefString(once)!)).toBe(once);
  });
  it('rejects out-of-range and malformed values in pt-BR', () => {
    expect(navRefError('Ref', '91.000000,0.000000')).toMatch(/latitude/);
    expect(navRefError('Ref', '0.000000,181.000000')).toMatch(/longitude/);
    expect(navRefError('Ref', 'x')).toMatch(/numéricas/);
    expect(navRefError('Ref', '-22.905556,-47.060833')).toBeNull();
  });
});
