// Decoders for the Gaiaball BLE notify packets. Wire format: firmware/docs/protocol.md
// (little-endian, packed). Decoders return null on any malformed input and never throw.

export const PROTOCOL_VERSION = 1;
export const PKT_IMU = 0x01;
export const PKT_CO2 = 0x02;
export const PKT_DIAG = 0x03;
export const PKT_SENSORS = 0x04;
export const PKT_AUDIO = 0x05;
export const PKT_MAG = 0x06;
export const PKT_NAV = 0x07;

export const IMU_PACKET_SIZE = 31;
export const CO2_PACKET_SIZE = 12;
export const DIAG_PACKET_SIZE = 34;
export const SENSORS_PACKET_SIZE = 23;
export const AUDIO_PACKET_SIZE = 208;
export const AUDIO_CHUNK_SAMPLES = 96;
export const NAV_PACKET_SIZE = 22;
export const MAG_PACKET_SIZE = 106;
export const MAG_CHUNK_MAX_SAMPLES = 16;
/** Magnetometer capture wire scale: int16 in 0.1 uT. */
export const MAG_LSB_PER_UT = 10;

export const QUAT_SCALE = 16384; // Q14
/** MPU-9250 accel full scale is ±4 g -> 8192 LSB/g. */
export const ACCEL_LSB_PER_G = 8192;
/** Audio RMS counts: INMP441 24-bit samples (int32 >> 8), full-scale amplitude 2^23. */
export const AUDIO_FULL_SCALE = 8388608;
/** AES17 dBFS: 0 dBFS = RMS of a full-scale sine (FS/sqrt(2)). Floor at -120 dBFS. */
export function rmsToDbfs(rms: number): number {
  if (!(rms > 0)) return -120;
  return Math.max(-120, 20 * Math.log10((rms * Math.SQRT2) / AUDIO_FULL_SCALE));
}

/** Floor of the firmware's dBFS x100 wire fields (firmware logic/AudioFeatures.h). */
export const AUDIO_DB_FLOOR = -120;

export const IMU_FLAG_QUAT_VALID = 1 << 0;
export const IMU_FLAG_NINE_AXIS = 1 << 1;
export const IMU_FLAG_MOVING = 1 << 2;
export const IMU_FLAG_AUDIO_VALID = 1 << 3;
export const CO2_FLAG_VALID = 1 << 0;
export const CO2_FLAG_STALE = 1 << 1;
export const CO2_FLAG_VIRTUAL_ACTIVE = 1 << 2;
export const CO2_FLAG_TRIGGER = 1 << 3;
export const NAV_FLAG_VALID = 1 << 0;
export const NAV_FLAG_SETTLED = 1 << 1;
export const NAV_FLAG_MOVING = 1 << 2;
export const DIAG_FLAG_IMU_OK = 1 << 0;
export const DIAG_FLAG_AUDIO_OK = 1 << 1;
export const DIAG_FLAG_CO2_OK = 1 << 2;
// DiagSample.ahrsFlags: the live state of the IMU pipeline (firmware logic/Ahrs.h).
export const AHRS_FLAG_STARTUP = 1 << 0;
export const AHRS_FLAG_OVERRANGE = 1 << 1;
export const AHRS_FLAG_ACCEL_IGNORED = 1 << 2;
export const AHRS_FLAG_MAG_IGNORED = 1 << 3;
export const AHRS_FLAG_BIAS_AT_REST = 1 << 4;
export const AHRS_FLAG_BIAS_ADAPTING = 1 << 5;
export const AHRS_FLAG_MAG_CALIBRATED = 1 << 6;
export const AHRS_FLAG_MAG_CAPTURE = 1 << 7;
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
  /**
   * Microphone envelope measured in the firmware over the 20 ms window that ended
   * with this sample, at the full 16 kHz rate (left, right).
   * RMS is AES17 dBFS (0 dBFS = full-scale sine); peak is amplitude dBFS (0 dBFS =
   * full-scale sample), so the same sine reads 3.01 dB lower on the peak scale.
   * Both read AUDIO_DB_FLOOR while audioValid is false.
   */
  audioValid: boolean;
  audioRmsDbfs: [number, number];
  audioPeakDbfs: [number, number];
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
  /** CO2 above the trigger threshold (with hysteresis), decided in the firmware. */
  trigger: boolean;
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
  /** IMU pipeline state; see the AHRS_FLAG_* constants. */
  ahrsFlags: number;
  /** Disagreement between the estimated and measured inclination, in degrees. */
  accelErrDeg: number;
  /** Disagreement between the estimated and measured heading, in degrees. */
  magErrDeg: number;
  /** Estimated gyroscope zero-rate offset per axis, in dps. */
  gyroBiasDps: [number, number, number];
  /**
   * Connection interval granted by this browser's Bluetooth stack, in ms (0 =
   * unknown). The firmware asks for 7.5-15 ms; this is what it got. It caps every
   * notify rate: max notifications/s = (packets per connection event) / interval.
   */
  connItvlMs: number;
  startup: boolean;
  accelIgnored: boolean;
  magIgnored: boolean;
  biasAtRest: boolean;
  biasAdapting: boolean;
  magCalibrated: boolean;
  magCapturing: boolean;
}

