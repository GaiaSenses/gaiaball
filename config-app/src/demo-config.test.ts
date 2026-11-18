import { describe, expect, it } from 'vitest';
import { demoConfigRegs, demoUnlock, demoWrite } from './demo-config';
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

  it('starts unlocked, and a PIN locks or unlocks it like the ball (factory PIN 1234)', () => {
    const regs = demoConfigRegs();
    expect(regs.get(TLV.unlockState)!.getUint8(0)).toBe(1);
    demoUnlock(regs, new TextEncoder().encode('9999'));
    expect(regs.get(TLV.unlockState)!.getUint8(0)).toBe(0);
    demoUnlock(regs, new TextEncoder().encode('1234'));
    expect(regs.get(TLV.unlockState)!.getUint8(0)).toBe(1);
  });

  it('ignores writes while locked, like the ball', () => {
    const regs = demoConfigRegs();
    demoUnlock(regs, new TextEncoder().encode('0000'));
    demoWrite(regs, [{ id: TLV.imuNotifyHz, value: 77 }]);
    expect(regs.get(TLV.imuNotifyHz)!.getUint8(0)).toBe(50);
  });
});
