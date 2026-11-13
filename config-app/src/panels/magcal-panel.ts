import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import {
  fitMagCalibration,
  IDENTITY_MAG_CAL,
  magCalToFixed,
  MAG_FIT_MIN_SAMPLES,
  type MagFit,
} from '../magcal/fit.ts';
import type { DiagSample, MagChunkSample } from '../protocol/packets.ts';
import { encodeMagCalRecord, TLV } from '../protocol/tlv.ts';
import { decorateInfo } from './info.ts';

const CMD_MAG_START = 0x15;
const CMD_MAG_STOP = 0x16;
const CMD_SAVE = 0x11;
/** Enough for a slow full sweep; older samples are dropped so a long run stays bounded. */
const MAX_SAMPLES = 4000;
const REFIT_MS = 400;

/** The eight sign-octants, labelled by the axis directions they combine. */
const OCTANTS = ['−X−Y−Z', '+X−Y−Z', '−X+Y−Z', '+X+Y−Z', '−X−Y+Z', '+X−Y+Z', '−X+Y+Z', '+X+Y+Z'];

function octantOf(v: readonly [number, number, number], dead: number): number | null {
  if (Math.abs(v[0]) <= dead || Math.abs(v[1]) <= dead || Math.abs(v[2]) <= dead) return null;
  return (v[0] > 0 ? 1 : 0) | (v[1] > 0 ? 2 : 0) | (v[2] > 0 ? 4 : 0);
}

