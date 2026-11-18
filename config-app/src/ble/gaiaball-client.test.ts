import { afterEach, describe, expect, it, vi } from 'vitest';
import { GaiaballClient } from './gaiaball-client';

const hex = (s: string) => new Uint8Array(s.trim().split(/\s+/).map((b) => parseInt(b, 16)));
const dv = (b: Uint8Array) => new DataView(b.buffer);
const IMU = '02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 0B F0 C8 F0 E4';
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

  it('a link drop resets the queue: an operation stuck on the dead link never blocks the next connection', async () => {
    const { c, log } = rig();
    const priv = c as unknown as Record<string, unknown>;
    const cfg = priv.cfgChar;
    void c.readConfig(); // never settles: the link died with it in flight
    await new Promise((r) => setTimeout(r, 0));
    expect(log).toEqual(['start read']);
    (priv.onDisconnected as () => void)();
    // The next connect() re-attaches the characteristics; its first operation must start at once.
    priv.cfgChar = cfg;
    priv.isConnected = true;
    void c.writeConfig([{ id: 0x01, value: 7 }]);
    await new Promise((r) => setTimeout(r, 0));
    expect(log).toEqual(['start read', 'start write']);
  });

  it('checks the connection when the operation runs, not when it is queued', async () => {
    const { c, release } = rig();
    const first = c.readConfig();
    const second = c.readConfig();
    const secondResult = expect(second).rejects.toThrow('Não conectado. Conecte a bola de novo e, para mudar algo, desbloqueie com o PIN outra vez: o desbloqueio vale só para a conexão atual.');
    await new Promise((r) => setTimeout(r, 0));
    (c as unknown as Record<string, unknown>).isConnected = false; // the link drops while the first is in flight
    await release();
    await first;
    await secondResult;
  });
});

describe('GaiaballClient.unlock', () => {
  function rig(readBytes: number[]) {
    const log: string[] = [];
    const c = new GaiaballClient();
    const priv = c as unknown as Record<string, unknown>;
    priv.cmdChar = { writeValue: async (v: Uint8Array) => { log.push(`write ${Array.from(v).join(',')}`); } };
    priv.cfgChar = { readValue: async () => { log.push('read'); return new DataView(new Uint8Array(readBytes).buffer); } };
    priv.isConnected = true;
    return { c, log };
  }

  it('sends 0x20 + the PIN, then reads the configuration back for the unlock state', async () => {
    const { c, log } = rig([0x02, 1, 50, 0x35, 1, 0x0d, 0x37, 1, 1]);
    await expect(c.unlock('1234')).resolves.toBe('unlocked');
    expect(log).toEqual(['write 32,49,50,51,52', 'read']);
  });

  it('reports a wrong PIN, the lockout, and firmware that cannot tell', async () => {
    await expect(rig([0x37, 1, 0]).c.unlock('9999')).resolves.toBe('locked');
    await expect(rig([0x37, 1, 2]).c.unlock('9999')).resolves.toBe('locked-out');
    await expect(rig([0x02, 1, 50, 0x35, 1, 0x0d]).c.unlock('1234')).resolves.toBe('unknown');
  });

  it('refuses a malformed PIN without touching the link', async () => {
    const { c, log } = rig([0x37, 1, 1]);
    await expect(c.unlock('12a4')).rejects.toThrow('O PIN deve ter 4 dígitos.');
    expect(log).toEqual([]);
  });
});

