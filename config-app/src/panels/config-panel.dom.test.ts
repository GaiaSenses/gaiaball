// @vitest-environment happy-dom
// The real panel mounted in a DOM: markers, Aplicar (n), keep-edits vs explicit read, presets.
import { beforeEach, describe, expect, it } from 'vitest';
import { mountConfigPanel } from './config-panel';
import { PresetStore } from '../presets/preset-store';
import { demoConfigRegs, demoWrite } from '../demo-config';
import { TLV, type TlvWrite } from '../protocol/tlv';
import type { GaiaballClient } from '../ble/gaiaball-client';

class FakeClient extends EventTarget {
  connected = false;
  regs = demoConfigRegs();
  async readConfig(): Promise<Map<number, DataView>> { return new Map(this.regs); }
  async writeConfig(): Promise<void> {}
  async sendCommand(): Promise<void> {}
  status(connected: boolean): void {
    this.connected = connected;
    this.dispatchEvent(new CustomEvent('status', { detail: { connected, name: 'x' } }));
  }
  /** What an automatic read or the stage bar's read-back does. */
  config(regs: Map<number, DataView> = this.regs): void {
    this.dispatchEvent(new CustomEvent('config', { detail: new Map(regs) }));
  }
}

const HZ = TLV.imuNotifyHz;
// happy-dom has no `Option` constructor; the panel builds <option>s with it.
(globalThis as { Option?: unknown }).Option ??= function Option(text = '', value = ''): HTMLOptionElement {
  const o = document.createElement('option');
  o.textContent = text;
  o.value = value;
  return o;
};

let root: HTMLElement;
let client: FakeClient;
let store: PresetStore;

const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
const input = (id: number): HTMLInputElement => q(`[data-reg="${id}"]`);
const mark = (id: number): HTMLElement => q(`#cfg-ball-${id}`);
const applyLabel = (): string => q('[data-act=apply]').textContent!;
const type = (id: number, v: string): void => {
  const el = input(id);
  el.value = v;
  el.dispatchEvent(new Event('input', { bubbles: true }));
  el.dispatchEvent(new Event('change', { bubbles: true }));
};
const ballRegs = (writes: TlvWrite[]): Map<number, DataView> => {
  const r = demoConfigRegs();
  demoWrite(r, writes);
  return r;
};
/** Loads a preset through its button; the store already holds p1 when the panel is mounted. */
const loadPreset = (): void => {
  q<HTMLSelectElement>('[data-el=plist]').value = 'p1';
  q('[data-act=pload]').click();
};

beforeEach(() => {
  document.body.innerHTML = '<div id="root"></div>';
  root = document.getElementById('root')!;
  client = new FakeClient();
  store = new PresetStore();
  store.save('p1', [{ id: HZ, value: 77 }]);
  mountConfigPanel(root, client as unknown as GaiaballClient, store);
});

describe('config panel markers and Aplicar (n)', () => {
  it('shows no marker and a plain Aplicar before anything was read', () => {
    type(HZ, '77');
    expect(mark(HZ).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar');
  });

  it('marks an edit with the ball value, counts it, and follows the typing live', () => {
    client.status(true);
    client.config();
    expect(applyLabel()).toBe('Aplicar');
    type(HZ, '77');
    expect(mark(HZ).hidden).toBe(false);
    expect(mark(HZ).textContent).toBe('na bola: 50');
    expect(applyLabel()).toBe('Aplicar (1)');
    type(HZ, '50');
    expect(mark(HZ).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar');
  });

  it('says "não informado por este firmware" for a field the ball did not report', () => {
    client.status(true);
    client.regs.delete(TLV.navNotifyHz);
    client.config();
    expect(mark(TLV.navNotifyHz).hidden).toBe(false);
    expect(mark(TLV.navNotifyHz).textContent).toBe('não informado por este firmware');
    expect(mark(HZ).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar'); // such a field is never counted
  });

  it('drops every marker on disconnect and says the screen shows the last reading', () => {
    client.status(true);
    client.config();
    type(HZ, '77');
    client.status(false);
    expect(mark(HZ).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar');
    expect(q('[data-el=hint]').textContent).toContain('última leitura');
  });
});

describe('automatic read keeps edits, explicit read discards them', () => {
  it('a preset loaded offline survives the automatic read on connect and stays marked', () => {
    loadPreset();
    client.status(true);
    client.config(); // ball runs 50
    expect(input(HZ).value).toBe('77');
    expect(mark(HZ).textContent).toBe('na bola: 50');
    expect(applyLabel()).toBe('Aplicar (1)');
    expect(q('[data-el=pmsg]').textContent).toContain('mantido no formulário');
  });

  it('an edit survives an automatic read while untouched fields take the ball value', () => {
    client.status(true);
    client.config();
    type(HZ, '77');
    client.config(ballRegs([{ id: TLV.navNotifyHz, value: 60 }]));
    expect(input(HZ).value).toBe('77');
    expect(input(TLV.navNotifyHz).value).toBe('60');
    expect(applyLabel()).toBe('Aplicar (1)');
  });

  it('an edit that now equals the ball is no longer protected from the next automatic read', () => {
    client.status(true);
    client.config();
    type(HZ, '77');
    client.config(ballRegs([{ id: HZ, value: 77 }]));
    expect(mark(HZ).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar');
    client.config(ballRegs([{ id: HZ, value: 90 }]));
    expect(input(HZ).value).toBe('90');
  });

  it('the explicit read discards edits and the preset, and says so', async () => {
    loadPreset();
    client.status(true);
    client.config();
    q('[data-act=read]').click();
    await new Promise((r) => setTimeout(r, 0));
    expect(input(HZ).value).toBe('50');
    expect(mark(HZ).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar');
    expect(q('[data-el=pmsg]').textContent).toContain('não está mais no formulário');
  });

  it('shows the strip with the IMU in use and the time of the last read', () => {
    client.status(true);
    client.config();
    expect(q('[data-el=strip]').textContent).toMatch(/^Valores lidos da bola às \d\d:\d\d:\d\d · IMU em uso: ICM-20948$/);
    client.status(false);
    expect(q('[data-el=strip]').textContent).toMatch(/^Desconectado — mostrando a última leitura \(\d\d:\d\d:\d\d\)$/);
  });
});
