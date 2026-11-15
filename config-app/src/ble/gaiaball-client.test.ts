import { describe, expect, it } from 'vitest';
import { GaiaballClient } from './gaiaball-client';

const hex = (s: string) => new Uint8Array(s.trim().split(/\s+/).map((b) => parseInt(b, 16)));
const dv = (b: Uint8Array) => new DataView(b.buffer);
const IMU = '02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 0B F0 C8 F0 E4';
const IMU_V1 = '01 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 01 00 FE FF 03 00 0B 00 00 30 F8 00 00 A6 FD';
const DIAG = '01 03 01 00 03 02 01 00 F4 01 F4 01 01 00 02 00 03 00 C8 00 96 00 07 11 03 0C 96 00 B5 FF 00 00 96 00';
const SENSORS = '01 04 07 00 40 E2 01 00 39 30 00 00 A0 5B 00 00 A0 86 01 00 00 08 07';

describe('GaiaballClient.ingest', () => {
  it('hands the raw bytes to the sink before the decoded event', () => {
    const c = new GaiaballClient();
    const order: string[] = [];
    let raw: Uint8Array | null = null;
    c.setRawSink((b) => { order.push('raw'); raw = b; });
    c.addEventListener('imu', () => order.push('imu'));
    c.ingest('imu', dv(hex(IMU)));
    expect(order).toEqual(['raw', 'imu']);
    expect(Array.from(raw!)).toEqual(Array.from(hex(IMU)));
  });

  it('does not send diagnostic packets to the sink but still decodes them', () => {
    const c = new GaiaballClient();
    let calls = 0;
    const seen: string[] = [];
    c.setRawSink(() => { calls++; });
    c.addEventListener('diag', () => seen.push('diag'));
    c.addEventListener('sensors', () => seen.push('sensors'));
    c.ingest('diag', dv(hex(DIAG)));
    c.ingest('diag', dv(hex(SENSORS)));
    expect(calls).toBe(0);
    expect(seen).toEqual(['diag', 'sensors']);
  });

  it('still reaches the sink when a decoder rejects, and survives a throwing sink', () => {
    const c = new GaiaballClient();
    const raw: number[] = [];
    c.setRawSink((b) => { raw.push(b.length); throw new Error('x'); });
    let imu = 0;
    c.addEventListener('imu', () => imu++);
    expect(() => c.ingest('imu', dv(hex(IMU)))).not.toThrow();
    expect(imu).toBe(1);
    c.setRawSink(null);
    expect(() => c.ingest('imu', dv(hex(IMU)))).not.toThrow();
    expect(raw).toEqual([27]);
  });

  it('warns once about legacy v1 firmware', () => {
    const c = new GaiaballClient();
    let n = 0;
    c.addEventListener('firmware-outdated', () => n++);
    c.ingest('imu', dv(hex(IMU_V1)));
    c.ingest('imu', dv(hex(IMU_V1)));
    expect(n).toBe(1);
  });
});
