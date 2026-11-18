import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { attachAutoRead, type ConfigReadDetail } from './auto-read';

class FakeClient extends EventTarget {
  connected = true;
  reads = 0;
  /** One entry per attempt: true = succeeds, false = fails. Beyond the list, succeeds. */
  script: boolean[] = [];
  async readConfig(): Promise<Map<number, DataView>> {
    const ok = this.script[this.reads++] ?? true;
    if (!ok) throw new Error('GATT');
    return new Map();
  }
  connect(): void { this.connected = true; this.dispatchEvent(new CustomEvent('status', { detail: { connected: true, name: 'x' } })); }
  drop(): void { this.connected = false; this.dispatchEvent(new CustomEvent('status', { detail: { connected: false, name: 'x' } })); }
}

function watch(c: FakeClient) {
  const events: string[] = [];
  const reads: ConfigReadDetail[] = [];
  c.addEventListener('config', () => events.push('config'));
  c.addEventListener('config-read', (ev) => {
    const d = (ev as CustomEvent<ConfigReadDetail>).detail;
    reads.push(d);
    events.push(d.state);
  });
  return { events, reads };
}

describe('attachAutoRead', () => {
  beforeEach(() => { vi.useFakeTimers(); vi.spyOn(console, 'warn').mockImplementation(() => {}); });
  afterEach(() => { vi.useRealTimers(); vi.restoreAllMocks(); });

  it('reads right away on connect: reading, then config, then ok with the time', async () => {
    const c = new FakeClient();
    const { events, reads } = watch(c);
    const at = new Date(2026, 10, 17, 9, 8, 7);
    attachAutoRead(c, { now: () => at });
    c.connect();
    await vi.advanceTimersByTimeAsync(0);
    expect(events).toEqual(['reading', 'config', 'ok']);
    expect(reads[1].at).toBe(at);
    expect(c.reads).toBe(1);
  });

  it('retries after 300 ms and then 800 ms, then fails after the third attempt', async () => {
    const c = new FakeClient();
    c.script = [false, false, false];
    const { events } = watch(c);
    attachAutoRead(c);
    c.connect();
    await vi.advanceTimersByTimeAsync(0);
    expect(c.reads).toBe(1);
    await vi.advanceTimersByTimeAsync(299);
    expect(c.reads).toBe(1);
    await vi.advanceTimersByTimeAsync(1);
    expect(c.reads).toBe(2);
    await vi.advanceTimersByTimeAsync(799);
    expect(c.reads).toBe(2);
    expect(events).toEqual(['reading']);
    await vi.advanceTimersByTimeAsync(1);
    expect(c.reads).toBe(3);
    expect(events).toEqual(['reading', 'failed']);
    await vi.advanceTimersByTimeAsync(10_000);
    expect(c.reads).toBe(3); // no fourth attempt
  });

  it('succeeds on a later attempt without reporting a failure', async () => {
    const c = new FakeClient();
    c.script = [false, true];
    const { events } = watch(c);
    attachAutoRead(c);
    c.connect();
    await vi.advanceTimersByTimeAsync(300);
    expect(events).toEqual(['reading', 'config', 'ok']);
  });

  it('stops retrying once the link drops, and says nothing about it', async () => {
    const c = new FakeClient();
    c.script = [false, false, false];
    const { events } = watch(c);
    attachAutoRead(c);
    c.connect();
    await vi.advanceTimersByTimeAsync(0);
    c.drop();
    await vi.advanceTimersByTimeAsync(10_000);
    expect(c.reads).toBe(1);
    expect(events).toEqual(['reading']);
  });

  it('a new connection cancels the previous run', async () => {
    const c = new FakeClient();
    c.script = [false, true, true];
    const { events } = watch(c);
    attachAutoRead(c);
    c.connect();
    await vi.advanceTimersByTimeAsync(0);
    c.drop();
    c.connect(); // reading again at once
    await vi.advanceTimersByTimeAsync(10_000);
    expect(c.reads).toBe(2);
    expect(events).toEqual(['reading', 'reading', 'config', 'ok']);
  });

  it('"config-retry" re-runs the automatic read while connected', async () => {
    const c = new FakeClient();
    c.script = [false, false, false, true];
    const { events } = watch(c);
    attachAutoRead(c);
    c.connect();
    await vi.advanceTimersByTimeAsync(2000);
    expect(events).toEqual(['reading', 'failed']);
    c.dispatchEvent(new CustomEvent('config-retry'));
    await vi.advanceTimersByTimeAsync(0);
    expect(events).toEqual(['reading', 'failed', 'reading', 'config', 'ok']);
  });

  it('"config-retry" does nothing while disconnected', async () => {
    const c = new FakeClient();
    c.connected = false;
    const { events } = watch(c);
    attachAutoRead(c);
    c.dispatchEvent(new CustomEvent('config-retry'));
    await vi.advanceTimersByTimeAsync(0);
    expect(events).toEqual([]);
  });
});
