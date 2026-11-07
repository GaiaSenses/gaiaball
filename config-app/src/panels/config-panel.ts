import type { GaiaballClient, StatusDetail } from '../ble/gaiaball-client.ts';
import { PresetStore } from '../presets/preset-store.ts';
import { encodeTlv, TLV, VALID_OUTPUT_GPIOS, type TlvWrite } from '../protocol/tlv.ts';

const GPIO_OPTIONS: [number, string][] = VALID_OUTPUT_GPIOS.map((p) => [p, `GPIO ${p}`]);

type Kind = 'number' | 'range' | 'checkbox' | 'select' | 'text' | 'pin';
interface Field {
  id: number;
  label: string;
  kind: Kind;
  min?: number;
  max?: number;
  options?: [number, string][];
  hint?: string;
}
interface Group { title: string; fields: Field[] }

const GROUPS: Group[] = [
  { title: 'Orientação', fields: [
    { id: TLV.orientationMode, label: 'Modo', kind: 'select', options: [[0, 'Quat6 (relativo)'], [1, 'Quat9 (absoluto)']] },
    { id: TLV.imuNotifyHz, label: 'Taxa (Hz)', kind: 'number', min: 10, max: 60 },
    { id: TLV.slerpAmount, label: 'Suavização', kind: 'range', min: 0, max: 100 },
  ] },
  { title: 'Áudio', fields: [
    { id: TLV.audioEnabled, label: 'Habilitado', kind: 'checkbox' },
    { id: TLV.audioFastAlphaX1000, label: 'Alfa rápido (×1000)', kind: 'number', min: 0, max: 1000 },
    { id: TLV.audioSlowAlphaX1000, label: 'Alfa lento (×1000)', kind: 'number', min: 0, max: 1000 },
    { id: TLV.debounceMs, label: 'Debounce (ms)', kind: 'number', min: 0, max: 65535 },
    { id: TLV.potControlsThreshold, label: 'Pot controla limiar', kind: 'checkbox' },
    { id: TLV.thresholdFixed, label: 'Limiar fixo', kind: 'number', min: 0, max: 4294967295 },
  ] },
  { title: 'CO₂', fields: [
    { id: TLV.co2Enabled, label: 'Habilitado', kind: 'checkbox' },
    { id: TLV.co2StaleTimeoutMs, label: 'Timeout de dado velho (ms)', kind: 'number', min: 0, max: 65535 },
    { id: TLV.virtualPeakPpm, label: 'Pico virtual (ppm)', kind: 'number', min: 0, max: 65535 },
    { id: TLV.virtualDelayMs, label: 'Atraso virtual (ms)', kind: 'number', min: 0, max: 65535 },
    { id: TLV.virtualHoldMs, label: 'Duração virtual (ms)', kind: 'number', min: 0, max: 65535 },
    { id: TLV.co2Protocol, label: 'Protocolo do sensor', kind: 'select',
      options: [[0, 'Senseair S8 (Modbus, placa atual)'], [1, 'Frames 0x42 0x4D (passivo)']],
      hint: 'A placa KiCAD usa um Senseair S8, que só responde quando consultado (Modbus).' },
    { id: TLV.co2ChecksumVariant, label: 'Variante de checksum (modo passivo)', kind: 'select', options: [[0, 'Variante 0'], [1, 'Variante 1']] },
    { id: TLV.co2RawLogMode, label: 'Log bruto', kind: 'checkbox' },
  ] },
  { title: 'LEDs (1 pino = 1 função; pinos distintos)', fields: [
    { id: TLV.soundLedPin, label: 'Pino LED de som (D2 na placa)', kind: 'select', options: GPIO_OPTIONS },
    { id: TLV.co2LedPin, label: 'Pino LED de CO₂ (D1 na placa)', kind: 'select', options: GPIO_OPTIONS },
    { id: TLV.co2AlertPpm, label: 'Alerta CO₂ (ppm)', kind: 'number', min: 0, max: 65535 },
  ] },
  { title: 'Sistema', fields: [
    { id: TLV.bleName, label: 'Nome BLE (≤16)', kind: 'text' },
    { id: TLV.pin, label: 'Novo PIN (4 dígitos)', kind: 'pin', hint: 'Somente escrita; nunca é lido do dispositivo.' },
  ] },
];
const FIELDS = GROUPS.flatMap((g) => g.fields);
const byId = (id: number): Field => FIELDS.find((f) => f.id === id)!;

