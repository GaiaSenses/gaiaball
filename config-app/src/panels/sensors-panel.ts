// Live per-sensor readouts + stacked time-series charts (Plotly), all subplots
// sharing one X axis (time in seconds since the panel started).
//
// Units (sources documented in firmware/docs/protocol.md and hardware-pinout.md):
//  - Orientation: degrees, legacy Euler convention (same output the site feeds to Pd).
//  - Acceleration: g — DMP full scale is ±4 g, 8192 LSB/g.
//  - Microphones (INMP441): dBFS per AES17 (0 dBFS = full-scale sine, FS = 2^23 for
//    the 32-bit I2S config with >>8). Estimated dB SPL = dBFS + 120, from the
//    INMP441 sensitivity spec (-26 dBFS @ 94 dB SPL, 1 kHz).
//  - CO2 (Senseair S8): ppm.
//  - Potentiometer: % of full travel (raw 0..4095).
import Plotly from 'plotly.js-dist-min';

import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import { quatToEulerLegacy } from '../protocol/euler.ts';
import {
  ACCEL_LSB_PER_G,
  rmsToDbfs,
  type Co2Sample,
  type ImuSample,
  type SensorsSample,
} from '../protocol/packets.ts';

const TICK_MS = 100; // chart sampling period (10 Hz)
const WINDOW_S = 120; // visible history
const MAX_POINTS = Math.ceil((WINDOW_S * 1000) / TICK_MS);
/** INMP441 sensitivity: -26 dBFS @ 94 dB SPL -> SPL ~= dBFS + 120 (1 kHz, estimate). */
const DBFS_TO_SPL = 120;

// One color family per chart (each chart visually distinct).
const C = {
  euler: ['#60a5fa', '#2563eb', '#bfdbfe'], // roll, pitch, yaw — blues
  acc: ['#fb923c', '#ea580c', '#fed7aa'], // X, Y, Z — oranges
  micL: '#4ade80', // green
  micR: '#2dd4bf', // teal
  co2: '#c084fc', // purple
  pot: '#f472b6', // pink
  threshold: '#9ca3af', // dashed guide on both mic charts
};

interface Latest {
  imu: ImuSample | null;
  sensors: SensorsSample | null;
  co2: Co2Sample | null;
}

const fmt = (v: number, digits = 1): string =>
  v.toLocaleString('pt-BR', { minimumFractionDigits: digits, maximumFractionDigits: digits });

