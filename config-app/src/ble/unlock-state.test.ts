import { describe, expect, it } from 'vitest';
import { bleErrorMessage, lockBadgeText, lockStateFrom, unlockResultMessage } from './unlock-state';
import { TLV } from '../protocol/tlv';

const regs = (bytes: number[] | null): Map<number, DataView> => {
  const m = new Map<number, DataView>([[TLV.imuNotifyHz, new DataView(new Uint8Array([50]).buffer)]]);
  if (bytes) m.set(TLV.unlockState, new DataView(new Uint8Array(bytes).buffer));
  return m;
};

describe('lockStateFrom (read-only TLV 0x37)', () => {
  it('maps the firmware byte to a state', () => {
    expect(TLV.unlockState).toBe(0x37);
    expect(lockStateFrom(regs([0]))).toBe('locked');
    expect(lockStateFrom(regs([1]))).toBe('unlocked');
    expect(lockStateFrom(regs([2]))).toBe('locked-out');
  });

  it('is unknown for firmware without 0x37, or a malformed record', () => {
    expect(lockStateFrom(regs(null))).toBe('unknown');
    expect(lockStateFrom(regs([1, 0]))).toBe('unknown');
    expect(lockStateFrom(regs([7]))).toBe('unknown');
  });
});

describe('unlock messages', () => {
  it('says plainly whether the PIN worked', () => {
    expect(unlockResultMessage('unlocked')).toMatch(/^Desbloqueada/);
    expect(unlockResultMessage('locked')).toMatch(/PIN incorreto/);
    expect(unlockResultMessage('locked')).toMatch(/1234/);
    expect(unlockResultMessage('locked-out')).toMatch(/30 s/);
    expect(unlockResultMessage('unknown')).toMatch(/não confirma/);
  });

  it('labels the badge, and shows none when the ball cannot tell', () => {
    expect(lockBadgeText('locked')).toBe('Bloqueada');
    expect(lockBadgeText('unlocked')).toBe('Desbloqueada');
    expect(lockBadgeText('locked-out')).toBe('Bloqueada por 30 s');
    expect(lockBadgeText('unknown')).toBe('');
  });
});

describe('bleErrorMessage', () => {
  const LINK = /A bola não respondeu pelo Bluetooth/;
  it('explains a failed GATT operation (the link dropped) in Portuguese, keeping the original detail', () => {
    const e = new DOMException('GATT operation failed for unknown reason.', 'NetworkError');
    const m = bleErrorMessage(e);
    expect(m).toMatch(LINK);
    expect(m).toMatch(/desbloqueie outra vez/);
    expect(m).toMatch(/GATT operation failed for unknown reason\./);
  });

  it('covers the other GATT failures Chrome reports', () => {
    expect(bleErrorMessage(new DOMException('GATT Server is disconnected. Cannot perform GATT operations.', 'NetworkError'))).toMatch(LINK);
    expect(bleErrorMessage(new Error('GATT Error Unknown.'))).toMatch(LINK);
  });

  it('leaves the app’s own messages alone', () => {
    expect(bleErrorMessage(new Error('Não conectado.'))).toBe('Não conectado.');
    expect(bleErrorMessage(new Error('O PIN deve ter 4 dígitos.'))).toBe('O PIN deve ter 4 dígitos.');
    expect(bleErrorMessage('falhou')).toBe('falhou');
  });
});
