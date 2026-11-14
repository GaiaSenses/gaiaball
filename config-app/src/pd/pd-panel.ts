// UI for the Pd bridge: the "Pd" block inside the Conexão panel and the status chip in the top bar.
import { renderSwitch } from '../ui/switch.ts';
import { PD_DEFAULT_PORT, PD_ENABLED_KEY, PD_PORT_KEY, type PdBridge, type PdStatus } from './pd-bridge.ts';

export interface PdPrefs { enabled: boolean; port: number }

export function validPort(v: number): boolean {
  return Number.isInteger(v) && v >= 1 && v <= 65535;
}

export function loadPdPrefs(): PdPrefs {
  const prefs: PdPrefs = { enabled: true, port: PD_DEFAULT_PORT };
  try {
    if (localStorage.getItem(PD_ENABLED_KEY) === '0') prefs.enabled = false;
    const p = Number(localStorage.getItem(PD_PORT_KEY));
    if (validPort(p)) prefs.port = p;
  } catch {
    // Storage unavailable: defaults.
  }
  return prefs;
}

function save(key: string, value: string): void {
  try { localStorage.setItem(key, value); } catch { /* per-viewer convenience only */ }
}

export function pdStatusText(s: PdStatus): string {
  switch (s.state) {
    case 'off': return 'Pd: desligado';
    case 'waiting': return 'Pd: aguardando';
    case 'connected': return `Pd: conectado · ${s.rate} pacotes/s`;
    case 'blocked': return 'Pd: bloqueado pelo Chrome';
  }
}

const BLOCKED_HELP =
  'Chrome → ícone à esquerda do endereço → Configurações do site → “Apps neste dispositivo” → Permitir.';

/** Top-bar chip. `demo` just states the mode; the chip always reflects the bridge. */
export function mountPdChip(chip: HTMLElement, bridge: PdBridge): void {
  const render = (): void => {
    const s = bridge.status;
    chip.textContent = pdStatusText(s);
    chip.dataset.state = s.state === 'connected' ? 'on' : s.state === 'blocked' ? 'demo' : 'off';
  };
  bridge.addEventListener('status', render);
  render();
}

export function mountPdBlock(root: HTMLElement, bridge: PdBridge, prefs: PdPrefs, onChange: () => void): void {
  const wrap = document.createElement('div');
  wrap.className = 'pd-block';
  wrap.innerHTML = `
    <h3>Pd</h3>
    <div class="row" data-el="switch" data-info="Envia ao Pd (objeto [gaiaball]) os pacotes de movimento, CO₂ e navegação, exatamente como chegam da bola, por um WebSocket local. Ligado só enquanto a bola está conectada (ou no modo demo)."></div>
    <div class="row"><label class="ctl">Porta
      <input data-el="port" type="number" min="1" max="65535" step="1" inputmode="numeric" aria-label="Porta do Pd" /></label>
      <span data-el="pdstatus" class="status" role="status"></span></div>
    <p class="msg" data-el="portmsg" role="status"></p>
    <p class="msg" data-el="help" hidden>${BLOCKED_HELP}</p>`;
  root.appendChild(wrap);
  const q = <T extends HTMLElement>(sel: string): T => wrap.querySelector<T>(sel)!;

  q('[data-el=switch]').appendChild(renderSwitch({
    options: [{ value: 0, label: 'Desligado' }, { value: 1, label: 'Ligado' }],
    value: prefs.enabled ? 1 : 0,
    label: 'Enviar ao Pd',
    onChange: (v) => {
      prefs.enabled = v === 1;
      save(PD_ENABLED_KEY, prefs.enabled ? '1' : '0');
      onChange();
    },
  }));
  // Visible name next to the switch (the control's own accessible name is "Enviar ao Pd: …").
  const title = document.createElement('span');
  title.textContent = 'Enviar ao Pd';
  q('[data-el=switch]').prepend(title);

  const port = q<HTMLInputElement>('[data-el=port]');
  const portMsg = q('[data-el=portmsg]');
  port.value = String(prefs.port);
  port.addEventListener('change', () => {
    const v = Number(port.value);
    if (!validPort(v)) {
      portMsg.textContent = 'Porta inválida: use um número de 1 a 65535.';
      port.value = String(prefs.port);
      return;
    }
    portMsg.textContent = '';
    prefs.port = v;
    save(PD_PORT_KEY, String(v));
    onChange();
  });

  const statusEl = q('[data-el=pdstatus]');
  const help = q('[data-el=help]');
  const render = (): void => {
    const s = bridge.status;
    statusEl.textContent = pdStatusText(s);
    help.hidden = s.state !== 'blocked';
  };
  bridge.addEventListener('status', render);
  render();
}
