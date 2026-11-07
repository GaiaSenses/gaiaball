// Decoders for the Gaiaball BLE notify packets. Wire format: firmware/docs/protocol.md
// (little-endian, packed). Decoders return null on any malformed input and never throw.

export const PROTOCOL_VERSION = 1;
export const PKT_IMU = 0x01;
export const PKT_CO2 = 0x02;
export const PKT_DIAG = 0x03;
export const PKT_SENSORS = 0x04;
export const PKT_AUDIO = 0x05;

export const IMU_PACKET_SIZE = 23;
export const CO2_PACKET_SIZE = 12;
export const DIAG_PACKET_SIZE = 23;
export const SENSORS_PACKET_SIZE = 23;
export const AUDIO_PACKET_SIZE = 208;
export const AUDIO_CHUNK_SAMPLES = 96;

export const QUAT_SCALE = 16384; // Q14
/** DMP accel full scale is ±4 g (SparkFun initializeDMP) -> 8192 LSB/g. */
export const ACCEL_LSB_PER_G = 8192;
/** Audio RMS counts: INMP441 24-bit samples (int32 >> 8), full-scale amplitude 2^23. */
export const AUDIO_FULL_SCALE = 8388608;
/** AES17 dBFS: 0 dBFS = RMS of a full-scale sine (FS/sqrt(2)). Floor at -120 dBFS. */
export function rmsToDbfs(rms: number): number {
  if (!(rms > 0)) return -120;
  return Math.max(-120, 20 * Math.log10((rms * Math.SQRT2) / AUDIO_FULL_SCALE));
}

export const IMU_FLAG_QUAT_VALID = 1 << 0;
export const IMU_FLAG_NINE_AXIS = 1 << 1;
export const IMU_FLAG_MOVING = 1 << 2;
export const CO2_FLAG_VALID = 1 << 0;
export const CO2_FLAG_STALE = 1 << 1;
export const CO2_FLAG_VIRTUAL_ACTIVE = 1 << 2;
export const DIAG_FLAG_IMU_OK = 1 << 0;
export const DIAG_FLAG_AUDIO_OK = 1 << 1;
export const DIAG_FLAG_CO2_OK = 1 << 2;
export const SENS_FLAG_AUDIO_OK = 1 << 0;
export const SENS_FLAG_AUDIO_ENABLED = 1 << 1;
export const SENS_FLAG_POT_CONTROLS = 1 << 2;
export const SENS_FLAG_AUDIO_FILTERED = 1 << 3;
export const AUDIO_FLAG_BPF_ENABLED = 1 << 0;

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

export interface SensorsSample {
  seq: number;
  tMs: number;
  /** Audio RMS in 24-bit counts (left / right); see rmsToDbfs for the dBFS value. */
  rmsL: number;
  rmsR: number;
  /** Peak-detector threshold, same RMS counts. */
  threshold: number;
  /** EMA-filtered potentiometer ADC reading, 0..4095. */
  potRaw: number;
  flags: number;
  audioOk: boolean;
  audioEnabled: boolean;
  potControls: boolean;
  /** RMS computed after the firmware HP/LP band-pass. */
  audioFiltered: boolean;
}

export interface AudioChunkSample {
  seq: number;
  tMs: number;
  sampleRateHz: number;
  /** RAW (unfiltered) mono samples, int16 (top 16 bits of the 24-bit audio). */
  samples: Int16Array;
  flags: number;
  bpfEnabled: boolean;
  /** Configured band-pass cutoffs, echoed by the firmware. */
  hpHz: number;
  lpHz: number;
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

export function decodeSensorsPacket(dv: DataView): SensorsSample | null {
  if (!header(dv, SENSORS_PACKET_SIZE, PKT_SENSORS)) return null;
  const flags = dv.getUint8(22);
  return {
    seq: dv.getUint16(2, true),
    tMs: dv.getUint32(4, true),
    rmsL: dv.getUint32(8, true),
    rmsR: dv.getUint32(12, true),
    threshold: dv.getUint32(16, true),
    potRaw: dv.getUint16(20, true),
    flags,
    audioOk: (flags & SENS_FLAG_AUDIO_OK) !== 0,
    audioEnabled: (flags & SENS_FLAG_AUDIO_ENABLED) !== 0,
    potControls: (flags & SENS_FLAG_POT_CONTROLS) !== 0,
    audioFiltered: (flags & SENS_FLAG_AUDIO_FILTERED) !== 0,
  };
}

export function decodeAudioChunkPacket(dv: DataView): AudioChunkSample | null {
  if (!header(dv, AUDIO_PACKET_SIZE, PKT_AUDIO)) return null;
  const count = dv.getUint8(10);
  if (count !== AUDIO_CHUNK_SAMPLES) return null;
  const flags = dv.getUint8(11);
  const samples = new Int16Array(count);
  for (let i = 0; i < count; i++) samples[i] = dv.getInt16(16 + 2 * i, true);
  return {
    seq: dv.getUint16(2, true),
    tMs: dv.getUint32(4, true),
    sampleRateHz: dv.getUint16(8, true),
    samples,
    flags,
    bpfEnabled: (flags & AUDIO_FLAG_BPF_ENABLED) !== 0,
    hpHz: dv.getUint16(12, true),
    lpHz: dv.getUint16(14, true),
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
