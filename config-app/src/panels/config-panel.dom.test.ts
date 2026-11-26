// @vitest-environment happy-dom
// The real panel mounted in a DOM: markers, Aplicar (n), keep-edits vs explicit read, presets.
import { beforeEach, describe, expect, it } from 'vitest';
import { mountConfigPanel, type BreathForm } from './config-panel';
import { PresetStore } from '../presets/preset-store';
import { demoConfigRegs, demoWrite } from '../demo-config';
import { TLV, type TlvWrite } from '../protocol/tlv';
import type { GaiaballClient } from '../ble/gaiaball-client';

class FakeClient extends EventTarget {
  connected = false;
  regs = demoConfigRegs();
  async readConfig(): Promise<Map<number, DataView>> { return new Map(this.regs); }
  async writeConfig(_w: TlvWrite[]): Promise<void> {}
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


describe('Aplicar on a locked ball (TLV 0x37)', () => {
  const withLock = (r: Map<number, DataView>, v: number): Map<number, DataView> => {
    r.set(TLV.unlockState, new DataView(new Uint8Array([v]).buffer));
    return r;
  };
  const msg = (): string => q('[data-el=msg]').textContent!;

  it('says the ball is locked and how to unlock, instead of a bare list of rejected fields', async () => {
    client.status(true);
    client.config(withLock(demoConfigRegs(), 0));
    type(HZ, '77');
    client.regs = withLock(demoConfigRegs(), 0); // the ball ignored the write: still 50, still locked
    q('[data-act=apply]').click();
    await new Promise((r) => setTimeout(r, 0));
    expect(msg()).toMatch(/A bola está bloqueada/);
    expect(msg()).toMatch(/Desbloquear/);
  });

  it('keeps the field list when the ball is unlocked but still refused a value', async () => {
    client.status(true);
    client.config(withLock(demoConfigRegs(), 1));
    type(HZ, '77');
    client.regs = withLock(demoConfigRegs(), 1);
    q('[data-act=apply]').click();
    await new Promise((r) => setTimeout(r, 0));
    expect(msg()).toMatch(/alguns valores não foram aceitos/);
    expect(msg()).not.toMatch(/bloqueada/);
  });

  it('explains a GATT failure in Portuguese', async () => {
    client.status(true);
    client.config();
    type(HZ, '77');
    client.writeConfig = async () => { throw new DOMException('GATT operation failed for unknown reason.', 'NetworkError'); };
    q('[data-act=apply]').click();
    await new Promise((r) => setTimeout(r, 0));
    expect(msg()).toMatch(/^A bola não respondeu pelo Bluetooth/);
  });
});

describe('Sensibilidade: one box, the knob shows inside it', () => {
  const SENS = TLV.breathThresholdNegDbX100;
  /** A sensors packet with only what the panel reads (the knob's ADC reading). */
  const knob = (potRaw: number): void => {
    client.dispatchEvent(new CustomEvent('sensors', { detail: { potRaw } }));
  };
  const potBox = (): HTMLInputElement => q('[data-el=pot-dbfs]');
  const row = (): HTMLElement => input(SENS).closest<HTMLElement>('.cfg-field')!;
  const potSource = (): Map<number, DataView> => ballRegs([{ id: TLV.potControlsThreshold, value: 1 }]);
  const fixedSource = (): Map<number, DataView> => ballRegs([{ id: TLV.potControlsThreshold, value: 0 }]);

  it('potentiometer: the box shows the knob in dBFS, locked, with no side label', () => {
    client.status(true);
    client.config(potSource());
    knob(2048); // -60 + 2048/4095 * 60
    expect(potBox().hidden).toBe(false);
    expect(potBox().disabled).toBe(true);
    expect(potBox().value).toBe('-29.99');
    expect(input(SENS).hidden).toBe(true);
    expect(row().textContent).not.toContain('Potenciômetro');
    expect(row().querySelector('.cfg-pot')).toBeNull();
    // The name and the box sit in the row itself, as in every other row.
    expect(potBox().parentElement).toBe(row());
    expect(input(SENS).parentElement).toBe(row());
  });

  it('potentiometer: the box follows the knob live', () => {
    client.status(true);
    client.config(potSource());
    knob(0);
    expect(potBox().value).toBe('-60.00');
    knob(4095);
    expect(potBox().value).toBe('0.00');
  });

  it('the knob never becomes the register value: no marker, nothing to apply', () => {
    client.status(true);
    client.config(potSource());
    knob(4095);
    expect(mark(SENS).hidden).toBe(true);
    expect(applyLabel()).toBe('Aplicar');
  });

  it('potentiometer before any sensors packet: an empty locked box', () => {
    client.status(true);
    client.config(potSource());
    expect(potBox().hidden).toBe(false);
    expect(potBox().value).toBe('');
    expect(potBox().disabled).toBe(true);
  });

  it('fixed value: the same place holds the stored value, editable', () => {
    client.status(true);
    client.config(fixedSource());
    knob(4095);
    expect(potBox().hidden).toBe(true);
    expect(input(SENS).hidden).toBe(false);
    expect(input(SENS).disabled).toBe(false);
    expect(input(SENS).value).toBe('-35.50');
  });

  it('switching the source swaps the box at once, both ways', () => {
    client.status(true);
    client.config(potSource());
    knob(4095);
    type(TLV.potControlsThreshold, '0');
    expect(potBox().hidden).toBe(true);
    expect(input(SENS).hidden).toBe(false);
    expect(input(SENS).disabled).toBe(false);
    expect(input(SENS).value).toBe('-35.50');
    type(TLV.potControlsThreshold, '1');
    expect(potBox().hidden).toBe(false);
    expect(potBox().value).toBe('0.00');
    expect(input(SENS).hidden).toBe(true);
  });

  it('disconnected: both boxes stay locked', () => {
    client.status(true);
    client.config(fixedSource());
    client.status(false);
    expect(input(SENS).disabled).toBe(true);
    expect(potBox().disabled).toBe(true);
  });
});

describe('band-pass controls (slope, Q) and the spectrum band', () => {
  const mountWithBand = () => {
    document.body.innerHTML = '<div id="root"></div>';
    root = document.getElementById('root')!;
    const { band } = mountConfigPanel(root, client as unknown as GaiaballClient, store);
    client.status(true);
    client.config();
    return band;
  };

  it('a slope changed and put back is not a pending band', () => {
    const band = mountWithBand();
    expect(band.current()).toMatchObject({ hpSlopeDbOct: 12, hpQX100: 71, edited: false });
    type(TLV.audioHpSlopeDbOct, '24');
    expect(band.current()).toMatchObject({ hpSlopeDbOct: 24, edited: true });
    type(TLV.audioHpSlopeDbOct, '12');
    expect(band.current().edited).toBe(false);
    expect(applyLabel()).toBe('Aplicar');
  });

  it('6 dB/oct hides the Q; an emptied hidden Q does not block Aplicar', async () => {
    mountWithBand();
    const qRow = (): HTMLElement => input(TLV.audioLpQX100).closest('.cfg-field') as HTMLElement;
    type(TLV.audioLpQX100, '');
    type(TLV.audioLpSlopeDbOct, '6');
    expect(qRow().closest('[hidden]')).not.toBeNull();
    q('[data-act=apply]').click();
    await new Promise((r) => setTimeout(r, 0));
    expect(root.textContent).not.toMatch(/informe um número/);
  });
});

describe('breath discriminators (firmware v10)', () => {
  const COH = TLV.breathCoherenceMaxX100;
  const toggleOf = (id: number): HTMLElement => input(id).closest('.cfg-field')!.querySelector<HTMLElement>('[role=switch], .toggle, button')!;

  // The first field with both an on/off toggle and a scale: it must show 0,60, not the wire 60.
  it('shows the coherence limit in display units and reads back with nothing pending', () => {
    client.status(true);
    client.config();
    expect(input(COH).value).toBe('0.60');
    expect(input(TLV.micSpacingMmX10).value).toBe('15.4');
    expect(applyLabel()).toBe('Aplicar');
  });

  it('sends the wire value of a typed limit, and 100 when switched off', async () => {
    const sent: TlvWrite[][] = [];
    client.writeConfig = async (w: TlvWrite[]) => { sent.push(w); };
    client.status(true);
    client.config();
    type(COH, '0.45');
    expect(applyLabel()).toBe('Aplicar (1)');
    q('[data-act=apply]').click();
    await new Promise((r) => setTimeout(r, 0));
    expect(sent.flat().find((w) => w.id === COH)?.value).toBe(45);
    const off = ballRegs([{ id: COH, value: 100 }]);
    client.config(off);
    expect(input(COH).value).toBe('0.60'); // off: the box keeps the "on" default for turning it back on
    expect(toggleOf(COH)).toBeTruthy();
  });

  it('the breath source follows the form: coherence limit (null when off), still only, rotation limit', () => {
    document.body.innerHTML = '<div id="root"></div>';
    root = document.getElementById('root')!;
    client = new FakeClient();
    const seen: BreathForm[] = [];
    const { breath } = mountConfigPanel(root, client as unknown as GaiaballClient, new PresetStore());
    breath.subscribe((b) => seen.push(b));
    client.status(true);
    client.config(ballRegs([{ id: COH, value: 35 }]));
    expect(seen.at(-1)).toEqual({ coherenceLimit: 0.35, stillOnly: true, maxDps: 20 });
    client.config(ballRegs([{ id: COH, value: 100 }, { id: TLV.breathStillOnly, value: 0 }, { id: TLV.stillMaxDps, value: 45 }]));
    expect(seen.at(-1)).toEqual({ coherenceLimit: null, stillOnly: false, maxDps: 45 });
  });

  it('"Sopro só com a bola parada" governs the four stillness thresholds', () => {
    client.status(true);
    client.config(ballRegs([{ id: TLV.breathStillOnly, value: 0 }]));
    for (const id of [TLV.stillMaxDps, TLV.stillMaxAccelMg, TLV.stillHoldMs, TLV.stillWindowMs]) expect(input(id).disabled).toBe(true);
    client.config(ballRegs([{ id: TLV.breathStillOnly, value: 1 }]));
    for (const id of [TLV.stillMaxDps, TLV.stillMaxAccelMg, TLV.stillHoldMs, TLV.stillWindowMs]) expect(input(id).disabled).toBe(false);
    expect(applyLabel()).toBe('Aplicar');
  });
});
