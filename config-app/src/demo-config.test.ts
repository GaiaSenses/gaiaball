import { describe, expect, it } from 'vitest';
import { demoConfigRegs, demoWrite } from './demo-config';
import { CONFIG_FIELDS, FIELD_DEFAULTS, imuInUseName } from './panels/config-panel';
import { TLV } from './protocol/tlv';

describe('demo configuration', () => {
  it('reports every field that has a factory default, plus the IMU in use', () => {
    const regs = demoConfigRegs();
    for (const f of CONFIG_FIELDS) if (FIELD_DEFAULTS.has(f.id)) expect(regs.has(f.id), f.label).toBe(true);
    expect(regs.has(TLV.pin)).toBe(false);
    expect(imuInUseName(regs.get(TLV.imuDetected)!.getUint8(0), 1)).toBe('ICM-20948');
  });

  it('remembers what is written', () => {
    const regs = demoConfigRegs();
    demoWrite(regs, [{ id: TLV.imuNotifyHz, value: 77 }]);
    expect(regs.get(TLV.imuNotifyHz)!.getUint8(0)).toBe(77);
  });
});