function decodeValue(f: Field, dv: DataView): number | string | null {
  if (f.kind === 'text') return new TextDecoder().decode(dv);
  switch (dv.byteLength) {
    case 1: return dv.getUint8(0);
    case 2: return dv.getUint16(0, true);
    case 4: return dv.getUint32(0, true);
    default: return null;
  }
}

export function mountConfigPanel(root: HTMLElement, client: GaiaballClient, store: PresetStore = new PresetStore()): void {
  root.innerHTML = `
    <h2>Configuração</h2>
    <div class="row">
      <button type="button" data-act="read">Ler do dispositivo</button>
      <button type="button" data-act="apply">Aplicar</button>
    </div>
    <p class="msg" data-el="hint">Aplicar vale até reiniciar. Para gravar de forma permanente, use "Salvar no dispositivo" no painel Conexão.</p>
    <p class="msg" data-el="msg" role="status"></p>
    <form data-el="form" onsubmit="return false"></form>
    <h3>Presets</h3>
    <div class="row"><input data-el="pname" type="text" placeholder="Nome do preset" aria-label="Nome do preset" />
      <button type="button" data-act="psave">Salvar preset</button></div>
    <div class="row"><select data-el="plist" aria-label="Presets salvos"></select>
      <button type="button" data-act="pload">Carregar</button>
      <button type="button" data-act="pdel">Excluir</button></div>
    <div class="row"><button type="button" data-act="pexport">Exportar JSON</button>
      <button type="button" data-act="pimport">Importar JSON</button>
      <input data-el="pfile" type="file" accept="application/json,.json" hidden /></div>
    <p class="msg" data-el="pmsg" role="status"></p>`;

  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const form = q<HTMLFormElement>('[data-el=form]');
  const msg = q('[data-el=msg]');
  const pmsg = q('[data-el=pmsg]');
  const inputs = new Map<number, HTMLInputElement | HTMLSelectElement>();

  for (const g of GROUPS) {
    const fs = document.createElement('fieldset');
    fs.innerHTML = `<legend>${g.title}</legend>`;
    for (const f of g.fields) {
      const label = document.createElement('label');
      label.className = 'cfg-field';
      label.append(`${f.label} `);
      let el: HTMLInputElement | HTMLSelectElement;
      if (f.kind === 'select') {
        el = document.createElement('select');
        for (const [v, t] of f.options!) el.add(new Option(t, String(v)));
      } else {
        el = document.createElement('input');
        el.type = f.kind === 'checkbox' ? 'checkbox' : f.kind === 'pin' ? 'password' : f.kind === 'text' ? 'text' : f.kind;
        if (f.min !== undefined) el.min = String(f.min);
        if (f.max !== undefined) el.max = String(f.max);
        if (f.kind === 'text') el.maxLength = 16;
        if (f.kind === 'pin') { el.maxLength = 4; el.inputMode = 'numeric'; el.autocomplete = 'off'; el.placeholder = '····'; }
      }
      el.dataset.reg = String(f.id);
      label.append(el);
      if (f.hint) { const s = document.createElement('small'); s.textContent = f.hint; label.append(s); }
      fs.append(label);
      inputs.set(f.id, el);
    }
    form.append(fs);
  }

  /** Last values known to be on the device (read or read-back). PIN never appears here. */
  let snapshot = new Map<number, number | string>();
  /** Fields the user (or a loaded preset) touched that the device never reported. */
  const dirty = new Set<number>();
  for (const [id, el] of inputs) el.addEventListener('input', () => dirty.add(id));

  const getValue = (f: Field): number | string | null => {
    const el = inputs.get(f.id)!;
    if (f.kind === 'checkbox') return (el as HTMLInputElement).checked ? 1 : 0;
    if (f.kind === 'text' || f.kind === 'pin') return el.value;
    if (el.value.trim() === '') return null;
    return Number(el.value);
  };
  const setValue = (f: Field, v: number | string): void => {
    const el = inputs.get(f.id)!;
    if (f.kind === 'checkbox') (el as HTMLInputElement).checked = Number(v) !== 0;
    else el.value = String(v);
  };
  const populate = (regs: Map<number, DataView>): void => {
    snapshot = new Map();
    dirty.clear();
    for (const f of FIELDS) {
      if (f.id === TLV.pin) continue; // firmware never sends the PIN
      const dv = regs.get(f.id);
      if (!dv) continue;
      const v = decodeValue(f, dv);
      if (v === null) continue;
      snapshot.set(f.id, v);
      setValue(f, v);
    }
    (inputs.get(TLV.pin) as HTMLInputElement).value = '';
  };

  /** Validate one field value; returns a pt-BR error or null. */
  const validate = (f: Field, v: number | string | null): string | null => {
    if (v === null || (typeof v === 'number' && !Number.isFinite(v))) return `${f.label}: informe um número.`;
    if (typeof v === 'number') {
      if (!Number.isInteger(v)) return `${f.label}: use um número inteiro.`;
      if (f.min !== undefined && v < f.min) return `${f.label}: mínimo ${f.min}.`;
      if (f.max !== undefined && v > f.max) return `${f.label}: máximo ${f.max}.`;
    }
    if (f.id === TLV.pin && !/^\d{4}$/.test(String(v))) return `${f.label}: exatamente 4 dígitos.`;
    try { encodeTlv([{ id: f.id, value: v }]); }
    catch (e) {
      if (e instanceof RangeError) return `${f.label}: valor inválido (${f.kind === 'text' ? 'de 1 a 16 bytes' : 'fora da faixa'}).`;
      throw e;
    }
    return null;
  };

  const say = (t: string): void => { msg.textContent = t; };
  const fail = (e: unknown): void => say(e instanceof Error ? e.message : String(e));
  const hasPin = (f: Field): boolean => f.id === TLV.pin;

  /** Mirror of the firmware rule: the two LED functions must sit on distinct GPIOs.
   *  Checks the RESULTING state (written value, else device snapshot, else firmware
   *  default) and only when some LED pin is being written — the firmware check stays
   *  authoritative either way. */
  const LED_PIN_DEFAULTS: ReadonlyMap<number, number> = new Map([
    [TLV.soundLedPin, 2], [TLV.co2LedPin, 4],
  ]);
  const ledPinsError = (regs: TlvWrite[]): string | null => {
    const ids = [...LED_PIN_DEFAULTS.keys()];
    if (!regs.some((r) => ids.includes(r.id))) return null;
    const finalPin = (id: number): number =>
      Number(regs.find((r) => r.id === id)?.value ?? snapshot.get(id) ?? LED_PIN_DEFAULTS.get(id));
    const pins = ids.map(finalPin);
    if (new Set(pins).size !== pins.length)
      return 'Pinos de LED repetidos: som e CO₂ precisam de GPIOs distintos (1 pino = 1 função).';
    return null;
  };

  /** Collect only fields that differ from the snapshot (PIN: only when typed). */
  const collectChanged = (): { regs: TlvWrite[]; error: string | null } => {
    const regs: TlvWrite[] = [];
    for (const f of FIELDS) {
      const v = getValue(f);
      if (hasPin(f)) {
        if (v === '') continue;
      } else if (snapshot.has(f.id)) {
        if (snapshot.get(f.id) === v) continue;
      } else if (!dirty.has(f.id)) continue; // never reported by the device and untouched
      const err = validate(f, v);
      if (err) return { regs: [], error: err };
      regs.push({ id: f.id, value: v as number | string });
    }
    const dupErr = ledPinsError(regs);
    if (dupErr) return { regs: [], error: dupErr };
    return { regs, error: null };
  };

  const doRead = async (): Promise<void> => {
    populate(await client.readConfig());
  };

  q('[data-act=read]').addEventListener('click', () => {
    void doRead().then(() => say('Configuração lida do dispositivo.'), fail);
  });

  q('[data-act=apply]').addEventListener('click', () => {
    void (async () => {
      if (snapshot.size === 0) { say('Leia a configuração do dispositivo antes de aplicar.'); return; }
      const { regs, error } = collectChanged();
      if (error) { say(error); return; }
      if (regs.length === 0) { say('Nenhuma alteração para aplicar.'); return; }
      await client.writeConfig(regs);
      // The firmware cannot signal rejected writes: read back and compare.
      const back = await client.readConfig();
      const rejected: string[] = [];
      for (const r of regs) {
        if (r.id === TLV.pin) continue;
        const f = byId(r.id);
        const dv = back.get(r.id);
        const got = dv ? decodeValue(f, dv) : null;
        if (got !== r.value) rejected.push(f.label);
      }
      populate(back);
      const pinNote = regs.some((r) => r.id === TLV.pin) ? ' Novo PIN enviado (não verificável).' : '';
      say(rejected.length > 0
        ? `⚠ alguns valores não foram aceitos (desbloqueou com o PIN?): ${rejected.join(', ')}.${pinNote}`
        : `Aplicado e verificado (${regs.length} registro(s)).${pinNote}`);
    })().catch(fail);
  });

  // ---- presets ----
  const plist = q<HTMLSelectElement>('[data-el=plist]');
  const pname = q<HTMLInputElement>('[data-el=pname]');
  const pfile = q<HTMLInputElement>('[data-el=pfile]');
  const refreshPresets = (select?: string): void => {
    plist.replaceChildren(...store.list().map((p) => new Option(p.name, p.name)));
    if (select) plist.value = select;
  };
  refreshPresets();

  q('[data-act=psave]').addEventListener('click', () => {
    const name = pname.value.trim();
    if (!name) { pmsg.textContent = 'Informe um nome para o preset.'; return; }
    if (snapshot.size === 0) { pmsg.textContent = 'Leia a configuração do dispositivo antes de salvar um preset.'; return; }
    const regs: TlvWrite[] = [];
    for (const f of FIELDS) {
      if (hasPin(f)) continue; // never persist the PIN in presets
      const v = getValue(f);
      if (v === null) continue; // empty numeric field (never read): not part of the preset
      const err = validate(f, v);
      if (err) { pmsg.textContent = err; return; }
      regs.push({ id: f.id, value: v as number | string });
    }
    store.save(name, regs);
    refreshPresets(name);
    pmsg.textContent = `Preset "${name}" salvo.`;
  });
  q('[data-act=pload]').addEventListener('click', () => {
    const p = store.list().find((x) => x.name === plist.value);
    if (!p) { pmsg.textContent = 'Selecione um preset.'; return; }
    for (const r of p.regs) {
      const f = FIELDS.find((x) => x.id === r.id);
      if (f && !hasPin(f)) { setValue(f, r.value); dirty.add(f.id); }
    }
    pmsg.textContent = `Preset "${p.name}" carregado no formulário. Clique em Aplicar para enviar.`;
  });
  q('[data-act=pdel]').addEventListener('click', () => {
    const name = plist.value;
    if (!name || !confirm(`Excluir o preset "${name}"?`)) return;
    store.remove(name);
    refreshPresets();
    pmsg.textContent = `Preset "${name}" excluído.`;
  });
  q('[data-act=pexport]').addEventListener('click', () => {
    const url = URL.createObjectURL(new Blob([store.exportJson()], { type: 'application/json' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = 'gaiaball-presets.json';
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  q('[data-act=pimport]').addEventListener('click', () => pfile.click());
  pfile.addEventListener('change', () => {
    const file = pfile.files?.[0];
    if (!file) return;
    void file.text().then((t) => {
      const n = store.importJson(t);
      refreshPresets();
      pmsg.textContent = n > 0 ? `${n} preset(s) importado(s).` : 'Arquivo inválido: nada foi importado.';
    }, () => { pmsg.textContent = 'Não foi possível ler o arquivo.'; }).finally(() => { pfile.value = ''; });
  });

  // ---- connection gating (presets stay usable offline) ----
  const gated = [...inputs.values(), q<HTMLButtonElement>('[data-act=read]'), q<HTMLButtonElement>('[data-act=apply]')];
  const render = (d: StatusDetail): void => {
    for (const el of gated) el.disabled = !d.connected;
    if (!d.connected) snapshot = new Map();
  };
  client.addEventListener('status', (ev) => render((ev as CustomEvent<StatusDetail>).detail));
  render({ connected: client.connected, name: null });
}
