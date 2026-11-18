import { describe, expect, it } from 'vitest';
import { encodeCo2Packet, encodeImuPacket, encodeNavPacket, encodeSensorsPacket } from './encode';
import { decodeCo2Packet, decodeImuPacket, decodeNav, decodeSensorsPacket } from './packets';

const bytes = (s: string) => new Uint8Array(s.trim().split(/\s+/).map((b) => parseInt(b, 16)));
const dv = (b: Uint8Array) => new DataView(b.buffer, b.byteOffset, b.byteLength);

const IMU = '02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 0B F0 C8 F0 E4';
const CO2 = '01 02 02 01 0D 0C 0B 0A E8 03 05 07';
const NAV_A = '01 07 02 01 4E 61 BC 00 01 03 B8 E3 58 F2 36 16 F3 E3 94 11 7B 00 07';
const NAV_B = '01 07 FF FF FF FF FF FF 02 06 00 E9 A4 35 00 2E B6 94 B0 B9 FF FF FF';
const SENSORS = '01 04 07 00 40 E2 01 00 39 30 00 00 A0 5B 00 00 A0 86 01 00 00 08 07';

describe('encode', () => {
  it('IMU v2 reproduces the golden bytes from its fields', () => {
    const out = encodeImuPacket({
      seq: 258, tUs: 0x0a0b0c0d, quat: { w: 1, x: 0, y: 0, z: 0 }, acc: [1000, -500, 151],
      quatValid: true, nineAxis: true, moving: false, audioValid: true, breath: false,
      audioRmsDbfs: [0, -20], audioPeakDbfs: [0, -6],
    });
    expect(Array.from(out)).toEqual(Array.from(bytes(IMU)));
  });

  it('round trips every golden packet through decode and encode', () => {
    expect(Array.from(encodeImuPacket(decodeImuPacket(dv(bytes(IMU)))!))).toEqual(Array.from(bytes(IMU)));
    expect(Array.from(encodeCo2Packet(decodeCo2Packet(dv(bytes(CO2)))!))).toEqual(Array.from(bytes(CO2)));
    expect(Array.from(encodeNavPacket(decodeNav(dv(bytes(NAV_A)))!))).toEqual(Array.from(bytes(NAV_A)));
    expect(Array.from(encodeNavPacket(decodeNav(dv(bytes(NAV_B)))!))).toEqual(Array.from(bytes(NAV_B)));
    expect(Array.from(encodeSensorsPacket(decodeSensorsPacket(dv(bytes(SENSORS)))!))).toEqual(Array.from(bytes(SENSORS)));
  });

  it('decode(encode(x)) returns x', () => {
    const imu = decodeImuPacket(dv(bytes(IMU.replace('0B F0', '1D F0'))))!; // moving + breath, no 9-axis
    expect(decodeImuPacket(dv(encodeImuPacket(imu)))).toEqual(imu);
    expect(imu.breath).toBe(true);
    const co2 = decodeCo2Packet(dv(bytes(CO2)))!;
    expect(decodeCo2Packet(dv(encodeCo2Packet(co2)))).toEqual(co2);
    const nav = decodeNav(dv(bytes(NAV_B)))!;
    expect(decodeNav(dv(encodeNavPacket(nav)))).toEqual(nav);
    const sens = decodeSensorsPacket(dv(bytes(SENSORS.replace(/07$/, '37'))))!;
    expect(sens.metricPeak && sens.adaptive).toBe(true);
    expect(decodeSensorsPacket(dv(encodeSensorsPacket(sens)))).toEqual(sens);
  });

  it('clamps audio to the u8 half-dB range', () => {
    const o = encodeImuPacket({
      ...decodeImuPacket(dv(bytes(IMU)))!, audioRmsDbfs: [-200, 50], audioPeakDbfs: [-120, 7.5],
    });
    expect(Array.from(o.slice(23, 27))).toEqual([0, 255, 0, 255]);
  });
});
