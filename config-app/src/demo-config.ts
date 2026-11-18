// A fake register map for ?demo=1, so the configuration panel (status strip, "na bola" marks,
// Aplicar (n)) can be tried without a ball. Factory defaults, ICM-20948 running.
import { CONFIG_FIELDS, FIELD_DEFAULTS, toWireRegs } from './panels/config-panel.ts';
import { decodeTlv, encodeTlv, TLV, type TlvWrite } from './protocol/tlv.ts';

const asView = (b: Uint8Array): DataView => new DataView(b.buffer, b.byteOffset, b.byteLength);

/**
 * What the ball's config characteristic would return: every field's default, plus the read-only
 * 0x35 (ICM-20948, found, with magnetometer) and 0x37 (this connection unlocked, so Aplicar works).
 */
export function demoConfigRegs(): Map<number, DataView> {
  const regs: TlvWrite[] = [];
  for (const f of CONFIG_FIELDS) {
    const d = FIELD_DEFAULTS.get(f.id);
    if (d !== undefined) regs.push({ id: f.id, value: f.id === TLV.imuChip ? 1 : d });
  }
  const body = encodeTlv(toWireRegs(regs));
  const out = new Uint8Array(body.length + 6);
  out.set(body);
  // Not writable registers, so they are appended by hand.
  out.set([TLV.imuDetected, 1, 0x01 | 0x04 | 0x08, TLV.unlockState, 1, 1], body.length);
  return decodeTlv(asView(out));
}

const locked = (regs: Map<number, DataView>): boolean => regs.get(TLV.unlockState)?.getUint8(0) === 0;

/** A write to the fake ball: the written registers replace the stored ones, unless it is locked. */
export function demoWrite(regs: Map<number, DataView>, writes: TlvWrite[]): void {
  if (locked(regs)) return;
  for (const [id, dv] of decodeTlv(asView(encodeTlv(writes)))) regs.set(id, dv);
}

/** The 0x20 command on the fake ball: the factory PIN 1234 unlocks, anything else locks. */
export function demoUnlock(regs: Map<number, DataView>, pin: Uint8Array | undefined): void {
  const ok = pin !== undefined && new TextDecoder().decode(pin) === '1234';
  regs.set(TLV.unlockState, asView(new Uint8Array([ok ? 1 : 0])));
}
