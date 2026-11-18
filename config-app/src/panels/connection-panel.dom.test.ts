// @vitest-environment happy-dom
// The Conexão panel: the PIN result the ball confirms (TLV 0x37), the lock badge, and GATT errors in Portuguese.
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { mountConnectionPanel } from './connection-panel';
import type { GaiaballClient } from '../ble/gaiaball-client';
import type { LockState } from '../ble/unlock-state';
import { TLV } from '../protocol/tlv';

class FakeClient extends EventTarget {
  connected = false;
  deviceName = 'Globo_1';
  pins: string[] = [];
  result: LockState | Error = 'unlocked';
  async unlock(pin: string): Promise<LockState> {
    this.pins.push(pin);
    if (this.result instanceof Error) throw this.result;
    return this.result;
  }
  async sendCommand(): Promise<void> {}
  status(connected: boolean): void {
    this.connected = connected;
    this.dispatchEvent(new CustomEvent('status', { detail: { connected, name: this.deviceName } }));
  }
  config(lock: number | null): void {
    const regs = new Map<number, DataView>([[TLV.imuNotifyHz, new DataView(new Uint8Array([50]).buffer)]]);
    if (lock !== null) regs.set(TLV.unlockState, new DataView(new Uint8Array([lock]).buffer));
    this.dispatchEvent(new CustomEvent('config', { detail: regs }));
  }
}

let root: HTMLElement;
let client: FakeClient;
const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
const msg = (): string => q('[data-el=msg]').textContent!;
const badge = (): HTMLElement => q('[data-el=lock]');
const settle = (): Promise<void> => new Promise((r) => setTimeout(r, 0));
const unlockWith = async (pin: string): Promise<void> => {
  q<HTMLInputElement>('[data-el=pin]').value = pin;
  q('[data-act=unlock]').click();
  await settle();
};

beforeEach(() => {
  document.body.innerHTML = '<section id="c"></section>';
  root = document.getElementById('c')!;
  client = new FakeClient();
  mountConnectionPanel(root, client as unknown as GaiaballClient);
  client.status(true);
});

describe('Desbloquear says what the ball answered', () => {
  it('confirms the unlock and shows the badge', async () => {
    await unlockWith('1234');
    expect(client.pins).toEqual(['1234']);
    expect(msg()).toMatch(/^Desbloqueada/);
    expect(badge().textContent).toBe('Desbloqueada');
    expect(badge().hidden).toBe(false);
    expect(badge().dataset.state).toBe('unlocked');
  });

  it('says the PIN is wrong, and when the ball stops accepting PINs', async () => {
    client.result = 'locked';
    await unlockWith('9999');
    expect(msg()).toMatch(/PIN incorreto/);
    expect(badge().textContent).toBe('Bloqueada');
    client.result = 'locked-out';
    await unlockWith('9999');
    expect(msg()).toMatch(/30 s/);
    expect(badge().textContent).toBe('Bloqueada por 30 s');
  });


  it('explains a GATT failure in Portuguese instead of the bare Chrome text', async () => {
    client.result = new DOMException('GATT operation failed for unknown reason.', 'NetworkError');
    await unlockWith('1234');
    expect(msg()).toMatch(/^A bola não respondeu pelo Bluetooth/);
    expect(msg()).toMatch(/desbloqueie outra vez/);
  });
});

describe('Conectar', () => {
  it('clears the previous message, so an old "Desbloqueada" never shows while connecting', async () => {
    await unlockWith('1234');
    client.status(false);
    (client as unknown as { connect: () => Promise<void> }).connect = () => new Promise(() => {});
    q('[data-act=toggle]').click();
    expect(msg()).toBe('');
    expect(q('[data-el=status]').textContent).toBe('Conectando…');
  });
});

describe('lock badge follows every configuration read and the link', () => {
  it('shows Bloqueada after the automatic read on connect, and clears on disconnect', () => {
    expect(badge().hidden).toBe(true);
    client.config(0);
    expect(badge().textContent).toBe('Bloqueada');
    expect(badge().hidden).toBe(false);
    client.config(1);
    expect(badge().textContent).toBe('Desbloqueada');
    client.status(false);
    expect(badge().hidden).toBe(true);
  });

  it('stays hidden when the read has no 0x37 (older firmware)', () => {
    client.config(null);
    expect(badge().hidden).toBe(true);
  });
});

describe('lockout badge', () => {
  afterEach(() => { vi.useRealTimers(); });
  it('goes back to Bloqueada once the 30 s window is over', async () => {
    vi.useFakeTimers();
    client.result = 'locked-out';
    q<HTMLInputElement>('[data-el=pin]').value = '9999';
    q('[data-act=unlock]').click();
    await vi.advanceTimersByTimeAsync(0);
    expect(badge().textContent).toBe('Bloqueada por 30 s');
    await vi.advanceTimersByTimeAsync(29_000);
    expect(badge().textContent).toBe('Bloqueada por 30 s');
    await vi.advanceTimersByTimeAsync(1_000);
    expect(badge().textContent).toBe('Bloqueada');
  });

  it('another refused PIN during the lockout does not extend it (the ball counts from the last wrong PIN it compared)', async () => {
    vi.useFakeTimers();
    client.result = 'locked-out';
    q<HTMLInputElement>('[data-el=pin]').value = '9999';
    q('[data-act=unlock]').click();
    await vi.advanceTimersByTimeAsync(20_000);
    q('[data-act=unlock]').click(); // refused without comparing: the window does not move
    await vi.advanceTimersByTimeAsync(10_000);
    expect(badge().textContent).toBe('Bloqueada');
  });

  it('a later answer wins over the pending switch', async () => {
    vi.useFakeTimers();
    client.result = 'locked-out';
    q<HTMLInputElement>('[data-el=pin]').value = '9999';
    q('[data-act=unlock]').click();
    await vi.advanceTimersByTimeAsync(0);
    client.status(false);
    await vi.advanceTimersByTimeAsync(30_000);
    expect(badge().hidden).toBe(true);
  });
});
