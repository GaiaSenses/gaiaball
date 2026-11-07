import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import type { StreamStats } from '../metrics/stream-stats.ts';
import type { Co2Sample, DiagSample } from '../protocol/packets.ts';
import { resolvedTheme } from '../theme.ts';
import { decorateInfo } from './info.ts';
import { RingBuffer, drawSparkline } from './sparkline.ts';

const POINTS = 60;
const UPDATE_MS = 250;
const SPARK = {
  dark: { hz: '#4cc2ff', loss: '#ff8a65' },
  light: { hz: '#0369a1', loss: '#c2410c' },
};

export function mountMetricsPanel(root: HTMLElement, client: GaiaballClient, stats: StreamStats): void {
  root.innerHTML = `
    <h2>Métricas <span class="msg">— medidas aqui, no navegador</span></h2>
    <dl class="metrics">
      <dt data-info="Pacotes de orientação chegando por segundo — deve acompanhar a “Taxa (Hz)” configurada. Unidade: hertz (Hz).">Taxa</dt><dd id="m-hz">–</dd><canvas id="sp-hz" width="120" height="28"></canvas>
      <dt data-info="Variação do intervalo entre pacotes consecutivos: quanto menor, mais regular o fluxo chega. Unidade: milissegundos (ms).">Jitter</dt><dd id="m-jit">–</dd><span></span>
      <dt data-info="Pacotes pulados, detectados pelo contador de sequência (seq) de cada pacote. Unidade: % do total enviado.">Perda</dt><dd id="m-loss">–</dd><canvas id="sp-loss" width="120" height="28"></canvas>
      <dt data-info="Há quanto tempo chegou o último pacote de orientação. Unidade: milissegundos (ms).">Idade da amostra</dt><dd id="m-age">–</dd><span></span>
    </dl>
    <h3>Firmware <span class="msg">— medidas pela própria bola (diagnóstico a 1 Hz)</span></h3>
    <dl class="metrics">
      <dt data-info="Taxa real de amostragem da IMU, medida pelo firmware. Unidade: hertz (Hz).">IMU</dt><dd id="d-imuhz">–</dd><span></span>
      <dt data-info="Taxa real de notificações BLE de orientação enviadas pelo firmware. Unidade: hertz (Hz).">Notify</dt><dd id="d-nothz">–</dd><span></span>
      <dt data-info="Contador acumulado de falhas de leitura da IMU no barramento I²C desde o boot. Adimensional.">Erros I²C da IMU</dt><dd id="d-fifo">–</dd><span></span>
      <dt data-info="Contador acumulado de falhas na captura de áudio dos microfones (barramento I²S). Adimensional.">Erros I²S</dt><dd id="d-i2s">–</dd><span></span>
      <dt data-info="Quadros do sensor de CO₂ descartados por erro de formato ou de checksum na UART. Adimensional.">Erros UART</dt><dd id="d-uart">–</dd><span></span>
      <dt data-info="Memória RAM livre agora / mínimo desde o boot — um mínimo que só cai indica vazamento. Unidade: kilobytes (KB).">Heap livre / mín</dt><dd id="d-heap">–</dd><span></span>
      <dt data-info="Autoteste de cada subsistema (IMU, áudio, CO₂) reportado pelo firmware. Sem unidade.">Saúde</dt><dd id="d-flags">–</dd><span></span>
      <dt data-info="Última leitura de CO₂. “virtual” = pulso disparado por sopro; “desatualizado” = sem quadro novo do sensor dentro do timeout. Unidade: ppm.">CO₂</dt><dd id="d-co2">–</dd><span></span>
    </dl>`;
  decorateInfo(root);
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
    const spark = SPARK[resolvedTheme()];
    drawSparkline(spHz, hzBuf.values(), spark.hz);
    drawSparkline(spLoss, lossBuf.values(), spark.loss);
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
