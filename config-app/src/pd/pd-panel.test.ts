import { describe, expect, it } from 'vitest';
import { pdStatusText } from './pd-panel';
import type { PdStatus } from './pd-bridge';

const st = (state: PdStatus['state'], wanted: boolean): PdStatus => ({ state, rate: 0, dropped: 0, wanted });

describe('pdStatusText', () => {
  it('says "desligado" only when the switch is off', () => {
    expect(pdStatusText(st('off', false))).toBe('Pd: desligado');
  });
  it('says the ball is awaited while the switch is on but BLE is down', () => {
    expect(pdStatusText(st('off', true))).toBe('Pd: aguardando a bola');
  });
  it('keeps the other states', () => {
    expect(pdStatusText(st('waiting', true))).toBe('Pd: aguardando');
    expect(pdStatusText({ ...st('connected', true), rate: 53 })).toBe('Pd: conectado · 53 pacotes/s');
    expect(pdStatusText(st('blocked', true))).toBe('Pd: bloqueado pelo Chrome');
  });
});
