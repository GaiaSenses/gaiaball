// Binary encoders: the exact inverse of the decoders in packets.ts (same layouts, little-endian).
// Used by the ?demo=1 generator so the screen and the Pd bridge see real packet bytes.
// Flags are always derived from the boolean fields; the `flags` field of a sample is ignored.
import {
  CO2_FLAG_STALE,
  CO2_FLAG_TRIGGER,
  CO2_FLAG_VALID,
  CO2_FLAG_VIRTUAL_ACTIVE,
  CO2_PACKET_MIN_SIZE,
  IMU_FLAG_AUDIO_VALID,
  IMU_FLAG_BREATH,
  IMU_FLAG_MOVING,
  IMU_FLAG_NINE_AXIS,
  IMU_FLAG_QUAT_VALID,
  IMU_PACKET_MIN_SIZE,
  IMU_PACKET_VERSION,
  NAV_FLAG_MOVING,
  NAV_FLAG_SETTLED,
  NAV_FLAG_VALID,
  NAV_ANCHOR_SEQ_OFFSET,
  NAV_PACKET_MIN_SIZE,
  PKT_CO2,
  PKT_IMU,
  PKT_NAV,
  PKT_SENSORS,
  PROTOCOL_VERSION,
  QUAT_SCALE,
  SENSORS_PACKET_MIN_SIZE,
  SENS_FLAG_ADAPTIVE,
  SENS_FLAG_AUDIO_ENABLED,
  SENS_FLAG_AUDIO_FILTERED,
  SENS_FLAG_AUDIO_OK,
  SENS_FLAG_METRIC_PEAK,
  SENS_FLAG_POT_CONTROLS,
  type Co2Sample,
  type ImuSample,
  type NavSample,
  type SensorsSample,
  SENSORS_PACKET_V10_SIZE,
  BREATH_GATE_OVER,
  BREATH_GATE_COHERENT,
  BREATH_GATE_HANDLING,
  BREATH_GATE_WAITING,
} from './packets.ts';

const clamp = (v: number, lo: number, hi: number): number => Math.min(hi, Math.max(lo, v));
const bits = (pairs: [boolean, number][]): number => pairs.reduce((f, [on, bit]) => (on ? f | bit : f), 0);
/** dBFS -> u8 half-dB: dBFS = v / 2 - 120. */
const halfDb = (db: number): number => clamp(Math.round((db + 120) * 2), 0, 255);

function start(size: number, type: number, version: number, seq: number): DataView {
  const dv = new DataView(new ArrayBuffer(size));
  dv.setUint8(0, version);
  dv.setUint8(1, type);
  dv.setUint16(2, seq & 0xffff, true);
  return dv;
}
const bytes = (dv: DataView): Uint8Array => new Uint8Array(dv.buffer);

export function encodeImuPacket(s: Omit<ImuSample, 'flags'> & { flags?: number }): Uint8Array {
  const dv = start(IMU_PACKET_MIN_SIZE, PKT_IMU, IMU_PACKET_VERSION, s.seq);
  dv.setUint32(4, s.tUs >>> 0, true);
  const q = (o: number, v: number): void => dv.setInt16(o, clamp(Math.round(v * QUAT_SCALE), -32768, 32767), true);
  q(8, s.quat.w); q(10, s.quat.x); q(12, s.quat.y); q(14, s.quat.z);
  for (let i = 0; i < 3; i++) dv.setInt16(16 + 2 * i, clamp(Math.round(s.acc[i]), -32768, 32767), true);
  dv.setUint8(22, bits([
    [s.quatValid, IMU_FLAG_QUAT_VALID], [s.nineAxis, IMU_FLAG_NINE_AXIS], [s.moving, IMU_FLAG_MOVING],
    [s.audioValid, IMU_FLAG_AUDIO_VALID], [s.breath, IMU_FLAG_BREATH],
  ]));
  dv.setUint8(23, halfDb(s.audioRmsDbfs[0]));
  dv.setUint8(24, halfDb(s.audioRmsDbfs[1]));
  dv.setUint8(25, halfDb(s.audioPeakDbfs[0]));
  dv.setUint8(26, halfDb(s.audioPeakDbfs[1]));
  return bytes(dv);
}

