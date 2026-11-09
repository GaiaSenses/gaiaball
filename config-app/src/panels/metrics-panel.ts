import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import type { StreamStats } from '../metrics/stream-stats.ts';
import { AHRS_FLAG_OVERRANGE, type Co2Sample, type DiagSample } from '../protocol/packets.ts';
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
      <dt data-info="O que a fusão está fazendo agora: “partida” = ganho ainda em rampa (converge depois do boot); “saturação” = o giroscópio passou do fundo de escala e o filtro reiniciou; “accel ignorado” / “mag ignorado” = a rejeição está agindo, protegendo a orientação de aceleração ou de um ímã. Sem unidade.">Estado da fusão</dt><dd id="d-ahrs">–</dd><span></span>
      <dt data-info="Discordância entre a inclinação estimada e a que o acelerômetro indica (e, no Quat9, o mesmo para o rumo e o magnetômetro). É o valor comparado com os limiares de rejeição. Unidade: graus (°).">Discordância incl./rumo</dt><dd id="d-err">–</dd><span></span>
      <dt data-info="Offset de velocidade angular estimado em repouso e subtraído de cada leitura — é ele que, se não fosse removido, viraria deriva de rumo. “aprendendo” aparece enquanto a bola está parada e a estimativa se refina. Unidade: graus por segundo (°/s).">Bias do giroscópio</dt><dd id="d-bias">–</dd><span></span>
      <dt data-info="Se há uma calibração hard/soft-iron gravada na bola. Sem ela, o modo Quat9 pode errar o rumo em dezenas de graus. Sem unidade.">Calibração magnética</dt><dd id="d-magcal">–</dd><span></span>
      <dt data-info="Intervalo de conexão BLE concedido por este navegador. O firmware pede 7,5–15 ms; o navegador decide. É o teto de qualquer taxa de notificação: notificações por segundo ≤ (pacotes por evento de conexão) ÷ intervalo. Com 15 ms e 3 pacotes por evento o teto é 200/s. Unidade: milissegundos (ms).">Intervalo de conexão</dt><dd id="d-itvl">–</dd><span></span>
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
      const state: string[] = [];
      if (diag.startup) state.push('<span class="badge warn">partida</span>');
      if (diag.ahrsFlags & AHRS_FLAG_OVERRANGE) state.push('<span class="badge warn">saturação</span>');
      if (diag.accelIgnored) state.push('<span class="badge warn">accel ignorado</span>');
      if (diag.magIgnored) state.push('<span class="badge warn">mag ignorado</span>');
      if (diag.magCapturing) state.push('<span class="badge">capturando mag</span>');
      $('d-ahrs').innerHTML = state.length ? state.join(' ') : '<span class="ok">estável</span>';
      $('d-err').textContent = `${diag.accelErrDeg}° / ${diag.magErrDeg}°`;
      const bias = diag.gyroBiasDps.map((v) => v.toFixed(2)).join(' / ');
      $('d-bias').innerHTML = `${bias} °/s` +
        (diag.biasAdapting ? ' <span class="badge">aprendendo</span>'
          : diag.biasAtRest ? ' <span class="badge">em repouso</span>' : '');
      $('d-magcal').innerHTML = diag.magCalibrated
        ? '<span class="ok">calibrado</span>'
        : '<span class="badge warn">não calibrado</span>';
      // The granted interval sets the notify ceiling; show the headroom next to it so
      // a disappointing rate has a visible cause instead of looking like a firmware bug.
      if (diag.connItvlMs > 0) {
        const ceiling = Math.round((3 / diag.connItvlMs) * 1000);  // ~3 packets per event
        $('d-itvl').innerHTML = `${diag.connItvlMs.toFixed(1)} ms` +
          ` <span class="badge${diag.connItvlMs <= 15 ? '' : ' warn'}">teto ≈ ${ceiling} notif/s</span>`;
      } else {
        $('d-itvl').textContent = '–';
      }
    }
    if (co2) {
      $('d-co2').innerHTML = `${co2.ppm} ppm` +
        (co2.stale ? ' <span class="badge warn">desatualizado</span>' : '') +
        (co2.virtualActive ? ' <span class="badge">virtual</span>' : '');
    }
  }, UPDATE_MS);
}
