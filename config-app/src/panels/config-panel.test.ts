import { describe, expect, it } from 'vitest';
import {
  BASIC_FIELD_IDS, CONFIG_FIELDS, FIELD_DEFAULTS, GOVERNORS, dlpfOptions, formatBallValue, governedOff, imuInUseName, imuRebootPending, imuStatusText, isPerBall, navRefError, resolveOptions,
  stopRuleError, toWireRegs, fromDisplay, toDisplay,
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
    // 23: +4 campos do detector de sopro, −1 limiar fixo em contagens.
    // 22: −1 atraso do pulso virtual (TLV 0x24 aposentado; o pulso sai no próprio sopro).
    // 21: −1 "Modo" (Quat6/Quat9): the app only offers Quat6, see the Quat9 notice in the DOM test.
    expect(basic.length).toBeLessThanOrEqual(21);
    expect(BASIC_FIELD_IDS.size).toBe(21);
    // Operating the globe must not require the advanced tab.
    for (const id of [TLV.imuNotifyHz, TLV.breathThresholdNegDbX100, TLV.bleName, TLV.pin]) {
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

  it('lists imuChip in Básico with a tooltip, and no magnetometer or Quat9 field exists', () => {
    expect(BASIC_FIELD_IDS.has(TLV.imuChip)).toBe(true);
    const f = CONFIG_FIELDS.find((x) => x.id === TLV.imuChip)!;
    expect(f.kind).toBe('select');
    expect(f.info.length).toBeGreaterThan(40);
    for (const id of [TLV.orientationMode, TLV.magAxisRemap, TLV.magRejectionDeg, TLV.magCal]) {
      expect(CONFIG_FIELDS.some((x) => x.id === id)).toBe(false);
      expect(FIELD_DEFAULTS.has(id)).toBe(false);
    }
  });

  it('FIELD_DEFAULTS has an entry for every new Field (factory ball: IMU off)', () => {
    expect(FIELD_DEFAULTS.get(TLV.imuChip)).toBe(0);
    expect(FIELD_DEFAULTS.has(TLV.imuDetected)).toBe(false); // read-only, never a Field
    expect(CONFIG_FIELDS.some((f) => f.id === TLV.imuDetected)).toBe(false);
  });

  it('never mentions the magnetometer in a tooltip (the app is Quat6 only)', () => {
    for (const f of CONFIG_FIELDS) expect(f.info, f.label).not.toMatch(/magnet|quat9/i);
  });
});

