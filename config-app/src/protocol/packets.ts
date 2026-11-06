// Decoders for the Gaiaball BLE notify packets. Wire format: firmware/docs/protocol.md
// (little-endian, packed). Decoders return null on any malformed input and never throw.

export const PROTOCOL_VERSION = 1;
export const PKT_IMU = 0x01;
export const PKT_CO2 = 0x02;
export const PKT_DIAG = 0x03;

export const IMU_PACKET_SIZE = 23;
export const CO2_PACKET_SIZE = 12;
export const DIAG_PACKET_SIZE = 23;

export const QUAT_SCALE = 16384; // Q14

export const IMU_FLAG_QUAT_VALID = 1 << 0;
export const IMU_FLAG_NINE_AXIS = 1 << 1;
export const IMU_FLAG_MOVING = 1 << 2;
export const CO2_FLAG_VALID = 1 << 0;
export const CO2_FLAG_STALE = 1 << 1;
export const CO2_FLAG_VIRTUAL_ACTIVE = 1 << 2;
export const DIAG_FLAG_IMU_OK = 1 << 0;
export const DIAG_FLAG_AUDIO_OK = 1 << 1;
export const DIAG_FLAG_CO2_OK = 1 << 2;

export interface ImuSample {
  seq: number;
  tUs: number;
  quat: { w: number; x: number; y: number; z: number };
  acc: [number, number, number];
  flags: number;
  quatValid: boolean;
  nineAxis: boolean;
  moving: boolean;
}

export interface Co2Sample {
  seq: number;
  tMs: number;
  ppm: number;
  flags: number;
  ageS: number;
  valid: boolean;
  stale: boolean;
  virtualActive: boolean;
}

export interface DiagSample {
  seq: number;
  uptimeS: number;
  imuHzX10: number;
  notifyHzX10: number;
  fifoOverflows: number;
  i2sErrors: number;
  uartFrameErrors: number;
  freeHeapKb: number;
  minFreeHeapKb: number;
  flags: number;
  imuOk: boolean;
  audioOk: boolean;
  co2Ok: boolean;
}

function header(dv: DataView, size: number, type: number): boolean {
  return dv.byteLength === size && dv.getUint8(0) === PROTOCOL_VERSION && dv.getUint8(1) === type;
}

export function decodeImuPacket(dv: DataView): ImuSample | null {
  if (!header(dv, IMU_PACKET_SIZE, PKT_IMU)) return null;
  const q = (o: number) => dv.getInt16(o, true) / QUAT_SCALE;
  const flags = dv.getUint8(22);
  return {
    seq: dv.getUint16(2, true),
    tUs: dv.getUint32(4, true),
    quat: { w: q(8), x: q(10), y: q(12), z: q(14) },
    acc: [dv.getInt16(16, true), dv.getInt16(18, true), dv.getInt16(20, true)],
    flags,
    quatValid: (flags & IMU_FLAG_QUAT_VALID) !== 0,
    nineAxis: (flags & IMU_FLAG_NINE_AXIS) !== 0,
    moving: (flags & IMU_FLAG_MOVING) !== 0,
  };
}

export function decodeCo2Packet(dv: DataView): Co2Sample | null {
  if (!header(dv, CO2_PACKET_SIZE, PKT_CO2)) return null;
  const flags = dv.getUint8(10);
  return {
    seq: dv.getUint16(2, true),
    tMs: dv.getUint32(4, true),
    ppm: dv.getUint16(8, true),
    flags,
    ageS: dv.getUint8(11),
    valid: (flags & CO2_FLAG_VALID) !== 0,
    stale: (flags & CO2_FLAG_STALE) !== 0,
    virtualActive: (flags & CO2_FLAG_VIRTUAL_ACTIVE) !== 0,
  };
}

export function decodeDiagPacket(dv: DataView): DiagSample | null {
  if (!header(dv, DIAG_PACKET_SIZE, PKT_DIAG)) return null;
  const flags = dv.getUint8(22);
  return {
    seq: dv.getUint16(2, true),
    uptimeS: dv.getUint32(4, true),
    imuHzX10: dv.getUint16(8, true),
    notifyHzX10: dv.getUint16(10, true),
    fifoOverflows: dv.getUint16(12, true),
    i2sErrors: dv.getUint16(14, true),
    uartFrameErrors: dv.getUint16(16, true),
    freeHeapKb: dv.getUint16(18, true),
    minFreeHeapKb: dv.getUint16(20, true),
    flags,
    imuOk: (flags & DIAG_FLAG_IMU_OK) !== 0,
    audioOk: (flags & DIAG_FLAG_AUDIO_OK) !== 0,
    co2Ok: (flags & DIAG_FLAG_CO2_OK) !== 0,
  };
}