export function mountMagCalPanel(root: HTMLElement, client: GaiaballClient): void {
  root.innerHTML = `
    <h2>Magnetômetro</h2>
    <p class="msg">O modo Quat9 só dá um rumo confiável depois desta calibração: ímã do alto-falante, bateria e
      trilhas somam um campo fixo (hard iron) e o ferro em volta deforma a esfera do campo terrestre (soft iron).
      A bola envia as leituras cruas; o ajuste acontece aqui no navegador.</p>
    <div class="row">
      <button type="button" data-act="start" class="primary">Iniciar calibração</button>
      <button type="button" data-act="stop" disabled>Parar</button>
    </div>
    <ol class="mag-steps">
      <li>Afaste a bola de mesas metálicas, caixas de som e computadores.</li>
      <li>Gire devagar, desenhando um 8 no ar, até os oito setores abaixo acenderem.</li>
      <li>Com resíduo abaixo de 3 %, grave a calibração.</li>
    </ol>
    <div class="mag-octants" data-el="octants" role="img"
      data-info="Os oito setores em que a esfera do campo foi dividida. Um setor acende quando a bola passou por aquela combinação de direções. Oito acesos = varredura completa; com poucos setores o ajuste parece bom e erra, porque nunca viu o outro lado da esfera."></div>
    <dl class="metrics">
      <dt data-info="Leituras cruas recebidas nesta sessão de calibração. O ajuste começa com ${MAG_FIT_MIN_SAMPLES}.">Amostras</dt><dd data-el="count">–</dd><span></span>
      <dt data-info="Quantos dos oito setores da esfera já foram visitados.">Cobertura</dt><dd data-el="cover">–</dd><span></span>
      <dt data-info="Intensidade do campo que a calibração vai preservar — no Brasil, entre 22 e 28 µT. Unidade: microtesla (µT).">Campo</dt><dd data-el="field">–</dd><span></span>
      <dt data-info="Quanto as leituras corrigidas ainda se afastam de uma esfera perfeita, em RMS. Abaixo de 3 % é um bom ajuste; acima disso, gire mais devagar e cubra mais direções.">Resíduo</dt><dd data-el="resid">–</dd><span></span>
    </dl>
    <div class="row">
      <button type="button" data-act="apply" disabled>Gravar calibração na bola</button>
      <button type="button" data-act="clear">Remover calibração</button>
    </div>
    <p class="msg" data-el="msg" role="status">Desbloqueie a conexão com o PIN antes de calibrar.</p>`;
  decorateInfo(root);

  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const btnStart = q<HTMLButtonElement>('[data-act=start]');
  const btnStop = q<HTMLButtonElement>('[data-act=stop]');
  const btnApply = q<HTMLButtonElement>('[data-act=apply]');
  const btnClear = q<HTMLButtonElement>('[data-act=clear]');
  const msg = q('[data-el=msg]');
  const cells: HTMLElement[] = OCTANTS.map((label) => {
    const el = document.createElement('span');
    el.className = 'mag-octant';
    el.textContent = label;
    q('[data-el=octants]').append(el);
    return el;
  });

  let samples: [number, number, number][] = [];
  let fit: MagFit | null = null;
  let capturing = false;
  let lastFitMs = 0;
  /** Octants seen in the RAW cloud, which is what tells the user where to turn next. */
  const seen = new Set<number>();

  const say = (t: string): void => { msg.textContent = t; };
  const fail = (e: unknown): void => say(e instanceof Error ? e.message : String(e));

  const render = (): void => {
    q('[data-el=count]').textContent = samples.length ? String(samples.length) : '–';
    q('[data-el=cover]').textContent = `${seen.size} de 8 setores`;
    q('[data-el=field]').textContent = fit ? `${fit.radiusUt.toFixed(1)} µT` : '–';
    q('[data-el=resid]').textContent = fit ? `${fit.residualPct.toFixed(1)} %` : '–';
    for (let i = 0; i < cells.length; i++) cells[i].classList.toggle('on', seen.has(i));
    btnApply.disabled = fit === null;
    btnStart.disabled = capturing;
    btnStop.disabled = !capturing;
  };

  const refit = (): void => {
    // The coverage test needs a centre to measure from, and before the first fit the
    // raw cloud is still offset: use the running mean, which is close enough to sort
    // samples into octants while the sweep is in progress.
    const n = samples.length;
    if (n === 0) return;
    const mean = samples.reduce((a, s) => [a[0] + s[0] / n, a[1] + s[1] / n, a[2] + s[2] / n], [0, 0, 0]);
    const spread = Math.hypot(
      ...([0, 1, 2].map((a) => Math.max(...samples.map((s) => Math.abs(s[a] - mean[a])))) as number[]),
    );
    seen.clear();
    const dead = 0.2 * (spread / Math.sqrt(3));
    for (const s of samples) {
      const o = octantOf([s[0] - mean[0], s[1] - mean[1], s[2] - mean[2]], dead);
      if (o !== null) seen.add(o);
    }
    fit = fitMagCalibration(samples);
    render();
  };

  client.addEventListener('magchunk', (ev) => {
    if (!capturing) return;
    const chunk = (ev as CustomEvent<MagChunkSample>).detail;
    for (const s of chunk.samples) samples.push(s);
    if (samples.length > MAX_SAMPLES) samples = samples.slice(-MAX_SAMPLES);
    const now = performance.now();
    if (now - lastFitMs < REFIT_MS) return;
    lastFitMs = now;
    refit();
  });

  // The device is the source of truth about whether a capture is running (it also
  // stops on its own after three minutes).
  client.addEventListener('diag', (ev) => {
    const d = (ev as CustomEvent<DiagSample>).detail;
    if (capturing && !d.magCapturing && samples.length > 0) {
      capturing = false;
      refit();
      say(fit ? 'Captura encerrada pela bola. Confira o resíduo e grave.' : 'Captura encerrada sem amostras suficientes.');
    }
  });

  btnStart.addEventListener('click', async () => {
    samples = [];
    seen.clear();
    fit = null;
    try {
      await client.sendCommand(CMD_MAG_START);
      capturing = true;
      render();
      say('Capturando. Gire a bola devagar em todos os sentidos.');
    } catch (e) { fail(e); }
  });

  btnStop.addEventListener('click', async () => {
    try {
      await client.sendCommand(CMD_MAG_STOP);
      capturing = false;
      refit();
      say(fit ? 'Captura parada. Confira o resíduo e grave.' : 'Poucas amostras para ajustar.');
    } catch (e) { fail(e); }
  });

  btnApply.addEventListener('click', async () => {
    if (!fit) return;
    if (fit.octants < 6 && !confirm(`A varredura cobriu só ${fit.octants} de 8 setores. Gravar mesmo assim?`)) return;
    const { hard, soft } = magCalToFixed(fit.cal);
    try {
      await client.writeConfig([{ id: TLV.magCal, value: encodeMagCalRecord(hard, soft) }]);
      await client.sendCommand(CMD_SAVE);
      if (capturing) { await client.sendCommand(CMD_MAG_STOP); capturing = false; }
      render();
      say(`Calibração gravada: campo ${fit.radiusUt.toFixed(1)} µT, resíduo ${fit.residualPct.toFixed(1)} %.`);
    } catch (e) { fail(e); }
  });

  btnClear.addEventListener('click', async () => {
    if (!confirm('Remover a calibração gravada? O modo Quat9 volta a usar o magnetômetro cru.')) return;
    const { hard, soft } = magCalToFixed(IDENTITY_MAG_CAL);
    try {
      await client.writeConfig([{ id: TLV.magCal, value: encodeMagCalRecord(hard, soft) }]);
      await client.sendCommand(CMD_SAVE);
      say('Calibração removida.');
    } catch (e) { fail(e); }
  });

  render();
}
