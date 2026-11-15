// The stage bar ("Modo palco"): everything that stays alive while the rest of the UI is
// paused. Cheap on purpose: one 'imu' listener that only stores the latest sample, and a
// 10 Hz timer that runs only while the bar is visible.
import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import { pdStatusText } from '../pd/pd-panel.ts';
import type { PdBridge } from '../pd/pd-bridge.ts';
import type { ImuSample } from '../protocol/packets.ts';
import { TLV } from '../protocol/tlv.ts';

const REDRAW_MS = 100; // 10 Hz
const METER_MIN_DBFS = -90;
const PULSE_HOLD_MS = 250;
const MIN_DBFS = -90;
const MAX_DBFS = 0;
const LOCK_HINT = 'Desbloqueie com o PIN no painel Conexão';

export type StageSource = 'pot' | 'fixed' | null;
export type StageMetric = 'rms' | 'peak';

/** Louder of the two microphones in the active metric, or null when the audio is not live. */
export function meterLevel(s: Pick<ImuSample, 'audioValid' | 'audioRmsDbfs' | 'audioPeakDbfs'>, metric: StageMetric): number | null {
  if (!s.audioValid) return null;
  const v = metric === 'peak' ? s.audioPeakDbfs : s.audioRmsDbfs;
  return Math.max(v[0], v[1]);
}

export function meterFraction(dbfs: number | null): number {
  if (dbfs === null) return 0;
  return Math.min(1, Math.max(0, (dbfs - METER_MIN_DBFS) / -METER_MIN_DBFS));
}

/** Register value (-dBFS x 100, clamped to the u16 0..9000 the firmware accepts). */
export function sensitivityReg(dbfs: number): number {
  return Math.min(9000, Math.max(0, Math.round(-dbfs * 100)));
}

export interface StageBarHandle {
  show(on: boolean): void;
}

