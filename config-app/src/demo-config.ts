// A fake register map for ?demo=1, so the configuration panel (status strip, "na bola" marks,
// Aplicar (n)) can be tried without a ball. Factory defaults, ICM-20948 running.
import { CONFIG_FIELDS, FIELD_DEFAULTS, toWireRegs } from './panels/config-panel.ts';
import { decodeTlv, encodeTlv, TLV, type TlvWrite } from './protocol/tlv.ts';

const asView = (b: Uint8Array): DataView => new DataView(b.buffer, b.byteOffset, b.byteLength);

/** What the ball's config characteristic would return: every field's default, plus 0x35 (ICM-20948, found, with magnetometer). */
export function demoConfigRegs(): Map<number, DataView> {
  const regs: TlvWrite[] = [];
  for (const f of CONFIG_FIELDS) {
    const d = FIELD_DEFAULTS.get(f.id);
    if (d !== undefined) regs.push({ id: f.id, value: f.id === TLV.imuChip ? 1 : d });
  }
  const body = encodeTlv(toWireRegs(regs));
  const out = new Uint8Array(body.length + 3);
  out.set(body);
  out.set([TLV.imuDetected, 1, 0x01 | 0x04 | 0x08], body.length); // not a writable register, so it is appended by hand
  return decodeTlv(asView(out));
}

/** A write to the fake ball: the written registers replace the stored ones. */
export function demoWrite(regs: Map<number, DataView>, writes: TlvWrite[]): void {
  for (const [id, dv] of decodeTlv(asView(encodeTlv(writes)))) regs.set(id, dv);
}
