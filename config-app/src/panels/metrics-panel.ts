import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import type { StreamStats } from '../metrics/stream-stats.ts';
import type { Co2Sample, DiagSample } from '../protocol/packets.ts';
import { RingBuffer, drawSparkline } from './sparkline.ts';

const POINTS = 60;
const UPDATE_MS = 250;

export function mountMetricsPanel(root: HTMLElement, client: GaiaballClient, stats: StreamStats): void {
  root.innerHTML = `
    <h2>Métricas</h2>
    <dl class="metrics">
      <dt>Taxa</dt><dd id="m-hz">–</dd><canvas id="sp-hz" width="120" height="28"></canvas>
      <dt>Jitter</dt><dd id="m-jit">–</dd><span></span>
      <dt>Perda</dt><dd id="m-loss">–</dd><canvas id="sp-loss" width="120" height="28"></canvas>
      <dt>Idade da amostra</dt><dd id="m-age">–</dd><span></span>
    </dl>
    <h2>Firmware</h2>
    <dl class="metrics">
      <dt>IMU</dt><dd id="d-imuhz">–</dd><span></span>
      <dt>Notify</dt><dd id="d-nothz">–</dd><span></span>
      <dt>Erros I²C da IMU</dt><dd id="d-fifo">–</dd><span></span>
      <dt>Erros I²S</dt><dd id="d-i2s">–</dd><span></span>
      <dt>Erros UART</dt><dd id="d-uart">–</dd><span></span>
      <dt>Heap livre / mín</dt><dd id="d-heap">–</dd><span></span>
      <dt>Saúde</dt><dd id="d-flags">–</dd><span></span>
      <dt>CO₂</dt><dd id="d-co2">–</dd><span></span>
    </dl>`;
  const $ = (id: string) => root.querySelector<HTMLElement>('#' + id)!;
  const hzBuf = new RingBuffer(POINTS), lossBuf = new RingBuffer(POINTS);
  const spHz = $('sp-hz') as HTMLCanvasElement, spLoss = $('sp-loss') as HTMLCanvasElement;
  let diag: DiagSample | null = null;
  let co2: Co2Sample | null = null;

  client.addEventListener('diag', (ev) => { diag = (ev as CustomEvent<DiagSample>).detail; });
  client.addEventListener('co2', (ev) => { co2 = (ev as CustomEvent<Co2Sample>).detail; });

  const flag = (name: string, ok: boolean) =>
    `<span class="${ok ? 'ok' : 'fail'}">${name} ${ok ? 'OK' : 'falha'}</span>`;

  setInterval(() => {
    if (document.hidden) return;
    const hz = stats.hz(), loss = stats.lossPct();
    hzBuf.push(hz); lossBuf.push(loss);
    $('m-hz').textContent = `${hz.toFixed(1)} Hz`;
    $('m-jit').textContent = `${stats.jitterMs().toFixed(1)} ms`;
    $('m-loss').textContent = `${loss.toFixed(2)} %`;
    const age = stats.ageMs();
    $('m-age').textContent = Number.isFinite(age) ? `${Math.round(age)} ms` : '–';
    drawSparkline(spHz, hzBuf.values(), '#4cc2ff');
    drawSparkline(spLoss, lossBuf.values(), '#ff8a65');
    if (diag) {
      $('d-imuhz').textContent = `${(diag.imuHzX10 / 10).toFixed(1)} Hz`;
      $('d-nothz').textContent = `${(diag.notifyHzX10 / 10).toFixed(1)} Hz`;
      $('d-fifo').textContent = String(diag.fifoOverflows);
      $('d-i2s').textContent = String(diag.i2sErrors);
      $('d-uart').textContent = String(diag.uartFrameErrors);
      $('d-heap').textContent = `${diag.freeHeapKb} / ${diag.minFreeHeapKb} KB`;
      $('d-flags').innerHTML = [flag('IMU', diag.imuOk), flag('Áudio', diag.audioOk), flag('CO₂', diag.co2Ok)].join(' ');
    }
    if (co2) {
      $('d-co2').innerHTML = `${co2.ppm} ppm` +
        (co2.stale ? ' <span class="badge warn">desatualizado</span>' : '') +
        (co2.virtualActive ? ' <span class="badge">virtual</span>' : '');
    }
  }, UPDATE_MS);
}
