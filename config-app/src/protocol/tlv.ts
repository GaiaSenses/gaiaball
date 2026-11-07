// Config TLV codec (characteristic ...0004): records of `id u8 | len u8 | value`, little-endian.
// Registry mirrors firmware/docs/protocol.md.

export const TLV = {
  orientationMode: 0x01,
  imuNotifyHz: 0x02, // 10..200; also drives the DMP sample rate
  slerpAmount: 0x03,
  accelLpAlphaX1000: 0x04, // 0 = off (raw)
  audioEnabled: 0x10,
  audioFastAlphaX1000: 0x11,
  audioSlowAlphaX1000: 0x12,
  debounceMs: 0x13,
  potControlsThreshold: 0x14,
  thresholdFixed: 0x15,
  audioBpfEnabled: 0x16,
  audioHpHz: 0x17,
  audioHpQX100: 0x18,
  audioLpHz: 0x19,
  audioLpQX100: 0x1a,
  potEmaAlphaX1000: 0x1b,
  co2Enabled: 0x20,
  co2ChecksumVariant: 0x21,
  co2StaleTimeoutMs: 0x22,
  virtualPeakPpm: 0x23,
  virtualDelayMs: 0x24,
  virtualHoldMs: 0x25,
  co2RawLogMode: 0x26,
  co2Protocol: 0x27, // 0 = Senseair S8 Modbus polling (board default), 1 = passive 0x42 0x4D frames
  co2EmaAlphaX1000: 0x28, // 1000 = no smoothing
  soundLedPin: 0x30,
  co2LedPin: 0x31,
  co2AlertPpm: 0x32,
  // 0x33 is reserved (was statusLedPin; removed with the legacy LED command).
  bleName: 0x40,
  pin: 0x41,
  serialLogEnabled: 0x50, // default 0: serial formatting steals CPU
} as const;

/** Output-capable ESP32 GPIOs (excludes SPI-flash 6–11 and nonexistent pins). Mirrors firmware isValidOutputGpio. */
export const VALID_OUTPUT_GPIOS: readonly number[] = [
  0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33,
];

export interface TlvWrite {
  id: number;
  value: number | string;
}

type Spec = { kind: 'u8' | 'u16' | 'u32' } | { kind: 'str'; min: number; max: number };

const U8: Spec = { kind: 'u8' };
const U16: Spec = { kind: 'u16' };
const U32: Spec = { kind: 'u32' };

const SPECS: Record<number, Spec> = {
  [TLV.orientationMode]: U8,
  [TLV.imuNotifyHz]: U8,
  [TLV.slerpAmount]: U8,
  [TLV.accelLpAlphaX1000]: U16,
  [TLV.audioBpfEnabled]: U8,
  [TLV.audioHpHz]: U16,
  [TLV.audioHpQX100]: U16,
  [TLV.audioLpHz]: U16,
  [TLV.audioLpQX100]: U16,
  [TLV.potEmaAlphaX1000]: U16,
  [TLV.co2EmaAlphaX1000]: U16,
  [TLV.serialLogEnabled]: U8,
  [TLV.audioEnabled]: U8,
  [TLV.audioFastAlphaX1000]: U16,
  [TLV.audioSlowAlphaX1000]: U16,
  [TLV.debounceMs]: U16,
  [TLV.potControlsThreshold]: U8,
  [TLV.thresholdFixed]: U32,
  [TLV.co2Enabled]: U8,
  [TLV.co2ChecksumVariant]: U8,
  [TLV.co2StaleTimeoutMs]: U16,
  [TLV.virtualPeakPpm]: U16,
  [TLV.virtualDelayMs]: U16,
  [TLV.virtualHoldMs]: U16,
  [TLV.co2RawLogMode]: U8,
  [TLV.co2Protocol]: U8,
  [TLV.soundLedPin]: U8,
  [TLV.co2LedPin]: U8,
  [TLV.co2AlertPpm]: U16,
  [TLV.bleName]: { kind: 'str', min: 1, max: 16 },
  [TLV.pin]: { kind: 'str', min: 4, max: 4 },
};

const MAX: Record<string, number> = { u8: 0xff, u16: 0xffff, u32: 0xffffffff };

/** Encode writes; throws RangeError on unknown ids or out-of-range/ill-typed values. */
export function encodeTlv(regs: TlvWrite[]): Uint8Array {
  const parts: number[] = [];
  for (const { id, value } of regs) {
    const spec = SPECS[id];
    if (!spec) throw new RangeError(`unknown TLV id 0x${id.toString(16)}`);
    if (spec.kind === 'str') {
      if (typeof value !== 'string') throw new RangeError(`TLV 0x${id.toString(16)} expects a string`);
      const bytes = new TextEncoder().encode(value);
      if (bytes.length < spec.min || bytes.length > spec.max) {
        throw new RangeError(`TLV 0x${id.toString(16)} length must be ${spec.min}..${spec.max}`);
      }
      parts.push(id, bytes.length, ...bytes);
    } else {
      if (typeof value !== 'number' || !Number.isInteger(value) || value < 0 || value > MAX[spec.kind]) {
        throw new RangeError(`TLV 0x${id.toString(16)} value out of ${spec.kind} range`);
      }
      const n = spec.kind === 'u8' ? 1 : spec.kind === 'u16' ? 2 : 4;
      parts.push(id, n);
      for (let i = 0; i < n; i++) parts.push(Math.floor(value / 2 ** (8 * i)) & 0xff);
    }
  }
  return Uint8Array.from(parts);
}

/** Decode records into id -> value view. Stops at a truncated record (never throws). */
export function decodeTlv(dv: DataView): Map<number, DataView> {
  const out = new Map<number, DataView>();
  let o = 0;
  while (o + 2 <= dv.byteLength) {
    const id = dv.getUint8(o);
    const len = dv.getUint8(o + 1);
    if (o + 2 + len > dv.byteLength) break;
    out.set(id, new DataView(dv.buffer, dv.byteOffset + o + 2, len));
    o += 2 + len;
  }
  return out;
}

/** Command write payload: opcode byte followed by optional argument bytes. */
export function buildCommand(opcode: number, arg?: Uint8Array): Uint8Array {
  const out = new Uint8Array(1 + (arg?.length ?? 0));
  out[0] = opcode;
  if (arg) out.set(arg, 1);
  return out;
}
