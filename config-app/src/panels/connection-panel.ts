import { type GaiaballClient, type StatusDetail } from '../ble/gaiaball-client.ts';
import { decorateInfo } from './info.ts';

// Factory reset (0x12) is intentionally NOT exposed here: too dangerous for a bench panel.
const COMMANDS: { label: string; opcode: number; confirm?: string }[] = [
  { label: 'Zerar orientação', opcode: 0x10 },
  { label: 'Calibrar montagem', opcode: 0x14, confirm: 'Calibrar a montagem agora? Mantenha o globo parado na posição de referência.' },
  { label: 'Salvar no dispositivo', opcode: 0x11 },
  { label: 'Reiniciar', opcode: 0x13, confirm: 'Reiniciar o dispositivo? A conexão será perdida.' },
];

export function mountConnectionPanel(root: HTMLElement, client: GaiaballClient): void {
  root.innerHTML = `
    <h2>Conexão</h2>
    <div class="row"><button data-act="toggle" type="button" class="primary">Conectar</button>
      <span data-el="status" class="status">Desconectado</span></div>
    <div class="row" data-info="PIN de 4 dígitos que desbloqueia comandos e escritas de configuração nesta conexão (padrão de fábrica: 1234). O desbloqueio expira ao desconectar. O firmware não confirma o PIN: com PIN errado, os comandos são silenciosamente ignorados."><input data-el="pin" type="password" inputmode="numeric" maxlength="4"
      pattern="[0-9]{4}" placeholder="PIN" autocomplete="off" aria-label="PIN" />
      <button data-act="unlock" type="button">Desbloquear</button></div>
    <div class="row" data-el="cmds" data-info="Comandos enviados à bola (exigem desbloqueio). “Zerar orientação” define a postura atual como referência; “Calibrar montagem” grava como a placa está montada dentro do globo; “Salvar no dispositivo” persiste a configuração na memória NVS; “Reiniciar” reinicia o firmware."></div>
    <p class="msg" data-el="msg" role="status"></p>`;
  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const toggle = q<HTMLButtonElement>('[data-act=toggle]');
  const unlock = q<HTMLButtonElement>('[data-act=unlock]');
  const pin = q<HTMLInputElement>('[data-el=pin]');
  const statusEl = q('[data-el=status]');
  const msg = q('[data-el=msg]');
  const gated: (HTMLButtonElement | HTMLInputElement)[] = [unlock, pin];

  const report = (text: string): void => { msg.textContent = text; };
  const run = async (fn: () => Promise<void>, ok: string): Promise<void> => {
    try { await fn(); report(ok); }
    catch (e) { report(e instanceof Error ? e.message : String(e)); }
  };

  for (const c of COMMANDS) {
    const b = document.createElement('button');
    b.type = 'button';
    b.textContent = c.label;
    b.addEventListener('click', () => {
      if (c.confirm && !confirm(c.confirm)) return;
      void run(() => client.sendCommand(c.opcode), `${c.label}: enviado.`);
    });
    q('[data-el=cmds]').appendChild(b);
    gated.push(b);
  }
  decorateInfo(root);

  const render = (d: StatusDetail): void => {
    toggle.textContent = d.connected ? 'Desconectar' : 'Conectar';
    statusEl.textContent = d.connected ? `Conectado: ${d.name ?? 'dispositivo'}` : 'Desconectado';
    for (const el of gated) el.disabled = !d.connected;
  };

  toggle.addEventListener('click', () => {
    if (client.connected) { client.disconnect(); return; }
    toggle.disabled = true;
    statusEl.textContent = 'Conectando…';
    void run(() => client.connect(), 'Conectado.').finally(() => {
      toggle.disabled = false;
      render({ connected: client.connected, name: client.deviceName });
    });
  });
  unlock.addEventListener('click', () => {
    void run(
      () => client.unlock(pin.value),
      'PIN enviado (o firmware não confirma; comandos sensíveis são ignorados se o PIN estiver errado).',
    );
  });
  client.addEventListener('status', (ev) => render((ev as CustomEvent<StatusDetail>).detail));
  render({ connected: false, name: null });
}
