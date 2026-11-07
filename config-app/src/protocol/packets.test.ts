import { describe, expect, it } from 'vitest';
import { decodeCo2Packet, decodeDiagPacket, decodeImuPacket, decodeSensorsPacket, rmsToDbfs } from './packets';
import { TLV, VALID_OUTPUT_GPIOS, buildCommand, decodeTlv, encodeTlv } from './tlv';

const hex = (s: string) => new DataView(new Uint8Array(s.trim().split(/\s+/).map((b) => parseInt(b, 16))).buffer);

// Golden vectors copied verbatim from firmware/docs/protocol.md
const IMU = '01 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 01 00 FE FF 03 00 03';
const CO2 = '01 02 02 01 0D 0C 0B 0A E8 03 05 07';
const DIAG = '01 03 01 00 03 02 01 00 F4 01 F4 01 01 00 02 00 03 00 C8 00 96 00 07';
const SENSORS = '01 04 07 00 40 E2 01 00 39 30 00 00 A0 5B 00 00 A0 86 01 00 00 08 07';

describe('golden decode', () => {
  it('IMU', () => {
    const s = decodeImuPacket(hex(IMU))!;
    expect(s.seq).toBe(258);
    expect(s.tUs).toBe(0x0a0b0c0d);
    expect(s.quat).toEqual({ w: 1.0, x: 0, y: 0, z: 0 });
    expect(s.acc).toEqual([1, -2, 3]);
    expect(s.flags).toBe(0b011);
    expect([s.quatValid, s.nineAxis, s.moving]).toEqual([true, true, false]);
  });
  it('CO2', () => {
    const s = decodeCo2Packet(hex(CO2))!;
    expect(s).toMatchObject({ seq: 258, tMs: 0x0a0b0c0d, ppm: 1000, flags: 0b101, ageS: 7 });
    expect([s.valid, s.stale, s.virtualActive]).toEqual([true, false, true]);
  });
  it('Diag', () => {
    const s = decodeDiagPacket(hex(DIAG))!;
    expect(s).toMatchObject({
      seq: 1, uptimeS: 0x00010203, imuHzX10: 500, notifyHzX10: 500, fifoOverflows: 1,
      i2sErrors: 2, uartFrameErrors: 3, freeHeapKb: 200, minFreeHeapKb: 150, flags: 7,
    });
    expect([s.imuOk, s.audioOk, s.co2Ok]).toEqual([true, true, true]);
  });

  it('Sensors', () => {
    const s = decodeSensorsPacket(hex(SENSORS))!;
    expect(s).toMatchObject({
      seq: 7, tMs: 123456, rmsL: 12345, rmsR: 23456, threshold: 100000, potRaw: 2048, flags: 7,
    });
    expect([s.audioOk, s.audioEnabled, s.potControls]).toEqual([true, true, true]);
    // A diag packet is NOT a sensors packet and vice versa (same characteristic).
    expect(decodeSensorsPacket(hex(DIAG))).toBeNull();
    expect(decodeDiagPacket(hex(SENSORS))).toBeNull();
  });

  it('rmsToDbfs: AES17 reference points', () => {
    expect(rmsToDbfs(8388608 / Math.SQRT2)).toBeCloseTo(0, 6); // full-scale sine RMS
    expect(rmsToDbfs(0)).toBe(-120); // silence floor
    expect(rmsToDbfs(8388608 / Math.SQRT2 / 10)).toBeCloseTo(-20, 6);
  });
});

describe('invalid input returns null', () => {
  const dec = [decodeImuPacket, decodeCo2Packet, decodeDiagPacket];
  const good = [IMU, CO2, DIAG];
  it('truncated / extended / empty', () => {
    dec.forEach((d, i) => {
      const b = new Uint8Array(hex(good[i]).buffer);
      expect(d(new DataView(b.buffer, 0, b.length - 1))).toBeNull();
      expect(d(new DataView(new Uint8Array(b.length + 1).buffer))).toBeNull();
      expect(d(new DataView(new ArrayBuffer(0)))).toBeNull();
    });
  });
  it('wrong version / type', () => {
    dec.forEach((d, i) => {
      const v = new Uint8Array(hex(good[i]).buffer);
      v[0] = 2;
      expect(d(new DataView(v.buffer))).toBeNull();
      const t = new Uint8Array(hex(good[i]).buffer);
      t[1] = 0x7f;
      expect(d(new DataView(t.buffer))).toBeNull();
    });
  });
  it('cross-type buffers rejected', () => {
    expect(decodeImuPacket(hex(DIAG))).toBeNull();
    expect(decodeDiagPacket(hex(IMU))).toBeNull();
    expect(decodeCo2Packet(hex(IMU))).toBeNull();
  });
  it('fuzz never throws', () => {
    let x = 12345;
    const rnd = () => ((x = (x * 1103515245 + 12345) & 0x7fffffff) >> 8) & 0xff;
    for (let n = 0; n < 2000; n++) {
      const len = n % 30;
      const b = new Uint8Array(len).map(rnd);
      for (const d of dec) expect(() => d(new DataView(b.buffer))).not.toThrow();
    }
  });
});

