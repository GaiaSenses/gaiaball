import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { PD_MAX_BUFFERED, PD_RETRY_MS, PdBridge, type PdBridgeDeps, type PdStatus, type WsLike } from './pd-bridge';

class FakeSocket implements WsLike {
  readyState = 0;
  bufferedAmount = 0;
  binaryType = 'blob';
  sent: Uint8Array[] = [];
  onopen: (() => void) | null = null;
  onclose: (() => void) | null = null;
  onerror: (() => void) | null = null;
  readonly url: string;
  constructor(url: string) { this.url = url; }
  send(d: Uint8Array): void { this.sent.push(d); }
  close(): void { this.readyState = 3; this.onclose?.(); }
  open(): void { this.readyState = 1; this.onopen?.(); }
  drop(): void { this.readyState = 3; this.onclose?.(); }
}

let sockets: FakeSocket[];
let perm: 'granted' | 'denied' | 'prompt' | 'unknown';
let bridge: PdBridge;
const flush = async (): Promise<void> => { for (let i = 0; i < 5; i++) await Promise.resolve(); };

beforeEach(() => {
  vi.useFakeTimers();
  sockets = [];
  perm = 'unknown';
  const deps: PdBridgeDeps = {
    createSocket: (url) => { const s = new FakeSocket(url); sockets.push(s); return s; },
    setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout,
    now: () => Date.now(),
    queryLoopbackPermission: async () => perm,
  };
  bridge = new PdBridge(deps);
});
afterEach(() => { bridge.setEnabled(false); vi.useRealTimers(); });

const pkt = (type: number) => new Uint8Array([1, type, 0, 0]);
const last = () => sockets[sockets.length - 1];

describe('PdBridge', () => {
  it('forwards types 0x01, 0x02 and 0x07 only', () => {
    bridge.setEnabled(true);
    last().open();
    for (const t of [1, 2, 3, 4, 5, 6, 7]) bridge.forward(pkt(t));
    bridge.forward(new Uint8Array(0));
    expect(last().sent.map((b) => b[1])).toEqual([1, 2, 7]);
    expect(last().url).toBe('ws://127.0.0.1:9478');
    expect(last().binaryType).toBe('arraybuffer');
  });

  it('drops and counts when the socket is backed up', () => {
    bridge.setEnabled(true);
    last().open();
    last().bufferedAmount = PD_MAX_BUFFERED + 904; // 5000
    bridge.forward(pkt(1));
    expect(last().sent).toHaveLength(0);
    expect(bridge.status.dropped).toBe(1);
  });

  it('drops (counted, no throw) while not connected', () => {
    bridge.setEnabled(true);
    expect(() => bridge.forward(pkt(1))).not.toThrow();
    expect(bridge.status.dropped).toBe(1);
  });

  it('reconnects after PD_RETRY_MS when Pd closes, then forwards again', () => {
    bridge.setEnabled(true);
    last().open();
    last().drop();
    expect(bridge.status.state).toBe('waiting');
    expect(() => bridge.forward(pkt(1))).not.toThrow();
    expect(sockets).toHaveLength(1);
    vi.advanceTimersByTime(PD_RETRY_MS - 1);
    expect(sockets).toHaveLength(1);
    vi.advanceTimersByTime(1);
    expect(sockets).toHaveLength(2);
    last().open();
    bridge.forward(pkt(2));
    expect(last().sent).toHaveLength(1);
  });

  it('goes waiting -> connected -> waiting', () => {
    const seen: string[] = [];
    bridge.addEventListener('status', (e) => seen.push((e as CustomEvent<PdStatus>).detail.state));
    expect(bridge.status.state).toBe('off');
    bridge.setEnabled(true);
    expect(bridge.status.state).toBe('waiting');
    last().open();
    expect(bridge.status.state).toBe('connected');
    last().drop();
    expect(bridge.status.state).toBe('waiting');
    expect(seen).toEqual(['waiting', 'connected', 'waiting']);
  });

  it('reports the packets per second while connected', () => {
    bridge.setEnabled(true);
    last().open();
    for (let i = 0; i < 40; i++) bridge.forward(pkt(1));
    vi.advanceTimersByTime(1000);
    expect(bridge.status.rate).toBe(40);
  });

  it('is blocked when the loopback permission is denied', async () => {
    perm = 'denied';
    bridge.setEnabled(true);
    await flush();
    last().drop();
    await flush();
    expect(bridge.status.state).toBe('blocked');
    vi.advanceTimersByTime(PD_RETRY_MS);
    last().open();
    expect(bridge.status.state).toBe('connected');
  });

  it('setPort reconnects to the new port', () => {
    bridge.setEnabled(true);
    last().open();
    bridge.setPort(9500);
    expect(last().url).toBe('ws://127.0.0.1:9500');
    expect(sockets[0].readyState).toBe(3);
    expect(bridge.status.state).toBe('waiting');
  });

  it('creates no socket while disabled and closes it when turned off', () => {
    bridge.setPort(9501);
    bridge.forward(pkt(1));
    expect(sockets).toHaveLength(0);
    expect(bridge.status.state).toBe('off');
    bridge.setEnabled(true);
    last().open();
    bridge.setEnabled(false);
    expect(bridge.status.state).toBe('off');
    vi.advanceTimersByTime(10_000);
    expect(sockets).toHaveLength(1);
  });

  it('forward needs no document', () => {
    expect((globalThis as { document?: unknown }).document).toBeUndefined();
    bridge.setEnabled(true);
    last().open();
    expect(() => bridge.forward(pkt(1))).not.toThrow();
    expect(last().sent).toHaveLength(1);
  });

  it('forward never throws even if send does', () => {
    bridge.setEnabled(true);
    last().open();
    last().send = () => { throw new Error('boom'); };
    expect(() => bridge.forward(pkt(1))).not.toThrow();
    expect(bridge.status.dropped).toBe(1);
  });

  it('reports whether the user wants Pd, independent of the BLE link', () => {
    const seen: boolean[] = [];
    bridge.addEventListener('status', (e) => seen.push((e as CustomEvent<PdStatus>).detail.wanted));
    expect(bridge.status.wanted).toBe(false);
    bridge.setWanted(true);
    expect(bridge.status).toMatchObject({ state: 'off', wanted: true });
    bridge.setWanted(true);
    bridge.setWanted(false);
    expect(seen).toEqual([true, false]);
  });
});