export function mountStageBar(
  root: HTMLElement,
  client: GaiaballClient,
  bridge: PdBridge,
  onExit: () => void,
): StageBarHandle {
  root.innerHTML = `
    <span class="chip" data-el="pd" data-state="off"></span>
    <div class="stage-meter" data-el="meterwrap" title="Nível do maior dos dois microfones">
      <span class="stage-label" data-el="metric">RMS</span>
      <div class="stage-meter-track" role="meter" aria-label="Nível do microfone" aria-valuemin="-90" aria-valuemax="0" data-el="meter"><div class="stage-meter-fill" data-el="fill"></div></div>
      <span class="stage-db" data-el="db">—</span>
    </div>
    <span class="stage-pulse" data-el="pulse" role="img" aria-label="Pulso de sopro"></span><span class="stage-label">sopro</span>
    <div class="stage-sens">
      <label class="ctl" data-el="senswrap">Sensibilidade do sopro
        <input data-el="sens" type="number" min="${MIN_DBFS}" max="${MAX_DBFS}" step="0.5" value="-35.5" inputmode="decimal" aria-label="Sensibilidade do sopro em dBFS" /> dBFS</label>
      <span class="stage-label" data-el="potnote" hidden>controlado pelo potenciômetro</span>
      <span class="msg" data-el="sensmsg" role="status"></span>
    </div>
    <div class="stage-actions">
      <button type="button" data-el="disc">Desconectar</button>
      <button type="button" data-el="exit" class="primary">Sair do modo palco</button>
    </div>`;
  const q = <T extends HTMLElement>(k: string): T => root.querySelector<T>(`[data-el=${k}]`)!;
  const pdEl = q('pd'), fillEl = q('fill'), meterEl = q('meter'), dbEl = q('db'), metricEl = q('metric');
  const pulseEl = q('pulse'), sensEl = q<HTMLInputElement>('sens'), senswrap = q('senswrap');
  const potnote = q('potnote'), msgEl = q('sensmsg'), discBtn = q<HTMLButtonElement>('disc');

  let latest: ImuSample | null = null;
  let source: StageSource = null;
  let metric: StageMetric = 'rms';
  let lastBreathAt = -Infinity;
  let timer = 0;

  const renderPd = (): void => {
    const s = bridge.status;
    pdEl.textContent = pdStatusText(s);
    pdEl.dataset.state = s.state === 'connected' ? 'on' : s.state === 'blocked' ? 'demo' : 'off';
  };
  const renderSource = (): void => {
    const pot = source === 'pot';
    senswrap.hidden = pot;
    potnote.hidden = !pot;
    metricEl.textContent = metric === 'peak' ? 'Pico' : 'RMS';
  };
  const renderLive = (): void => {
    const s = latest;
    const lvl = s ? meterLevel(s, metric) : null;
    fillEl.style.width = `${(meterFraction(lvl) * 100).toFixed(1)}%`;
    dbEl.textContent = lvl === null ? '—' : `${lvl.toFixed(1)} dBFS`;
    if (lvl !== null) meterEl.setAttribute('aria-valuenow', lvl.toFixed(1));
    pulseEl.classList.toggle('on', performance.now() - lastBreathAt < PULSE_HOLD_MS);
  };

  client.addEventListener('imu', (ev) => {
    const s = (ev as CustomEvent<ImuSample>).detail;
    latest = s;
    if (s.breath) lastBreathAt = performance.now();
  });
  client.addEventListener('config', (ev) => {
    const regs = (ev as CustomEvent<Map<number, DataView>>).detail;
    const pot = regs.get(TLV.potControlsThreshold);
    if (pot && pot.byteLength === 1) source = pot.getUint8(0) ? 'pot' : 'fixed';
    const m = regs.get(TLV.breathMetric);
    if (m && m.byteLength === 1) metric = m.getUint8(0) === 1 ? 'peak' : 'rms';
    const thr = regs.get(TLV.breathThresholdNegDbX100);
    if (thr && thr.byteLength === 2 && document.activeElement !== sensEl) {
      sensEl.value = (-thr.getUint16(0, true) / 100).toFixed(1);
    }
    renderSource();
  });
  client.addEventListener('status', (ev) => {
    const connected = (ev as CustomEvent<{ connected: boolean }>).detail.connected;
    discBtn.disabled = !connected;
    if (!connected) { source = null; renderSource(); }
  });
  bridge.addEventListener('status', renderPd);

  sensEl.addEventListener('change', async () => {
    const v = Number(sensEl.value);
    if (!Number.isFinite(v) || v < MIN_DBFS || v > MAX_DBFS) {
      msgEl.textContent = `Use um valor entre ${MIN_DBFS} e ${MAX_DBFS} dBFS.`;
      return;
    }
    const reg = sensitivityReg(v);
    msgEl.textContent = '';
    try {
      await client.writeConfig([{ id: TLV.breathThresholdNegDbX100, value: reg }]);
      const regs = await client.readConfig();
      const back = regs.get(TLV.breathThresholdNegDbX100);
      if (!back || back.byteLength !== 2 || back.getUint16(0, true) !== reg) {
        msgEl.textContent = LOCK_HINT;
        return;
      }
      client.dispatchEvent(new CustomEvent('config', { detail: regs }));
      msgEl.textContent = 'Gravado.';
    } catch {
      msgEl.textContent = client.connected ? LOCK_HINT : 'Sem conexão com a bola.';
    }
  });
  discBtn.addEventListener('click', () => client.disconnect());
  q('exit').addEventListener('click', onExit);

  renderPd();
  renderSource();
  discBtn.disabled = !client.connected;

  return {
    show(on: boolean): void {
      root.hidden = !on;
      clearInterval(timer);
      timer = 0;
      if (on) {
        renderPd();
        renderLive();
        timer = window.setInterval(renderLive, REDRAW_MS);
      }
    },
  };
}