export function mountSensorsPanel(root: HTMLElement, client: GaiaballClient): void {
  root.innerHTML = `
    <h2>Sensores em tempo real</h2>
    <div class="sensor-cards">
      <div class="sensor-card" data-el="card-ori"><h3>Orientação (IMU)</h3><p data-el="v-euler">—</p><small>graus (°) · convenção legada roll/pitch/yaw</small></div>
      <div class="sensor-card" data-el="card-acc"><h3>Aceleração (IMU)</h3><p data-el="v-acc">—</p><small>g · fundo de escala ±4 g (8192 LSB/g)</small></div>
      <div class="sensor-card" data-el="card-micl"><h3>Microfone esquerdo</h3><p data-el="v-micl">—</p><small>dBFS (AES17) · dB SPL estimado (INMP441: −26 dBFS @ 94 dB SPL)</small></div>
      <div class="sensor-card" data-el="card-micr"><h3>Microfone direito</h3><p data-el="v-micr">—</p><small>dBFS (AES17) · dB SPL estimado</small></div>
      <div class="sensor-card" data-el="card-co2"><h3>CO₂ (Senseair S8)</h3><p data-el="v-co2">—</p><small>ppm · partes por milhão</small></div>
      <div class="sensor-card" data-el="card-pot"><h3>Potenciômetro</h3><p data-el="v-pot">—</p><small>% do curso (ADC 0–4095) · limiar em contagens RMS</small></div>
    </div>
    <div data-el="chart" class="sensor-chart" role="img" aria-label="Gráficos de linha dos sensores no tempo"></div>
    <p class="msg">Janela de ${WINDOW_S} s, amostrada a ${1000 / TICK_MS} Hz. Todos os gráficos compartilham o eixo X (tempo, s). Arraste para dar zoom; duplo clique restaura.</p>`;

  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const chartEl = q<HTMLDivElement>('[data-el=chart]');

  const latest: Latest = { imu: null, sensors: null, co2: null };
  const t0 = performance.now();

  // --- chart ----------------------------------------------------------------
  const line = (name: string, color: string, axis: number, dash?: string) => ({
    name,
    x: [] as number[],
    y: [] as (number | null)[],
    mode: 'lines',
    line: { color, width: 1.5, ...(dash ? { dash } : {}) },
    xaxis: axis === 1 ? 'x' : `x${axis}`,
    yaxis: axis === 1 ? 'y' : `y${axis}`,
    hovertemplate: '%{y:.2f}<extra>' + name + '</extra>',
  });

  const traces = [
    line('roll (°)', C.euler[0], 1),
    line('pitch (°)', C.euler[1], 1),
    line('yaw (°)', C.euler[2], 1),
    line('acc X (g)', C.acc[0], 2),
    line('acc Y (g)', C.acc[1], 2),
    line('acc Z (g)', C.acc[2], 2),
    line('mic E (dBFS)', C.micL, 3),
    line('limiar (dBFS)', C.threshold, 3, 'dot'),
    line('mic D (dBFS)', C.micR, 4),
    line('limiar (dBFS)', C.threshold, 4, 'dot'),
    line('CO₂ (ppm)', C.co2, 5),
    line('pot (%)', C.pot, 6),
  ];

  const N_ROWS = 6;
  const axisStyle = { gridcolor: '#2a2e35', zerolinecolor: '#2a2e35', color: '#aab2bf' };
  const layout: Record<string, unknown> = {
    height: 900,
    margin: { l: 56, r: 8, t: 8, b: 40 },
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: '#aab2bf', size: 11 },
    showlegend: false,
    grid: { rows: N_ROWS, columns: 1, pattern: 'independent', ygap: 0.08 },
  };
  const rowTitles = ['Orientação (°)', 'Aceleração (g)', 'Mic esquerdo (dBFS)', 'Mic direito (dBFS)', 'CO₂ (ppm)', 'Potenciômetro (%)'];
  for (let i = 1; i <= N_ROWS; i++) {
    const xk = i === 1 ? 'xaxis' : `xaxis${i}`;
    const yk = i === 1 ? 'yaxis' : `yaxis${i}`;
    layout[xk] = {
      ...axisStyle,
      // Every subplot follows the bottom axis: one shared X (time).
      ...(i === N_ROWS ? { title: { text: 'tempo (s)' } } : { matches: `x${N_ROWS}`, showticklabels: false }),
    };
    layout[yk] = { ...axisStyle, title: { text: rowTitles[i - 1], font: { size: 11 } } };
  }
  void Plotly.newPlot(chartEl, traces as unknown[], layout, {
    responsive: true,
    displaylogo: false,
    modeBarButtonsToRemove: ['lasso2d', 'select2d'],
  });

  // --- live readouts (updated per packet) ------------------------------------
  client.addEventListener('imu', (ev) => {
    latest.imu = (ev as CustomEvent<ImuSample>).detail;
  });
  client.addEventListener('sensors', (ev) => {
    latest.sensors = (ev as CustomEvent<SensorsSample>).detail;
  });
  client.addEventListener('co2', (ev) => {
    latest.co2 = (ev as CustomEvent<Co2Sample>).detail;
  });

  const renderCards = (): void => {
    const { imu, sensors, co2 } = latest;
    if (imu) {
      const e = quatToEulerLegacy(imu.quat);
      q('[data-el=v-euler]').textContent = imu.quatValid
        ? `roll ${fmt(e.rollDeg)}° · pitch ${fmt(e.pitchDeg)}° · yaw ${fmt(e.yawDeg)}°`
        : 'quaternion inválido';
      q('[data-el=v-acc]').textContent = `X ${fmt(imu.acc[0] / ACCEL_LSB_PER_G, 2)} g · Y ${fmt(
        imu.acc[1] / ACCEL_LSB_PER_G, 2)} g · Z ${fmt(imu.acc[2] / ACCEL_LSB_PER_G, 2)} g`;
    }
    if (sensors) {
      const dbL = rmsToDbfs(sensors.rmsL);
      const dbR = rmsToDbfs(sensors.rmsR);
      const audioNote = !sensors.audioOk ? ' · falha de I²S' : !sensors.audioEnabled ? ' · desativado' : '';
      q('[data-el=v-micl]').textContent = `${fmt(dbL)} dBFS ≈ ${fmt(dbL + DBFS_TO_SPL, 0)} dB SPL${audioNote}`;
      q('[data-el=v-micr]').textContent = `${fmt(dbR)} dBFS ≈ ${fmt(dbR + DBFS_TO_SPL, 0)} dB SPL${audioNote}`;
      const potPct = (sensors.potRaw / 4095) * 100;
      const thrSrc = sensors.potControls ? 'potenciômetro' : 'fixo';
      q('[data-el=v-pot]').textContent = `${fmt(potPct)} % (bruto ${sensors.potRaw}) · limiar ${sensors.threshold.toLocaleString('pt-BR')} (${thrSrc})`;
    }
    if (co2) {
      const estado = co2.virtualActive ? 'virtual' : co2.stale ? 'sem leitura (stale)' : co2.valid ? 'real' : 'inválido';
      q('[data-el=v-co2]').textContent = `${co2.ppm} ppm · ${estado}`;
    }
  };

  // --- shared-X sampler: one tick appends one point to every trace ------------
  const tick = (): void => {
    renderCards();
    const { imu, sensors, co2 } = latest;
    if (!imu && !sensors && !co2) return; // nothing received yet
    const t = (performance.now() - t0) / 1000;
    const e = imu && imu.quatValid ? quatToEulerLegacy(imu.quat) : null;
    const thrDb = sensors ? rmsToDbfs(sensors.threshold) : null;
    const audioLive = sensors !== null && sensors.audioOk && sensors.audioEnabled;
    const ys: (number | null)[] = [
      e ? e.rollDeg : null,
      e ? e.pitchDeg : null,
      e ? e.yawDeg : null,
      imu ? imu.acc[0] / ACCEL_LSB_PER_G : null,
      imu ? imu.acc[1] / ACCEL_LSB_PER_G : null,
      imu ? imu.acc[2] / ACCEL_LSB_PER_G : null,
      audioLive ? rmsToDbfs(sensors.rmsL) : null,
      thrDb,
      audioLive ? rmsToDbfs(sensors.rmsR) : null,
      thrDb,
      co2 ? co2.ppm : null,
      sensors ? (sensors.potRaw / 4095) * 100 : null,
    ];
    void Plotly.extendTraces(
      chartEl,
      { x: ys.map(() => [t]), y: ys.map((v) => [v]) },
      ys.map((_, i) => i),
      MAX_POINTS,
    );
  };

  const timer = setInterval(() => {
    if (!document.hidden) tick();
  }, TICK_MS);
  window.addEventListener('pagehide', () => {
    clearInterval(timer);
    Plotly.purge(chartEl);
  });
}
