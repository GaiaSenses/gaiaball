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

describe('GaiaballClient GATT queue', () => {
  /** A connected client whose config/diag characteristics log when each operation starts and ends. */
  function rig(opts: { failRead?: boolean } = {}) {
    const log: string[] = [];
    const pending: (() => void)[] = [];
    const slow = <T>(name: string, value: T, fail = false) => () => {
      log.push(`start ${name}`);
      return new Promise<T>((resolve, reject) => {
        pending.push(() => { log.push(`end ${name}`); if (fail) reject(new Error(name)); else resolve(value); });
      });
    };
    const cfg = {
      readValue: slow('read', new DataView(new Uint8Array([0x01, 0x01, 0x07]).buffer), opts.failRead),
      writeValue: slow('write', undefined),
    };
    const diag = { stopNotifications: slow('stop', undefined), startNotifications: slow('start', undefined) };
    const c = new GaiaballClient();
    const priv = c as unknown as Record<string, unknown>;
    priv.cfgChar = cfg;
    priv.diagChar = diag;
    priv.isConnected = true;
    const release = async (): Promise<void> => { pending.shift()?.(); await new Promise((r) => setTimeout(r, 0)); };
    return { c, log, release, pending };
  }

  it('runs operations strictly one after another, in call order', async () => {
    const { c, log, release } = rig();
    const ops = [c.pauseDiag(), c.readConfig(), c.writeConfig([{ id: 0x01, value: 7 }]), c.resumeDiag()];
    await new Promise((r) => setTimeout(r, 0));
    expect(log).toEqual(['start stop']); // nothing else starts while the first is in flight
    await release();
    expect(log).toEqual(['start stop', 'end stop', 'start read']);
    await release();
    await release();
    await release();
    await Promise.all(ops);
    expect(log).toEqual([
      'start stop', 'end stop', 'start read', 'end read', 'start write', 'end write', 'start start', 'end start',
    ]);
  });

  it('a rejected operation rejects its own caller and does not block the next', async () => {
    const { c, log, release } = rig({ failRead: true });
    const read = c.readConfig();
    const write = c.writeConfig([{ id: 0x01, value: 7 }]);
    const readResult = expect(read).rejects.toThrow('read');
    await new Promise((r) => setTimeout(r, 0));
    await release(); // the read fails
    await readResult;
    await new Promise((r) => setTimeout(r, 0));
    expect(log).toContain('start write');
    await release();
    await expect(write).resolves.toBeUndefined();
  });

  it('checks the connection when the operation runs, not when it is queued', async () => {
    const { c, release } = rig();
    const first = c.readConfig();
    const second = c.readConfig();
    const secondResult = expect(second).rejects.toThrow('Não conectado.');
    await new Promise((r) => setTimeout(r, 0));
    (c as unknown as Record<string, unknown>).isConnected = false; // the link drops while the first is in flight
    await release();
    await first;
    await secondResult;
  });
});