export function encodeCo2Packet(s: Omit<Co2Sample, 'flags'> & { flags?: number }): Uint8Array {
  const dv = start(CO2_PACKET_MIN_SIZE, PKT_CO2, PROTOCOL_VERSION, s.seq);
  dv.setUint32(4, s.tMs >>> 0, true);
  dv.setUint16(8, clamp(Math.round(s.ppm), 0, 0xffff), true);
  dv.setUint8(10, bits([
    [s.valid, CO2_FLAG_VALID], [s.stale, CO2_FLAG_STALE],
    [s.virtualActive, CO2_FLAG_VIRTUAL_ACTIVE], [s.trigger, CO2_FLAG_TRIGGER],
  ]));
  dv.setUint8(11, clamp(Math.round(s.ageS), 0, 255));
  return bytes(dv);
}

export function encodeNavPacket(s: Omit<NavSample, 'flags'> & { flags?: number }): Uint8Array {
  const dv = start(NAV_PACKET_MIN_SIZE, PKT_NAV, PROTOCOL_VERSION, s.seq);
  dv.setUint32(4, s.tUs >>> 0, true);
  dv.setUint8(8, s.mode);
  dv.setUint8(9, bits([[s.valid, NAV_FLAG_VALID], [s.settled, NAV_FLAG_SETTLED], [s.moving, NAV_FLAG_MOVING]]));
  dv.setInt32(10, Math.round(s.lat * 1e7), true);
  dv.setInt32(14, Math.round(s.lon * 1e7), true);
  dv.setInt16(18, clamp(Math.round(s.bearingDeg * 100), -32768, 32767), true);
  dv.setUint16(20, clamp(Math.round(s.speedDegS * 10), 0, 0xffff), true);
  dv.setUint8(NAV_ANCHOR_SEQ_OFFSET, s.anchorSeq & 0xff);
  return bytes(dv);
}

/** The v10 packet (25 bytes) when the sample has a coherence or a gate, else the 23-byte one. */
export function encodeSensorsPacket(s: Omit<SensorsSample, 'flags'> & { flags?: number }): Uint8Array {
  const v10 = s.coherence !== null || s.gate !== null;
  const dv = start(v10 ? SENSORS_PACKET_V10_SIZE : SENSORS_PACKET_MIN_SIZE, PKT_SENSORS, PROTOCOL_VERSION, s.seq);
  dv.setUint32(4, s.tMs >>> 0, true);
  dv.setUint32(8, s.rmsL >>> 0, true);
  dv.setUint32(12, s.rmsR >>> 0, true);
  dv.setUint32(16, s.threshold >>> 0, true);
  dv.setUint16(20, clamp(Math.round(s.potRaw), 0, 0xffff), true);
  dv.setUint8(22, bits([
    [s.audioOk, SENS_FLAG_AUDIO_OK], [s.audioEnabled, SENS_FLAG_AUDIO_ENABLED],
    [s.potControls, SENS_FLAG_POT_CONTROLS], [s.audioFiltered, SENS_FLAG_AUDIO_FILTERED],
    [s.metricPeak, SENS_FLAG_METRIC_PEAK], [s.adaptive, SENS_FLAG_ADAPTIVE],
  ]));
  if (v10) {
    dv.setUint8(23, clamp(Math.round((s.coherence ?? 0) * 100), 0, 100));
    const g = s.gate;
    dv.setUint8(24, g ? bits([
      [g.over, BREATH_GATE_OVER], [g.coherent, BREATH_GATE_COHERENT],
      [g.handling, BREATH_GATE_HANDLING], [g.waiting, BREATH_GATE_WAITING],
    ]) : 0);
  }
  return bytes(dv);
}