describe('presets', () => {
  const field = (id: number) => CONFIG_FIELDS.find((f) => f.id === id)!;
  it('keeps per-ball registers out of presets (PIN, imuChip, navRef)', () => {
    expect(isPerBall(field(TLV.imuChip))).toBe(true);
    expect(isPerBall(field(TLV.pin))).toBe(true);
    expect(isPerBall(field(TLV.navRef))).toBe(true);
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

describe('IMU choice', () => {
  // Some GY-91 boards carry an MPU-6500 (WHO_AM_I 0x70) instead of the MPU-9250; the firmware
  // drives both under the same choice (the missing magnetometer is irrelevant to a Quat6 app,
  // and tooltips never mention it: see 'never mentions the magnetometer').
  it('names both chips a GY-91 can carry and tells the operator to pick GY-91 for either', () => {
    const f = CONFIG_FIELDS.find((x) => x.id === TLV.imuChip)!;
    expect(f.options).toContainEqual([2, 'GY-91 (MPU-9250 ou MPU-6500)']);
    expect(f.info).toMatch(/MPU-6500 no lugar do MPU-9250: escolha GY-91/);
  });
});

describe('IMU status line', () => {
  it.each([
    [undefined, 0, 'não configurado'],
    [0x0E, 2, 'GY-91 (MPU-9250 ou MPU-6500) ok'],
    [0x0D, 1, 'ICM-20948 ok'],
    [0x05, 1, 'ICM-20948 ok'], // magnetometer bit (3) is no longer reported on
    [0x06, 2, 'GY-91 (MPU-9250 ou MPU-6500) ok'],
    [0x01, 1, 'ICM-20948 selecionado, não encontrado'],
    [0x09, 1, 'ICM-20948 selecionado, não encontrado'], // bit 3 without bit 2: the chip verdict wins
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


describe('breath detector fields', () => {
  const f = (id: number) => CONFIG_FIELDS.find((x) => x.id === id)!;

  it('new defaults match the firmware and encode', () => {
    expect(FIELD_DEFAULTS.get(TLV.breathThresholdNegDbX100)).toBe(3550);
    expect(FIELD_DEFAULTS.get(TLV.breathMetric)).toBe(0);
    expect(FIELD_DEFAULTS.get(TLV.breathDetector)).toBe(0);
    expect(FIELD_DEFAULTS.get(TLV.breathRearmDb)).toBe(6);
    expect(FIELD_DEFAULTS.get(TLV.debounceMs)).toBe(3000);
    for (const id of [TLV.breathThresholdNegDbX100, TLV.breathMetric, TLV.breathDetector, TLV.breathRearmDb]) {
      expect(() => encodeTlv(toWireRegs([{ id, value: FIELD_DEFAULTS.get(id)! }]))).not.toThrow();
    }
  });

  it('debounce is shown in seconds and stays in ms on the wire', () => {
    const d = f(TLV.debounceMs);
    expect(toDisplay(d, 3000)).toBe(3);
    expect(fromDisplay(d, 3)).toBe(3000);
    expect(fromDisplay(d, 0.3)).toBe(300);
    expect(fromDisplay(d, 2.5)).toBe(2500);
  });

  it('negate: 3550 <-> -35.5 and back', () => {
    const s = f(TLV.breathThresholdNegDbX100);
    expect(toDisplay(s, 3550)).toBeCloseTo(-35.5, 6);
    expect(fromDisplay(s, -35.5)).toBe(3550);
    expect(fromDisplay(s, 0)).toBe(0);
    expect(Object.is(fromDisplay(s, 0), 0)).toBe(true);
    expect(fromDisplay(s, -90)).toBe(9000);
  });

  it('integer-step fields never put a fraction on the wire', () => {
    const r = f(TLV.breathRearmDb);
    expect(fromDisplay(r, 2.5)).toBe(3);
    expect(fromDisplay(r, 2.4)).toBe(2);
    expect(fromDisplay({}, 2.5)).toBe(2.5);
  });

  it('negate without scale still flips the sign', () => {
    expect(toDisplay({ negate: true }, 5)).toBe(-5);
    expect(fromDisplay({ negate: true }, -5)).toBe(5);
  });

  it('alpha fields show only with the adaptive detector', () => {
    for (const id of [TLV.audioFastAlphaX1000, TLV.audioSlowAlphaX1000]) {
      const vis = f(id).visible!;
      expect(vis(new Map([[TLV.breathDetector, 0]]))).toBe(false);
      expect(vis(new Map([[TLV.breathDetector, 1]]))).toBe(true);
    }
  });

  it('sensitivity is enabled only with a fixed value', () => {
    const en = f(TLV.breathThresholdNegDbX100).enabled!;
    expect(en(new Map([[TLV.potControlsThreshold, 1]]))).toBe(false);
    expect(en(new Map([[TLV.potControlsThreshold, 0]]))).toBe(true);
  });

  it('switch fields have two options; detector describes both verbatim', () => {
    for (const id of [TLV.breathDetector, TLV.potControlsThreshold, TLV.breathMetric]) {
      expect(f(id).kind).toBe('switch');
      expect(f(id).switchOptions).toHaveLength(2);
    }
    expect(f(TLV.breathDetector).describe!(0)).toMatch(/^Dispara quando o som de qualquer microfone passa da linha\./);
    expect(f(TLV.breathDetector).describe!(1)).toMatch(/^Aprende o ruído da sala em cerca de 3 segundos/);
  });
});

describe('on/off fields and what they switch off', () => {
  const byId = new Map(CONFIG_FIELDS.map((f) => [f.id, f]));

  it('every governor is an on/off field and every governed id is a field', () => {
    for (const [gov, ids] of GOVERNORS) {
      expect(byId.get(gov)?.kind).toBe('checkbox');
      for (const id of ids) expect(byId.has(id)).toBe(true);
    }
  });

  it('the raw audio stream is an on/off toggle too', () => {
    expect(byId.get(TLV.audioRawStream)?.kind).toBe('checkbox');
  });

  it('a field is off only while one of its governors is 0', () => {
    expect(governedOff(TLV.audioHpHz, new Map([[TLV.audioBpfEnabled, 0]]))).toBe(true);
    expect(governedOff(TLV.audioHpHz, new Map([[TLV.audioBpfEnabled, 1]]))).toBe(false);
    // A value the form does not have yet counts as on.
    expect(governedOff(TLV.audioHpHz, new Map())).toBe(false);
    // The governor itself stays usable, or it could never be turned back on.
    expect(governedOff(TLV.audioEnabled, new Map([[TLV.audioEnabled, 0]]))).toBe(false);
  });

  it('the raw CO2 log needs both the sensor and the serial log', () => {
    const on = new Map([[TLV.co2Enabled, 1], [TLV.serialLogEnabled, 1]]);
    expect(governedOff(TLV.co2RawLogMode, on)).toBe(false);
    expect(governedOff(TLV.co2RawLogMode, new Map([...on, [TLV.serialLogEnabled, 0]]))).toBe(true);
    expect(governedOff(TLV.co2RawLogMode, new Map([...on, [TLV.co2Enabled, 0]]))).toBe(true);
  });

  it('follows the firmware: audioEnabled stops the detector, not the microphones', () => {
    const off = new Map([[TLV.audioEnabled, 0]]);
    expect(governedOff(TLV.breathThresholdNegDbX100, off)).toBe(true);
    expect(governedOff(TLV.debounceMs, off)).toBe(true);
    // Levels, band-pass and raw stream keep working with the detector off.
    expect(governedOff(TLV.audioBpfEnabled, off)).toBe(false);
    expect(governedOff(TLV.audioHpHz, off)).toBe(false);
    expect(governedOff(TLV.audioRawStream, off)).toBe(false);
  });

  it('follows the firmware: co2Enabled stops the real sensor, not the virtual pulse or the trigger', () => {
    const off = new Map([[TLV.co2Enabled, 0]]);
    expect(governedOff(TLV.co2EmaAlphaX1000, off)).toBe(true);
    expect(governedOff(TLV.co2StaleTimeoutMs, off)).toBe(true);
    expect(governedOff(TLV.virtualPeakPpm, off)).toBe(false);
    expect(governedOff(TLV.co2TriggerPpm, off)).toBe(false);
  });
});

describe('CO₂ virtual pulse fields', () => {
  const f = (id: number) => CONFIG_FIELDS.find((x) => x.id === id)!;

  it('has no delay field: the pulse starts on the breath itself (0x24 is retired)', () => {
    expect('virtualDelayMs' in TLV).toBe(false);
    expect(CONFIG_FIELDS.some((x) => x.id === 0x24)).toBe(false);
    expect(FIELD_DEFAULTS.has(0x24)).toBe(false);
    expect(() => encodeTlv([{ id: 0x24, value: 0 }])).toThrow(RangeError);
  });

  it('duration is shown in seconds (default 10 s) and stays in ms on the wire', () => {
    const d = f(TLV.virtualHoldMs);
    expect(d.label).toBe('Duração virtual (s)');
    expect(FIELD_DEFAULTS.get(TLV.virtualHoldMs)).toBe(10000);
    expect(toDisplay(d, 10000)).toBe(10);
    expect(fromDisplay(d, 10)).toBe(10000);
    expect(fromDisplay(d, 2.5)).toBe(2500);
    expect(BASIC_FIELD_IDS.has(TLV.virtualHoldMs)).toBe(true);
  });
});

describe('RMS window field (TLV 0x2F)', () => {
  const f = CONFIG_FIELDS.find((x) => x.id === TLV.audioWindowMs)!;

  it('lives in Avançado with the firmware range and default', () => {
    expect(f).toBeDefined();
    expect(BASIC_FIELD_IDS.has(TLV.audioWindowMs)).toBe(false);
    expect([f.min, f.max, f.step]).toEqual([5, 200, 5]);
    expect(FIELD_DEFAULTS.get(TLV.audioWindowMs)).toBe(50);
    expect(f.info).toMatch(/ms/);
  });

  it('encodes as a u16 and is not governed by the breath on/off', () => {
    const out = decodeTlv(new DataView(encodeTlv([{ id: TLV.audioWindowMs, value: 50 }]).buffer));
    expect(out.get(TLV.audioWindowMs)!.getUint16(0, true)).toBe(50);
    expect(governedOff(TLV.audioWindowMs, new Map([[TLV.audioEnabled, 0]]))).toBe(false);
  });
});

describe('formatBallValue ("na bola: X")', () => {
  const field = (id: number) => CONFIG_FIELDS.find((f) => f.id === id)!;

  it('shows a select by its option label', () => {
    expect(formatBallValue(field(TLV.navMode), 2)).toBe('Joystick');
    // DLPF labels follow the chip in the form.
    expect(formatBallValue(field(TLV.imuDlpfMode), 3, new Map([[TLV.imuChip, 2]]))).toBe('41 Hz');
  });

  it('shows a switch by its label and an on/off field as ligado/desligado', () => {
    const sw = field(TLV.breathDetector);
    expect(formatBallValue(sw, sw.switchOptions![1].value)).toBe(sw.switchOptions![1].label);
    expect(formatBallValue(field(TLV.audioEnabled), 1)).toBe('ligado');
    expect(formatBallValue(field(TLV.audioEnabled), 0)).toBe('desligado');
  });

  it('shows scaled and negated numbers with the input decimals and a pt-BR comma', () => {
    expect(formatBallValue(field(TLV.joyCurveX10), 25)).toBe('2,5');
    expect(formatBallValue(field(TLV.breathThresholdNegDbX100), 3550)).toBe('-35,50');
    expect(formatBallValue(field(TLV.imuNotifyHz), 50)).toBe('50');
  });

  it('shows a filter that is off as desligado, and text and reference as they read', () => {
    const f = CONFIG_FIELDS.find((x) => x.offValue !== undefined)!;
    expect(formatBallValue(f, f.offValue!)).toBe('desligado');
    expect(formatBallValue(field(TLV.bleName), 'Gaiaball')).toBe('Gaiaball');
    // pt-BR decimal commas, so the two coordinates are separated by a semicolon.
    expect(formatBallValue(field(TLV.navRef), '-22.905556,-47.060833')).toBe('-22,905556; -47,060833');
  });
});

describe('imuInUseName (status strip)', () => {
  it('names the chip running since boot', () => {
    expect(imuInUseName(0x01 | 0x04 | 0x08, 1)).toBe('ICM-20948');
    expect(imuInUseName(0x02 | 0x04, 2)).toBe('GY-91 (MPU-9250 ou MPU-6500)');
  });
  it('flags a chip that was not found, and an IMU that is off', () => {
    expect(imuInUseName(0x01, 1)).toBe('ICM-20948 (não encontrado)');
    expect(imuInUseName(0x00, 0)).toBe('não configurado');
  });
  it('without 0x35 says what the stored selection says', () => {
    expect(imuInUseName(undefined, 1)).toBe('ICM-20948 selecionado');
    expect(imuInUseName(undefined, 0)).toBe('não configurado');
  });
});