describe('TLV', () => {
  it('registry ids', () => {
    expect(Object.keys(TLV)).toHaveLength(22);
    expect(TLV.orientationMode).toBe(0x01);
    expect(TLV.imuNotifyHz).toBe(0x02);
    expect(TLV.slerpAmount).toBe(0x03);
    expect(TLV.thresholdFixed).toBe(0x15);
    expect(TLV.co2AlertPpm).toBe(0x32);
    expect(TLV.co2Protocol).toBe(0x27);
    expect(TLV.bleName).toBe(0x40);
    expect(TLV.pin).toBe(0x41);
  });
  it('encode layout little-endian', () => {
    expect(Array.from(encodeTlv([{ id: TLV.imuNotifyHz, value: 50 }]))).toEqual([0x02, 1, 50]);
    expect(Array.from(encodeTlv([{ id: TLV.co2Protocol, value: 0 }]))).toEqual([0x27, 1, 0]);
    expect(Array.from(encodeTlv([{ id: TLV.audioFastAlphaX1000, value: 250 }]))).toEqual([0x11, 2, 0xfa, 0x00]);
    expect(Array.from(encodeTlv([{ id: TLV.thresholdFixed, value: 100000 }]))).toEqual([0x15, 4, 0xa0, 0x86, 0x01, 0x00]);
    expect(Array.from(encodeTlv([{ id: TLV.bleName, value: 'Globo_1' }]))).toEqual([0x40, 7, ...new TextEncoder().encode('Globo_1')]);
  });
  it('roundtrip', () => {
    const out = decodeTlv(new DataView(encodeTlv([
      { id: TLV.orientationMode, value: 1 },
      { id: TLV.virtualHoldMs, value: 17000 },
      { id: TLV.thresholdFixed, value: 4000000000 },
      { id: TLV.bleName, value: 'Globo_9' },
    ]).buffer));
    expect(out.get(TLV.orientationMode)!.getUint8(0)).toBe(1);
    expect(out.get(TLV.virtualHoldMs)!.getUint16(0, true)).toBe(17000);
    expect(out.get(TLV.thresholdFixed)!.getUint32(0, true)).toBe(4000000000);
    expect(new TextDecoder().decode(out.get(TLV.bleName)!)).toBe('Globo_9');
  });
  it('rejects bad writes and tolerates truncated decode', () => {
    expect(() => encodeTlv([{ id: 0x99, value: 1 }])).toThrow();
    expect(() => encodeTlv([{ id: TLV.imuNotifyHz, value: 300 }])).toThrow();
    expect(() => encodeTlv([{ id: TLV.bleName, value: 'x'.repeat(17) }])).toThrow();
    expect(() => encodeTlv([{ id: TLV.pin, value: '12' }])).toThrow();
    expect(decodeTlv(new DataView(new Uint8Array([0x01, 1, 0, 0x02, 5, 1]).buffer)).size).toBe(1);
  });
  it('buildCommand', () => {
    expect(Array.from(buildCommand(0x10))).toEqual([0x10]);
    expect(Array.from(buildCommand(0x20, new TextEncoder().encode('1234')))).toEqual([0x20, 0x31, 0x32, 0x33, 0x34]);
  });
});

describe('VALID_OUTPUT_GPIOS', () => {
  it('excludes flash and nonexistent pins, includes valid ones', () => {
    for (const p of [6, 7, 8, 9, 10, 11, 20]) expect(VALID_OUTPUT_GPIOS).not.toContain(p);
    expect(VALID_OUTPUT_GPIOS).toContain(13);
    expect(VALID_OUTPUT_GPIOS).toContain(33);
    expect(VALID_OUTPUT_GPIOS).toHaveLength(22);
  });
});
