// Config TLV codec (characteristic ...0004): records of `id u8 | len u8 | value`, little-endian.
// Registry mirrors firmware/docs/protocol.md.

export const TLV = {
  orientationMode: 0x01,
  imuNotifyHz: 0x02, // 10..200; also drives the MPU-9250 sample rate
  slerpAmount: 0x03,
  accelLpAlphaX1000: 0x04, // 0 = off (raw)
  imuDlpfMode: 0x05, // 0 = auto (anti-alias picked from the rate)
  gyroFsSel: 0x06, // 0=+-250, 1=+-500, 2=+-1000, 3=+-2000 dps
  fusionGainX100: 0x07,
  accelRejectionDeg: 0x08, // 0 = off
  magRejectionDeg: 0x09, // 0 = off
  rejectionTimeoutS: 0x0a, // 0 = disables both rejections
  biasRestThDpsX100: 0x0b,
  biasMaxDpsX100: 0x0c,
  biasTauS: 0x0d,
  quatSmoothMode: 0x0e, // 0=off, 1=slerp fixo, 2=1 euro
  oneEuroFcMinX100: 0x0f,
  oneEuroBetaX1000: 0x1c,
  accelHampelKX10: 0x1d, // 0 = off
  magCal: 0x1e, // 24 bytes: 3x int16 hard iron (0.1 uT) + 9x int16 soft iron (1/4096)
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
  // 0x21 is reserved (was co2ChecksumVariant; the SC8-CO2-5K frame format is fixed).
  co2StaleTimeoutMs: 0x22,
  virtualPeakPpm: 0x23,
  virtualDelayMs: 0x24,
  virtualHoldMs: 0x25,
  co2RawLogMode: 0x26,
  // 0x27 is reserved (was co2Protocol; the SC8-CO2-5K streams actively, nothing to pick).
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
  value: number | string | Uint8Array;
}

type Spec =
  | { kind: 'u8' | 'u16' | 'u32' }
  | { kind: 'str'; min: number; max: number }
  | { kind: 'bytes'; len: number };

const U8: Spec = { kind: 'u8' };
const U16: Spec = { kind: 'u16' };
const U32: Spec = { kind: 'u32' };

const SPECS: Record<number, Spec> = {
  [TLV.orientationMode]: U8,
  [TLV.imuDlpfMode]: U8,
  [TLV.gyroFsSel]: U8,
  [TLV.fusionGainX100]: U16,
  [TLV.accelRejectionDeg]: U8,
  [TLV.magRejectionDeg]: U8,
  [TLV.rejectionTimeoutS]: U8,
  [TLV.biasRestThDpsX100]: U16,
  [TLV.biasMaxDpsX100]: U16,
  [TLV.biasTauS]: U8,
  [TLV.quatSmoothMode]: U8,
  [TLV.oneEuroFcMinX100]: U16,
  [TLV.oneEuroBetaX1000]: U16,
  [TLV.accelHampelKX10]: U8,
  [TLV.magCal]: { kind: 'bytes', len: 24 },
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
  [TLV.co2StaleTimeoutMs]: U16,
  [TLV.virtualPeakPpm]: U16,
  [TLV.virtualDelayMs]: U16,
  [TLV.virtualHoldMs]: U16,
  [TLV.co2RawLogMode]: U8,
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
    if (spec.kind === 'bytes') {
      if (!(value instanceof Uint8Array) || value.length !== spec.len) {
        throw new RangeError(`TLV 0x${id.toString(16)} expects ${spec.len} bytes`);
      }
      parts.push(id, spec.len, ...value);
    } else if (spec.kind === 'str') {
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

/**
 * Builds the 24-byte magnetometer calibration record (TLV 0x1E) from the fixed-point
 * arrays produced by magCalToFixed: hard iron x/y/z, then the soft-iron matrix
 * row-major, all little-endian int16.
 */
export function encodeMagCalRecord(hard: Int16Array, soft: Int16Array): Uint8Array {
  if (hard.length !== 3 || soft.length !== 9) throw new RangeError('magCal needs 3 + 9 values');
  const out = new Uint8Array(24);
  const dv = new DataView(out.buffer);
  for (let i = 0; i < 3; i++) dv.setInt16(2 * i, hard[i], true);
  for (let i = 0; i < 9; i++) dv.setInt16(6 + 2 * i, soft[i], true);
  return out;
}
