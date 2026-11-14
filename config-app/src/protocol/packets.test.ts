import { describe, expect, it } from 'vitest';
import { decodeAudioChunkPacket, decodeCo2Packet, decodeDiagPacket, decodeImuPacket, decodeMagChunkPacket, decodeNav, decodeSensorsPacket, rmsToDbfs } from './packets';
import { TLV, VALID_OUTPUT_GPIOS, buildCommand, decodeNavRef, decodeTlv, encodeNavRef, encodeTlv, navRefToString, parseNavRefString } from './tlv';

const hex = (s: string) => new DataView(new Uint8Array(s.trim().split(/\s+/).map((b) => parseInt(b, 16))).buffer);

// Golden vectors copied verbatim from firmware/docs/protocol.md
const IMU =
  '01 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 01 00 FE FF 03 00 0B' +
  ' 00 00 30 F8 00 00 A6 FD';
const CO2 = '01 02 02 01 0D 0C 0B 0A E8 03 05 07';
const DIAG =
  '01 03 01 00 03 02 01 00 F4 01 F4 01 01 00 02 00 03 00 C8 00 96 00 07 11 03 0C 96 00 B5 FF 00 00' +
  ' 96 00';
const MAG = '01 06 09 00 04 03 02 01 02 00 64 00 38 FF 2C 01 65 00 37 FF 2D 01' + ' 00'.repeat(84);
const SENSORS = '01 04 07 00 40 E2 01 00 39 30 00 00 A0 5B 00 00 A0 86 01 00 00 08 07';

