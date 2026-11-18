import { type GaiaballClient, type StatusDetail } from '../ble/gaiaball-client.ts';
import { bleErrorMessage, lockBadgeText, lockStateFrom, unlockResultMessage, type LockState } from '../ble/unlock-state.ts';
import { decorateInfo } from './info.ts';

// Factory reset (0x12) is intentionally NOT exposed here: too dangerous for a bench panel.
const COMMANDS: { label: string; opcode: number; confirm?: string }[] = [
  { label: 'Zerar orientação', opcode: 0x10 },
  { label: 'Calibrar montagem', opcode: 0x14, confirm: 'Calibrar a montagem agora? Mantenha o globo parado na posição de referência.' },
  { label: 'Salvar no dispositivo', opcode: 0x11 },
  { label: 'Reiniciar', opcode: 0x13, confirm: 'Reiniciar o dispositivo? A conexão será perdida.' },
];

/** CommandGate::kLockoutMs: after 5 wrong PINs the ball refuses new ones for this long. */
const LOCKOUT_MS = 30_000;

export function mountConnectionPanel(root: HTMLElement, client: GaiaballClient): void {
  root.innerHTML = `
    <h2>Conexão</h2>
    <div class="row"><button data-act="toggle" type="button" class="primary">Conectar</button>
      <span data-el="status" class="status">Desconectado</span></div>
    <div class="row" data-info="PIN de 4 dígitos que desbloqueia comandos e escritas de configuração nesta conexão (padrão de fábrica: 1234). O desbloqueio expira ao desconectar. Ao lado do botão aparece o que a bola informa: Bloqueada, Desbloqueada ou Bloqueada por 30 s (depois de 5 PINs errados seguidos). Firmware antigo não informa: nele, com PIN errado, os comandos são silenciosamente ignorados."><input data-el="pin" type="password" inputmode="numeric" maxlength="4"
      pattern="[0-9]{4}" placeholder="PIN" autocomplete="off" aria-label="PIN" />
      <button data-act="unlock" type="button">Desbloquear</button>
      <span data-el="lock" class="lock-state" hidden></span></div>
    <div class="row" data-el="cmds" data-info="Comandos enviados à bola (exigem desbloqueio). “Zerar orientação” define a postura atual como referência (e, na navegação, leva o globo ao ponto de referência — ou ao ponto da âncora, se houver uma ativa: o site do GaiaSenses ancora a bola com o comando 0x17 ao conectar). O zero não é gravado, e não precisa: a bola é ligada apoiada numa superfície plana e essa postura do boot já é a referência (não existe uma “frente” marcada, o movimento é relativo a ela). Zerar só serve para trocar a referência sem religar, com a bola parada na superfície; “Calibrar montagem” grava como a placa está montada dentro do globo; “Salvar no dispositivo” persiste a configuração na memória NVS; “Reiniciar” reinicia o firmware."></div>
    <p class="msg" data-el="msg" role="status"></p>`;
  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const toggle = q<HTMLButtonElement>('[data-act=toggle]');
  const unlock = q<HTMLButtonElement>('[data-act=unlock]');
  const pin = q<HTMLInputElement>('[data-el=pin]');
  const statusEl = q('[data-el=status]');
  const msg = q('[data-el=msg]');
  const lock = q('[data-el=lock]');
  const gated: (HTMLButtonElement | HTMLInputElement)[] = [unlock, pin];

  const report = (text: string): void => { msg.textContent = text; };
  const run = async (fn: () => Promise<void>, ok: string): Promise<void> => {
    try { await fn(); report(ok); }
    catch (e) { report(bleErrorMessage(e)); }
  };
  /** What the ball says about this connection (TLV 0x37); hidden when it cannot tell or the link is down. */
  let lockoutEnd: ReturnType<typeof setTimeout> | undefined;
  const showLock = (state: LockState): void => {
    // The ball's window runs from the last wrong PIN it COMPARED; the ones it refuses during the
    // lockout do not move it, so an answer that finds the window already open keeps its end.
    if (state === 'locked-out' && lockoutEnd !== undefined) return;
    clearTimeout(lockoutEnd);
    lockoutEnd = undefined;
    lock.textContent = lockBadgeText(state);
    lock.dataset.state = state;
    lock.hidden = state === 'unknown';
    if (state === 'locked-out') lockoutEnd = setTimeout(() => { lockoutEnd = undefined; showLock('locked'); }, LOCKOUT_MS);
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
    if (!d.connected) showLock('unknown'); // the unlock dies with the connection
  };

  toggle.addEventListener('click', () => {
    if (client.connected) { client.disconnect(); return; }
    toggle.disabled = true;
    statusEl.textContent = 'Conectando…';
    report('');
    void run(() => client.connect(), 'Conectado.').finally(() => {
      toggle.disabled = false;
      render({ connected: client.connected, name: client.deviceName });
    });
  });
  unlock.addEventListener('click', () => {
    void (async () => {
      try {
        const state = await client.unlock(pin.value);
        showLock(state);
        report(unlockResultMessage(state));
      } catch (e) {
        report(bleErrorMessage(e));
      }
    })();
  });
  // Every configuration read (automatic on connect, Aplicar's read-back) carries the state.
  client.addEventListener('config', (ev) => showLock(lockStateFrom((ev as CustomEvent<Map<number, DataView>>).detail)));
  client.addEventListener('status', (ev) => render((ev as CustomEvent<StatusDetail>).detail));
  render({ connected: false, name: null });
}