describe('GaiaballClient.connect retries a link that drops during setup (Windows keeps the old one for seconds)', () => {
  const netErr = (m = 'GATT Server is disconnected. Cannot retrieve services.') => new DOMException(m, 'NetworkError');
  /** A fake ball whose service discovery fails `failures` times with the given errors before working. */
  function fakeBall(failures: Error[]) {
    let connects = 0;
    const listeners: Record<string, number> = {};
    const ch = () => {
      const c = new EventTarget() as EventTarget & Record<string, unknown>;
      const add = c.addEventListener.bind(c);
      c.addEventListener = (t: string, f: EventListenerOrEventListenerObject | null) => { listeners[t] = (listeners[t] ?? 0) + 1; add(t, f); };
      c.startNotifications = async () => c;
      c.stopNotifications = async () => c;
      c.readValue = async () => new DataView(new ArrayBuffer(0));
      c.writeValue = async () => {};
      return c;
    };
    const chars = new Map<string, ReturnType<typeof ch>>();
    const service = { getCharacteristic: async (u: string) => { if (!chars.has(u)) chars.set(u, ch()); return chars.get(u)!; } };
    const gatt = {
      connected: false,
      connect: async () => { connects++; gatt.connected = true; return server; },
      disconnect: () => { gatt.connected = false; },
    };
    const server = {
      connect: gatt.connect,
      disconnect: gatt.disconnect,
      getPrimaryService: async () => {
        const e = failures.shift();
        // Like Chrome: the link drop is announced on the device, then the pending call fails.
        if (e) { gatt.connected = false; device.dispatchEvent(new Event('gattserverdisconnected')); throw e; }
        return service;
      },
    };
    const device = Object.assign(new EventTarget(), { name: 'Globo_1', gatt });
    Object.defineProperty(globalThis.navigator, 'bluetooth', {
      value: { requestDevice: async () => device }, configurable: true,
    });
    return { connects: () => connects, listeners, device };
  }
  afterEach(() => { vi.useRealTimers(); });

  it('connects on the second attempt after a NetworkError, reporting a single status', async () => {
    vi.useFakeTimers();
    const ball = fakeBall([netErr()]);
    const c = new GaiaballClient();
    const statuses: boolean[] = [];
    c.addEventListener('status', (e) => statuses.push((e as CustomEvent<{ connected: boolean }>).detail.connected));
    const p = c.connect();
    await vi.runAllTimersAsync();
    await p;
    expect(ball.connects()).toBe(2);
    expect(c.connected).toBe(true);
    expect(statuses).toEqual([true]);
    expect(ball.listeners.characteristicvaluechanged).toBe(4); // imu, co2, diag, nav: once each, not per attempt
  });

  it('gives up after three link drops and leaves the client disconnected', async () => {
    vi.useFakeTimers();
    const ball = fakeBall([netErr(), netErr(), netErr()]);
    const c = new GaiaballClient();
    const p = c.connect();
    const failed = expect(p).rejects.toThrow('GATT Server is disconnected');
    await vi.runAllTimersAsync();
    await failed;
    expect(ball.connects()).toBe(3);
    expect(c.connected).toBe(false);
  });

  it('does not retry an error that is not a link drop', async () => {
    const ball = fakeBall([new DOMException('No Services matching UUID found in Device.', 'NotFoundError')]);
    const c = new GaiaballClient();
    await expect(c.connect()).rejects.toThrow('No Services matching');
    expect(ball.connects()).toBe(1);
  });

  it('a listener left by an earlier connection does not fire during the next setup (same device object)', async () => {
    vi.useFakeTimers();
    const failures: Error[] = [];
    const ball = fakeBall(failures);
    const c = new GaiaballClient();
    await c.connect();
    ball.device.gatt.connected = false;
    ball.device.dispatchEvent(new Event('gattserverdisconnected')); // the ball rebooted: unexpected drop
    const statuses: boolean[] = [];
    c.addEventListener('status', (e) => statuses.push((e as CustomEvent<{ connected: boolean }>).detail.connected));
    failures.push(netErr()); // the next Conectar lands on a dying link once
    const p = c.connect();
    await vi.runAllTimersAsync();
    await p;
    expect(statuses).toEqual([true]);
    expect(c.connected).toBe(true);
  });

  it('a link that drops after setup is still reported as a disconnect', async () => {
    const ball = fakeBall([]);
    const c = new GaiaballClient();
    await c.connect();
    expect(c.connected).toBe(true);
    ball.device.gatt.connected = false;
    ball.device.dispatchEvent(new Event('gattserverdisconnected'));
    expect(c.connected).toBe(false);
  });
});

describe('GaiaballClient retries a transient GATT failure (Windows, ~0.6% of operations while notifications flow)', () => {
  const transient = () => new DOMException('GATT operation failed for unknown reason.', 'NotSupportedError');
  function rig(failures: Error[], linkUp = true) {
    let calls = 0;
    const c = new GaiaballClient();
    const priv = c as unknown as Record<string, unknown>;
    priv.cmdChar = { writeValue: async () => { calls++; const e = failures.shift(); if (e) throw e; } };
    priv.isConnected = true;
    priv.device = { gatt: { connected: linkUp } };
    return { c, calls: () => calls };
  }
  afterEach(() => { vi.useRealTimers(); });

  it('repeats the operation once it failed transiently, and the caller never sees the failure', async () => {
    vi.useFakeTimers();
    const r = rig([transient()]);
    const p = r.c.sendCommand(0x11);
    await vi.runAllTimersAsync();
    await expect(p).resolves.toBeUndefined();
    expect(r.calls()).toBe(2);
  });

  it('gives up after three attempts', async () => {
    vi.useFakeTimers();
    const r = rig([transient(), transient(), transient()]);
    const p = r.c.sendCommand(0x11);
    const failed = expect(p).rejects.toThrow('GATT operation failed for unknown reason.');
    await vi.runAllTimersAsync();
    await failed;
    expect(r.calls()).toBe(3);
  });

  it('does not repeat other failures, nor anything once the link is down', async () => {
    const other = rig([new DOMException('GATT operation not permitted.', 'NotSupportedError')]);
    await expect(other.c.sendCommand(0x11)).rejects.toThrow('not permitted');
    expect(other.calls()).toBe(1);
    const down = rig([transient()], false);
    await expect(down.c.sendCommand(0x11)).rejects.toThrow('unknown reason');
    expect(down.calls()).toBe(1);
  });
});