describe('golden decode', () => {
  it('IMU', () => {
    const s = decodeImuPacket(hex(IMU))!;
    expect(s.seq).toBe(258);
    expect(s.tUs).toBe(0x0a0b0c0d);
    expect(s.quat).toEqual({ w: 1.0, x: 0, y: 0, z: 0 });
    expect(s.acc).toEqual([1, -2, 3]);
    expect(s.flags).toBe(0b1011);
    expect([s.quatValid, s.nineAxis, s.moving]).toEqual([true, true, false]);
    // Microphone envelope appended in v8: AES17 dBFS for the RMS, amplitude dBFS for
    // the peak, both x100 on the wire.
    expect(s.audioValid).toBe(true);
    expect(s.audioRmsDbfs).toEqual([0, -20]);
    expect(s.audioPeakDbfs[0]).toBe(0);
    expect(s.audioPeakDbfs[1]).toBeCloseTo(-6.02, 2);
  });

  it('IMU without a microphone reads the dB floor, not 0 dBFS', () => {
    // Same packet with kImuAudioValid clear and both levels at the floor (-120.00).
    const silent =
      '01 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 01 00 FE FF 03 00 01' +
      ' 20 D1 20 D1 20 D1 20 D1';
    const s = decodeImuPacket(hex(silent))!;
    expect(s.audioValid).toBe(false);
    expect(s.audioRmsDbfs).toEqual([-120, -120]);
    expect(s.audioPeakDbfs).toEqual([-120, -120]);
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
      ahrsFlags: 0x11, accelErrDeg: 3, magErrDeg: 12,
      // Granted connection interval (v8): the ceiling on every notify rate.
      connItvlMs: 15,
    });
    expect([s.imuOk, s.audioOk, s.co2Ok]).toEqual([true, true, true]);
    expect(s.gyroBiasDps[0]).toBeCloseTo(1.5, 6);
    expect(s.gyroBiasDps[1]).toBeCloseTo(-0.75, 6);
    expect(s.gyroBiasDps[2]).toBe(0);
    expect([s.startup, s.biasAtRest]).toEqual([true, true]);
    expect([s.accelIgnored, s.magIgnored, s.magCapturing]).toEqual([false, false, false]);
  });

  it('MagChunk', () => {
    const s = decodeMagChunkPacket(hex(MAG))!;
    expect(s.seq).toBe(9);
    expect(s.tMs).toBe(0x01020304);
    expect(s.samples).toHaveLength(2);
    expect(s.samples[0][0]).toBeCloseTo(10, 6);
    expect(s.samples[0][1]).toBeCloseTo(-20, 6);
    expect(s.samples[0][2]).toBeCloseTo(30, 6);
    // Cross-type safety on the shared diag characteristic.
    expect(decodeMagChunkPacket(hex(DIAG))).toBeNull();
    expect(decodeDiagPacket(hex(MAG))).toBeNull();
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

  it('AudioChunk', () => {
    // 208-byte packet: header + hp/lp echo + 96 int16 samples (first = -2, second = 513).
    const head = '01 05 2A 00 10 27 00 00 D0 07 60 01 0A 00 C8 00';
    const body = 'FE FF 01 02 ' + '00 00 '.repeat(94);
    const dv = hex(head + ' ' + body);
    expect(dv.byteLength).toBe(208);
    const s = decodeAudioChunkPacket(dv)!;
    expect(s).toMatchObject({ seq: 42, tMs: 10000, sampleRateHz: 2000, hpHz: 10, lpHz: 200 });
    expect(s.bpfEnabled).toBe(true);
    expect(s.samples.length).toBe(96);
    expect(s.samples[0]).toBe(-2);
    expect(s.samples[1]).toBe(513);
    expect(s.samples[95]).toBe(0);
    // Wrong type on the same characteristic -> null.
    expect(decodeAudioChunkPacket(hex(SENSORS))).toBeNull();
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
    expect(Object.keys(TLV)).toHaveLength(60);
    expect(TLV.orientationMode).toBe(0x01);
    expect(TLV.imuNotifyHz).toBe(0x02);
    expect(TLV.slerpAmount).toBe(0x03);
    expect(TLV.thresholdFixed).toBe(0x15);
    expect(TLV.co2AlertPpm).toBe(0x32);
    expect(TLV.bleName).toBe(0x40);
    expect(TLV.pin).toBe(0x41);
    expect(TLV.imuChip).toBe(0x34);
    expect(TLV.imuDetected).toBe(0x35);
    expect(TLV.magAxisRemap).toBe(0x36);
  });
  it('imuDetected is read-only: it cannot be encoded for a write', () => {
    expect(() => encodeTlv([{ id: TLV.imuDetected, value: 1 }])).toThrow(RangeError);
  });
  it('encode layout little-endian', () => {
    expect(Array.from(encodeTlv([{ id: TLV.imuNotifyHz, value: 50 }]))).toEqual([0x02, 1, 50]);
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

// Golden vectors A and B copied verbatim from the navigation protocol spec.
const NAV_A = '01 07 02 01 4E 61 BC 00 01 03 B8 E3 58 F2 36 16 F3 E3 94 11 7B 00';
const NAV_B = '01 07 FF FF FF FF FF FF 02 06 00 E9 A4 35 00 2E B6 94 B0 B9 FF FF';

describe('NavPacket', () => {
  it('golden A', () => {
    const s = decodeNav(hex(NAV_A))!;
    expect(s.seq).toBe(258);
    expect(s.tUs).toBe(12345678);
    expect(s.mode).toBe(1);
    expect(s.flags).toBe(0x03);
    expect([s.valid, s.settled, s.moving]).toEqual([true, true, false]);
    expect(s.lat).toBeCloseTo(-22.905556, 7);
    expect(s.lon).toBeCloseTo(-47.060833, 7);
    expect(s.bearingDeg).toBe(45);
    expect(s.speedDegS).toBeCloseTo(12.3, 6);
  });
  it('golden B (extremes)', () => {
    const s = decodeNav(hex(NAV_B))!;
    expect(s.seq).toBe(65535);
    expect(s.tUs).toBe(4294967295);
    expect(s.mode).toBe(2);
    expect([s.valid, s.settled, s.moving]).toEqual([false, true, true]);
    expect(s.lat).toBe(90);
    expect(s.lon).toBe(-180);
    expect(s.bearingDeg).toBe(-180);
    expect(s.speedDegS).toBeCloseTo(6553.5, 6);
  });
  it('rejects wrong length, version and type', () => {
    const b = new Uint8Array(hex(NAV_A).buffer);
    expect(decodeNav(new DataView(b.buffer, 0, 21))).toBeNull();
    expect(decodeNav(new DataView(new Uint8Array(23).buffer))).toBeNull();
    expect(decodeNav(new DataView(new ArrayBuffer(0)))).toBeNull();
    const v = new Uint8Array(b); v[0] = 2;
    expect(decodeNav(new DataView(v.buffer))).toBeNull();
    const t = new Uint8Array(b); t[1] = 0x06;
    expect(decodeNav(new DataView(t.buffer))).toBeNull();
    expect(decodeNav(hex(CO2))).toBeNull();
    expect(decodeCo2Packet(hex(NAV_A))).toBeNull();
  });
});

describe('Co2Packet trigger flag', () => {
  it('exposes bit3', () => {
    const b = new Uint8Array(hex(CO2).buffer);
    expect(decodeCo2Packet(new DataView(b.buffer))!.trigger).toBe(false); // flags 0x05
    b[10] = 0x0d;
    const s = decodeCo2Packet(new DataView(b.buffer))!;
    expect(s.trigger).toBe(true);
    expect([s.valid, s.stale, s.virtualActive]).toEqual([true, false, true]);
  });
});

describe('navigation TLV registers', () => {
  const cases: [string, number, number, number, number][] = [
    // name, id, wire bytes, valid value, ...
    ['co2TriggerPpm', 0x29, 2, 1200, 400],
    ['co2TriggerHystPpm', 0x2a, 2, 50, 2000],
    ['navMode', 0x60, 1, 1, 2],
    ['navNotifyHz', 0x61, 1, 50, 200],
    ['joyMaxSpeedDegS', 0x63, 2, 90, 720],
    ['joyFullAngleDeg', 0x64, 1, 60, 90],
    ['joyCurveX10', 0x65, 1, 30, 40],
    ['navStartDegS', 0x66, 1, 20, 200],
    ['navStopDegS', 0x67, 1, 5, 100],
    ['navStopMs', 0x68, 2, 420, 5000],
    ['joyStartDegSX10', 0x69, 2, 10, 2000],
    ['joyStopDegSX10', 0x6a, 2, 5, 2000],
  ];
  it.each(cases)('%s round-trips', (name, id, bytes, v1, v2) => {
    expect((TLV as Record<string, number>)[name]).toBe(id);
    for (const v of [v1, v2]) {
      const enc = encodeTlv([{ id, value: v }]);
      expect(enc.length).toBe(2 + bytes);
      expect(enc[0]).toBe(id);
      expect(enc[1]).toBe(bytes);
      const dv = decodeTlv(new DataView(enc.buffer)).get(id)!;
      expect(bytes === 1 ? dv.getUint8(0) : dv.getUint16(0, true)).toBe(v);
    }
  });
  it('navRef is 8 bytes: i32 latE6 + i32 lonE6, little-endian', () => {
    expect(TLV.navRef).toBe(0x62);
    const rec = encodeNavRef({ lat: -22.905556, lon: -47.060833 });
    const dv = new DataView(rec.buffer);
    expect(dv.getInt32(0, true)).toBe(-22905556);
    expect(dv.getInt32(4, true)).toBe(-47060833);
    const enc = encodeTlv([{ id: TLV.navRef, value: rec }]);
    expect(enc.length).toBe(10);
    expect(enc[1]).toBe(8);
    const back = decodeNavRef(decodeTlv(new DataView(enc.buffer)).get(TLV.navRef)!)!;
    expect(back).toEqual({ lat: -22.905556, lon: -47.060833 });
    expect(() => encodeTlv([{ id: TLV.navRef, value: new Uint8Array(7) }])).toThrow(RangeError);
    expect(decodeNavRef(new DataView(new ArrayBuffer(7)))).toBeNull();
    expect(() => encodeNavRef({ lat: 90.000001, lon: 0 })).toThrow(RangeError);
    expect(() => encodeNavRef({ lat: 0, lon: -180.000001 })).toThrow(RangeError);
    expect(decodeNavRef(new DataView(encodeNavRef({ lat: 90, lon: -180 }).buffer))).toEqual({ lat: 90, lon: -180 });
  });
  it('navRef canonical string round-trips', () => {
    const s = navRefToString({ lat: -22.905556, lon: -47.060833 });
    expect(s).toBe('-22.905556,-47.060833');
    expect(parseNavRefString(s)).toEqual({ lat: -22.905556, lon: -47.060833 });
    expect(navRefToString({ lat: -0.0000001, lon: 0 })).toBe('0.000000,0.000000');
    expect(parseNavRefString('abc')).toBeNull();
    expect(parseNavRefString('1,')).toBeNull();
  });
});