export interface NavSample {
  seq: number;
  tUs: number;
  /** 0 off, 1 mirror, 2 joystick. */
  mode: number;
  flags: number;
  /** False on the 1 Hz keepalive sent while the mode is off or the IMU is unavailable. */
  valid: boolean;
  /** Authority for "parado": set while the globe is held still (even if `moving` is also set). */
  settled: boolean;
  /** Reported speed above the active stop threshold (instantaneous, no hold time). */
  moving: boolean;
  /** Degrees, positive north / east. */
  lat: number;
  lon: number;
  /** Mapbox bearing in degrees, (-180, 180]. */
  bearingDeg: number;
  /** Globe angular speed, deg/s. */
  speedDegS: number;
}

export interface MagChunkSample {
  seq: number;
  tMs: number;
  /** Raw (uncalibrated) magnetometer triplets in uT, chip frame. */
  samples: [number, number, number][];
  flags: number;
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
  const db = (o: number) => dv.getInt16(o, true) / 100;
  return {
    seq: dv.getUint16(2, true),
    tUs: dv.getUint32(4, true),
    quat: { w: q(8), x: q(10), y: q(12), z: q(14) },
    acc: [dv.getInt16(16, true), dv.getInt16(18, true), dv.getInt16(20, true)],
    flags,
    quatValid: (flags & IMU_FLAG_QUAT_VALID) !== 0,
    nineAxis: (flags & IMU_FLAG_NINE_AXIS) !== 0,
    moving: (flags & IMU_FLAG_MOVING) !== 0,
    audioValid: (flags & IMU_FLAG_AUDIO_VALID) !== 0,
    audioRmsDbfs: [db(23), db(25)],
    audioPeakDbfs: [db(27), db(29)],
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
    trigger: (flags & CO2_FLAG_TRIGGER) !== 0,
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
  const ahrsFlags = dv.getUint8(23);
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
    ahrsFlags,
    accelErrDeg: dv.getUint8(24),
    magErrDeg: dv.getUint8(25),
    gyroBiasDps: [
      dv.getInt16(26, true) / 100,
      dv.getInt16(28, true) / 100,
      dv.getInt16(30, true) / 100,
    ],
    connItvlMs: dv.getUint16(32, true) / 10,
    startup: (ahrsFlags & AHRS_FLAG_STARTUP) !== 0,
    accelIgnored: (ahrsFlags & AHRS_FLAG_ACCEL_IGNORED) !== 0,
    magIgnored: (ahrsFlags & AHRS_FLAG_MAG_IGNORED) !== 0,
    biasAtRest: (ahrsFlags & AHRS_FLAG_BIAS_AT_REST) !== 0,
    biasAdapting: (ahrsFlags & AHRS_FLAG_BIAS_ADAPTING) !== 0,
    magCalibrated: (ahrsFlags & AHRS_FLAG_MAG_CALIBRATED) !== 0,
    magCapturing: (ahrsFlags & AHRS_FLAG_MAG_CAPTURE) !== 0,
  };
}

/** Raw magnetometer capture (diag characteristic, only while a calibration runs). */
export function decodeMagChunkPacket(dv: DataView): MagChunkSample | null {
  if (!header(dv, MAG_PACKET_SIZE, PKT_MAG)) return null;
  const count = dv.getUint8(8);
  if (count > MAG_CHUNK_MAX_SAMPLES) return null;
  const samples: [number, number, number][] = [];
  for (let i = 0; i < count; i++) {
    const o = 10 + 6 * i;
    samples.push([
      dv.getInt16(o, true) / MAG_LSB_PER_UT,
      dv.getInt16(o + 2, true) / MAG_LSB_PER_UT,
      dv.getInt16(o + 4, true) / MAG_LSB_PER_UT,
    ]);
  }
  return { seq: dv.getUint16(2, true), tMs: dv.getUint32(4, true), samples, flags: dv.getUint8(9) };
}

/** Globe navigation (nav characteristic, optional in older firmware). */
export function decodeNav(dv: DataView): NavSample | null {
  if (!header(dv, NAV_PACKET_SIZE, PKT_NAV)) return null;
  const flags = dv.getUint8(9);
  return {
    seq: dv.getUint16(2, true),
    tUs: dv.getUint32(4, true),
    mode: dv.getUint8(8),
    flags,
    valid: (flags & NAV_FLAG_VALID) !== 0,
    settled: (flags & NAV_FLAG_SETTLED) !== 0,
    moving: (flags & NAV_FLAG_MOVING) !== 0,
    lat: dv.getInt32(10, true) / 1e7,
    lon: dv.getInt32(14, true) / 1e7,
    bearingDeg: dv.getInt16(18, true) / 100,
    speedDegS: dv.getUint16(20, true) / 10,
  };
}
