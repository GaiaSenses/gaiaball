// Live per-sensor readouts + stacked time-series charts (Plotly), all subplots
// sharing one X axis (time in seconds since the panel started).
//
// Units (sources documented in firmware/docs/protocol.md and hardware-pinout.md):
//  - Orientation: degrees, legacy Euler convention (same output the site feeds to Pd).
//  - Acceleration: mg (converted by the firmware) — MPU-9250 full scale is ±4 g = ±4000 mg.
//  - Microphones (INMP441): dBFS per AES17 (0 dBFS = full-scale sine, FS = 2^23 for
//    the 32-bit I2S config with >>8). Estimated dB SPL = dBFS + 120, from the
//    INMP441 sensitivity spec (-26 dBFS @ 94 dB SPL, 1 kHz).
//  - CO2 (SC8-CO2-5K): ppm.
//  - Potentiometer: % of full travel (raw 0..4095).
import Plotly from 'plotly.js-dist-min';

import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import { computeSpectrum } from '../metrics/fft.ts';
import { onThemeChange, resolvedTheme } from '../theme.ts';
import { quatToEulerLegacy } from '../protocol/euler.ts';
import {
  rmsToDbfs,
  thresholdToDbfs,
  type AudioChunkSample,
  type Co2Sample,
  type ImuSample,
  type SensorsSample,
} from '../protocol/packets.ts';
import { TLV } from '../protocol/tlv.ts';
import {
  SampleQueue,
  WINDOW_OPTIONS_S,
  Y_RANGES,
  co2Range,
  loadWindowS,
  saveWindowS,
  thresholdRows,
  type WindowS,
} from './chart-model.ts';

const TICK_MS = 100; // chart sampling period (10 Hz)
const FFT_SIZE = 512; // 256 ms window at 2 kHz -> 3.9 Hz bins
const FFT_MS = 250; // spectrum refresh period
/** Largest window (12 s) at the highest notify rate (200 Hz). */
const MAX_POINTS = 12 * 200;
/** INMP441 sensitivity: -26 dBFS @ 94 dB SPL -> SPL ~= dBFS + 120 (1 kHz, estimate). */
const DBFS_TO_SPL = 120;

// One color family per chart (each chart visually distinct), picked per theme so
// every line keeps contrast on both backgrounds.
interface ChartPalette {
  euler: [string, string, string]; // roll, pitch, yaw — blues
  acc: [string, string, string]; // X, Y, Z — oranges
  micL: string;
  micR: string;
  co2: string;
  pot: string;
  threshold: string; // solid guide on the active metric's rows
  breath: string;
  peak: string;
  rms: string;
  spectrum: string;
  grid: string;
  font: string;
}
const PALETTES: Record<'dark' | 'light', ChartPalette> = {
  dark: {
    euler: ['#60a5fa', '#2563eb', '#bfdbfe'],
    acc: ['#fb923c', '#ea580c', '#fed7aa'],
    micL: '#4ade80', micR: '#2dd4bf', co2: '#c084fc', pot: '#f472b6', threshold: '#9ca3af', breath: '#38bdf8',
    peak: '#f87171', rms: '#fbbf24', spectrum: '#eac23d', grid: '#27333f', font: '#8da0ab',
  },
  light: {
    euler: ['#2563eb', '#1e3a8a', '#60a5fa'],
    acc: ['#ea580c', '#9a3412', '#fb923c'],
    micL: '#15803d', micR: '#0f766e', co2: '#7c3aed', pot: '#be185d', threshold: '#6b7280', breath: '#0369a1',
    peak: '#dc2626', rms: '#b45309', spectrum: '#9a6a10', grid: '#d3dcd8', font: '#5a6c74',
  },
};
const pal = (): ChartPalette => PALETTES[resolvedTheme()];
/** Line colors in trace order (must match the `traces` list below). */
const traceColors = (p: ChartPalette): string[] => [
  p.euler[0], p.euler[1], p.euler[2],
  p.acc[0], p.acc[1], p.acc[2],
  p.micL, p.threshold, p.micR, p.threshold,
  p.peak, p.rms, p.threshold, p.breath, p.co2, p.pot,
];

interface Latest {
  imu: ImuSample | null;
  sensors: SensorsSample | null;
  co2: Co2Sample | null;
}

function safeStorage(): Storage | null {
  try { return window.localStorage; } catch { return null; }
}

const fmt = (v: number, digits = 1): string =>
  v.toLocaleString('pt-BR', { minimumFractionDigits: digits, maximumFractionDigits: digits });

export function mountSensorsPanel(root: HTMLElement, client: GaiaballClient): void {
  root.innerHTML = `
    <h2>Sensores em tempo real</h2>
    <div class="sensor-cards">
      <div class="sensor-card" data-el="card-ori"><h3>Orientação (IMU)</h3><p data-el="v-euler">—</p><small>graus (°) · convenção legada roll/pitch/yaw</small></div>
      <div class="sensor-card" data-el="card-acc"><h3>Aceleração (IMU)</h3><p data-el="v-acc">—</p><small>mg · fundo de escala ±4000 mg</small></div>
      <div class="sensor-card" data-el="card-micl"><h3>Microfone esquerdo</h3><p data-el="v-micl">—</p><small>dBFS (AES17) · dB SPL estimado (INMP441: −26 dBFS @ 94 dB SPL)</small></div>
      <div class="sensor-card" data-el="card-micr"><h3>Microfone direito</h3><p data-el="v-micr">—</p><small>dBFS (AES17) · dB SPL estimado</small></div>
      <div class="sensor-card" data-el="card-co2"><h3>CO₂ (SC8)</h3><p data-el="v-co2">—</p><small>ppm · partes por milhão</small></div>
      <div class="sensor-card" data-el="card-pot"><h3>Potenciômetro</h3><p data-el="v-pot">—</p><small>% do curso (ADC 0–4095) · limiar de sopro em dBFS</small></div>
    </div>
    <h3 class="spec-title">Espectro do áudio (cru) — ajuste do passa-banda</h3>
    <p class="msg">FFT de 512 pontos calculado aqui no navegador sobre o fluxo bruto de 2 kHz vindo da bola (janela de 256 ms, bins de ~3,9 Hz). Sopre na bola e observe quais frequências sobem; as linhas tracejadas marcam os cortes HP/LP configurados.</p>
    <div data-el="spectrum" class="sensor-chart sensor-spectrum" role="img" aria-label="Espectro de frequências do áudio"></div>
    <div class="chart-window" role="group" aria-label="Janela de tempo dos gráficos">
      <span>Últimos</span>
      ${WINDOW_OPTIONS_S.map((w) => `<button type="button" class="chart-window-btn" data-win="${w}" aria-pressed="false">${w} s</button>`).join('')}
    </div>
    <div data-el="chart" class="sensor-chart" role="img" aria-label="Gráficos de linha dos sensores no tempo"></div>
    <p class="msg" data-el="chart-foot"></p>`;

  const q = <T extends HTMLElement>(sel: string): T => root.querySelector<T>(sel)!;
  const chartEl = q<HTMLDivElement>('[data-el=chart]');

  const latest: Latest = { imu: null, sensors: null, co2: null };
  const t0 = performance.now();
  const now = (): number => (performance.now() - t0) / 1000;
  // Every packet is queued with its arrival time and plotted on the next tick.
  const imuQ = new SampleQueue<ImuSample>();
  const sensQ = new SampleQueue<SensorsSample>();
  const co2Q = new SampleQueue<Co2Sample>();
  let windowS: WindowS = loadWindowS(safeStorage());
  const footEl = q<HTMLElement>('[data-el=chart-foot]');
  const syncWindowUi = (): void => {
    footEl.textContent = `Últimos ${windowS} s, com todos os pacotes recebidos. Os gráficos são redesenhados 10 vezes por segundo; isso não atrasa o envio ao Pd nem o site.`;
    root.querySelectorAll<HTMLButtonElement>('.chart-window-btn').forEach((b) =>
      b.setAttribute('aria-pressed', String(Number(b.dataset.win) === windowS)));
  };
  syncWindowUi();
  root.querySelectorAll<HTMLButtonElement>('.chart-window-btn').forEach((b) =>
    b.addEventListener('click', () => {
      windowS = Number(b.dataset.win) as WindowS;
      saveWindowS(safeStorage(), windowS);
      syncWindowUi();
    }));

  // --- spectrum (frontend FFT over the raw 2 kHz stream) ----------------------
  const specEl = q<HTMLDivElement>('[data-el=spectrum]');
  const audioRing: number[] = [];
  let lastChunk: AudioChunkSample | null = null;
  let lastChunkAt = 0;
  let specIdle: boolean | null = null;
  let specShapesKey = '';
  const p0 = pal();
  client.addEventListener('audio', (ev) => {
    const c = (ev as CustomEvent<AudioChunkSample>).detail;
    lastChunk = c;
    lastChunkAt = performance.now();
    for (const v of c.samples) audioRing.push(v);
    if (audioRing.length > FFT_SIZE * 2) audioRing.splice(0, audioRing.length - FFT_SIZE * 2);
  });
  void Plotly.newPlot(
    specEl,
    [{
      name: 'espectro (dBFS)',
      x: [] as number[],
      y: [] as number[],
      mode: 'lines',
      line: { color: p0.spectrum, width: 1.5 },
      hovertemplate: '%{x:.0f} Hz · %{y:.1f} dBFS<extra></extra>',
    }],
    {
      height: 260,
      margin: { l: 56, r: 8, t: 8, b: 40 },
      paper_bgcolor: 'rgba(0,0,0,0)',
      plot_bgcolor: 'rgba(0,0,0,0)',
      font: { color: p0.font, size: 11 },
      showlegend: false,
      xaxis: { gridcolor: p0.grid, zerolinecolor: p0.grid, color: p0.font, title: { text: 'frequência (Hz)' }, range: [0, 1000] },
      yaxis: { gridcolor: p0.grid, zerolinecolor: p0.grid, color: p0.font, title: { text: 'nível (dBFS)' }, range: [-120, 0] },
    },
    { responsive: true, displaylogo: false },
  );
  const refreshSpectrum = (): void => {
    // The raw stream is opt-in (TLV 0x1F) because it costs ~45 ms/s of BLE air time.
    // When it is off there is simply nothing to transform, so say so on the chart
    // instead of leaving a dead plot: the RMS and peak readouts keep working, they
    // come measured from the firmware on every orientation packet.
    const idle = performance.now() - lastChunkAt > 2000;
    if (idle !== specIdle) {
      specIdle = idle;
      specShapesKey = '';  // force the HP/LP markers to redraw when it comes back
      if (idle) {
        audioRing.length = 0;  // never splice pre-pause samples into a fresh FFT
        void Plotly.restyle(specEl, { x: [[] as number[]], y: [[] as number[]] }, [0]);
        void Plotly.relayout(specEl, {
          shapes: [],
          annotations: [{
            xref: 'paper', yref: 'paper', x: 0.5, y: 0.5, showarrow: false, align: 'center',
            text: 'Stream de áudio cru desligado.<br>Ligue em Configuração › Avançado › Áudio'
              + ' para ver o espectro.',
            font: { size: 12, color: pal().font },
          }],
        });
      }
    }
    if (idle || !lastChunk || audioRing.length < FFT_SIZE) return;
    const sp = computeSpectrum(audioRing.slice(-FFT_SIZE), FFT_SIZE, lastChunk.sampleRateHz || 2000);
    void Plotly.restyle(specEl, { x: [Array.from(sp.freqsHz)], y: [Array.from(sp.dbfs)] }, [0]);
    // Dashed cutoff markers follow the configured band (echoed in every chunk).
    const key = `${lastChunk.hpHz}|${lastChunk.lpHz}|${lastChunk.bpfEnabled}`;
    if (key !== specShapesKey) {
      specShapesKey = key;
      const mark = (hz: number) => ({
        type: 'line', x0: hz, x1: hz, y0: -120, y1: 0,
        line: { color: pal().threshold, width: 1, dash: 'dot' },
      });
      void Plotly.relayout(specEl, {
        shapes: lastChunk.bpfEnabled ? [mark(lastChunk.hpHz), mark(lastChunk.lpHz)] : [],
        annotations: lastChunk.bpfEnabled
          ? [
              { x: lastChunk.hpHz, y: -4, text: `HP ${lastChunk.hpHz} Hz`, showarrow: false, font: { size: 10, color: pal().font }, xanchor: 'left' },
              { x: lastChunk.lpHz, y: -12, text: `LP ${lastChunk.lpHz} Hz`, showarrow: false, font: { size: 10, color: pal().font }, xanchor: 'left' },
            ]
          : [],
      });
    }
  };
  const specTimer = setInterval(() => {
    if (!document.hidden) refreshSpectrum();
  }, FFT_MS);

  // --- chart ----------------------------------------------------------------
  const line = (name: string, color: string, axis: number, dash?: string, shape?: string) => ({
    name,
    x: [] as number[],
    y: [] as (number | null)[],
    mode: 'lines',
    line: { color, width: 1.5, ...(dash ? { dash } : {}), ...(shape ? { shape } : {}) },
    xaxis: axis === 1 ? 'x' : `x${axis}`,
    yaxis: axis === 1 ? 'y' : `y${axis}`,
    hovertemplate: '%{y:.2f}<extra>' + name + '</extra>',
  });

  const traces = [
    line('roll (°)', p0.euler[0], 1),
    line('pitch (°)', p0.euler[1], 1),
    line('yaw (°)', p0.euler[2], 1),
    line('acc X (mg)', p0.acc[0], 2),
    line('acc Y (mg)', p0.acc[1], 2),
    line('acc Z (mg)', p0.acc[2], 2),
    line('mic E (dBFS)', p0.micL, 3),
    line('limiar (dBFS)', p0.threshold, 3),
    line('mic D (dBFS)', p0.micR, 4),
    line('limiar (dBFS)', p0.threshold, 4),
    line('pico E (dBFS)', p0.peak, 5),
    line('pico D (dBFS)', p0.rms, 5),
    line('limiar (dBFS)', p0.threshold, 5),
    { ...line('sopro (0/1)', p0.breath, 6, undefined, 'hv'), fill: 'tozeroy', fillcolor: p0.breath + '33' },
    line('CO₂ (ppm)', p0.co2, 7),
    line('pot (%)', p0.pot, 8),
  ];

  const N_ROWS = 8;
  const yRanges: (readonly [number, number])[] = [
    Y_RANGES.orientation, Y_RANGES.accel, Y_RANGES.micRms, Y_RANGES.micRms, Y_RANGES.micPeak,
    Y_RANGES.breath, co2Range(0), Y_RANGES.pot,
  ];
  const axisStyle = { gridcolor: p0.grid, zerolinecolor: p0.grid, color: p0.font };
  const layout: Record<string, unknown> = {
    height: 1190,
    margin: { l: 56, r: 8, t: 8, b: 40 },
    paper_bgcolor: 'rgba(0,0,0,0)',
    plot_bgcolor: 'rgba(0,0,0,0)',
    font: { color: p0.font, size: 11 },
    showlegend: false,
    grid: { rows: N_ROWS, columns: 1, pattern: 'independent', ygap: 0.08 },
  };
  const rowTitles = ['Orientação (°)', 'Aceleração (mg)', 'Mic esquerdo (dBFS)', 'Mic direito (dBFS)', 'Pico por canal (dBFS)', 'Detecção de sopro', 'CO₂ (ppm)', 'Potenciômetro (%)'];
  for (let i = 1; i <= N_ROWS; i++) {
    const xk = i === 1 ? 'xaxis' : `xaxis${i}`;
    const yk = i === 1 ? 'yaxis' : `yaxis${i}`;
    layout[xk] = {
      ...axisStyle,
      // Every subplot follows the bottom axis: one shared X (time).
      ...(i === N_ROWS ? { title: { text: 'tempo (s)' } } : { matches: `x${N_ROWS}`, showticklabels: false }),
    };
    layout[yk] = {
      ...axisStyle,
      title: { text: rowTitles[i - 1], font: { size: 11 } },
      autorange: false,
      range: [...yRanges[i - 1]],
      ...(i === 6 ? { tickvals: [0, 1], ticktext: ['0', '1'] } : {}),
    };
  }
  (layout[`xaxis${N_ROWS}`] as Record<string, unknown>).range = [-windowS, 0];
  (layout[`xaxis${N_ROWS}`] as Record<string, unknown>).autorange = false;
  void Plotly.newPlot(chartEl, traces as unknown[], layout, {
    responsive: true,
    displaylogo: false,
    doubleClick: 'reset',
    modeBarButtonsToRemove: ['lasso2d', 'select2d'],
  });

  // Re-skin both Plotly charts when the theme flips (axes, fonts and line colors).
  const applyChartTheme = (): void => {
    const p = pal();
    const cols = traceColors(p);
    void Plotly.restyle(chartEl, { 'line.color': cols }, cols.map((_, i) => i));
    const re: Record<string, unknown> = { 'font.color': p.font };
    for (let i = 1; i <= N_ROWS; i++) {
      const sfx = i === 1 ? '' : String(i);
      for (const ax of [`xaxis${sfx}`, `yaxis${sfx}`]) {
        re[`${ax}.gridcolor`] = p.grid;
        re[`${ax}.zerolinecolor`] = p.grid;
        re[`${ax}.color`] = p.font;
      }
    }
    void Plotly.relayout(chartEl, re);
    void Plotly.restyle(specEl, { 'line.color': p.spectrum }, [0]);
    void Plotly.relayout(specEl, {
      'font.color': p.font,
      'xaxis.gridcolor': p.grid, 'xaxis.zerolinecolor': p.grid, 'xaxis.color': p.font,
      'yaxis.gridcolor': p.grid, 'yaxis.zerolinecolor': p.grid, 'yaxis.color': p.font,
    });
    specShapesKey = ''; // force the HP/LP markers to redraw with the new colors
  };
  onThemeChange(applyChartTheme);

  // --- live readouts (updated per packet) ------------------------------------
  client.addEventListener('imu', (ev) => {
    latest.imu = (ev as CustomEvent<ImuSample>).detail;
    imuQ.push(now(), latest.imu);
  });
  client.addEventListener('sensors', (ev) => {
    latest.sensors = (ev as CustomEvent<SensorsSample>).detail;
    sensQ.push(now(), latest.sensors);
  });
  client.addEventListener('co2', (ev) => {
    latest.co2 = (ev as CustomEvent<Co2Sample>).detail;
    co2Q.push(now(), latest.co2);
  });
  // CO2 axis follows the configured virtual peak (0x23, u16 little-endian).
  client.addEventListener('config', (ev) => {
    const v = (ev as CustomEvent<Map<number, DataView>>).detail.get(TLV.virtualPeakPpm);
    if (!v || v.byteLength !== 2) return;
    void Plotly.relayout(chartEl, { 'yaxis7.range': co2Range(v.getUint16(0, true)) });
  });

  const renderCards = (): void => {
    const { imu, sensors, co2 } = latest;
    if (imu) {
      const e = quatToEulerLegacy(imu.quat);
      q('[data-el=v-euler]').textContent = imu.quatValid
        ? `roll ${fmt(e.rollDeg)}° · pitch ${fmt(e.pitchDeg)}° · yaw ${fmt(e.yawDeg)}°`
        : 'quaternion inválido';
      q('[data-el=v-acc]').textContent = `X ${fmt(imu.acc[0], 0)} mg · Y ${fmt(
        imu.acc[1], 0)} mg · Z ${fmt(imu.acc[2], 0)} mg`;
    }
    if (sensors) {
      // RMS and peak come from the orientation packet (firmware, 20 ms window at the
      // full 16 kHz) when they are live; the 5 Hz sensors packet is the fallback and
      // still carries the threshold, the pot and the health flags.
      const live = imu !== null && imu.audioValid;
      const dbL = live ? imu.audioRmsDbfs[0] : rmsToDbfs(sensors.rmsL);
      const dbR = live ? imu.audioRmsDbfs[1] : rmsToDbfs(sensors.rmsR);
      const audioNote =
        (!sensors.audioOk ? ' · falha de I²S' : !sensors.audioEnabled ? ' · detector desativado' : '') +
        (sensors.audioFiltered ? ' · filtrado (HP/LP)' : ' · cru');
      const peakNote = (ch: 0 | 1) =>
        live ? ` · pico ${fmt(imu.audioPeakDbfs[ch])} dBFS` : '';
      q('[data-el=v-micl]').textContent =
        `${fmt(dbL)} dBFS ≈ ${fmt(dbL + DBFS_TO_SPL, 0)} dB SPL${peakNote(0)}${audioNote}`;
      q('[data-el=v-micr]').textContent =
        `${fmt(dbR)} dBFS ≈ ${fmt(dbR + DBFS_TO_SPL, 0)} dB SPL${peakNote(1)}${audioNote}`;
      const potPct = (sensors.potRaw / 4095) * 100;
      const thrSrc = sensors.potControls ? 'potenciômetro' : 'fixo';
      const metric = sensors.metricPeak ? 'pico' : 'RMS';
      q('[data-el=v-pot]').textContent = `${fmt(potPct)} % (bruto ${sensors.potRaw}) · limiar ${fmt(thresholdToDbfs(sensors))} dBFS (${metric}, ${thrSrc})`;
    }
    if (co2) {
      const estado = co2.virtualActive ? 'virtual' : co2.stale ? 'sem leitura (stale)' : co2.valid ? 'real' : 'inválido';
      q('[data-el=v-co2]').textContent = `${co2.ppm} ppm · ${estado}`;
    }
  };

  // --- sampler: one tick plots every packet queued since the previous one ------
  const NT = 16; // trace count (see `traces`)
  const tick = (): void => {
    renderCards();
    const imus = imuQ.drain();
    const senss = sensQ.drain();
    const co2s = co2Q.drain();
    const t = now();
    const xs: number[][] = Array.from({ length: NT }, () => []);
    const ys: (number | null)[][] = Array.from({ length: NT }, () => []);
    const add = (tr: number, x: number, y: number | null): void => { xs[tr].push(x); ys[tr].push(y); };
    // The envelope is measured in the firmware: RMS over a 20 ms window and peak at the
    // full 16 kHz; when it is not live the point is a gap.
    for (const { t: x, v: imu } of imus) {
      const e = imu.quatValid ? quatToEulerLegacy(imu.quat) : null;
      add(0, x, e ? e.rollDeg : null);
      add(1, x, e ? e.pitchDeg : null);
      add(2, x, e ? e.yawDeg : null);
      add(3, x, imu.acc[0]);
      add(4, x, imu.acc[1]);
      add(5, x, imu.acc[2]);
      const live = imu.audioValid;
      add(6, x, live ? imu.audioRmsDbfs[0] : null);
      add(8, x, live ? imu.audioRmsDbfs[1] : null);
      add(10, x, live ? imu.audioPeakDbfs[0] : null);
      add(11, x, live ? imu.audioPeakDbfs[1] : null);
      add(13, x, imu.breath ? 1 : 0);
    }
    for (const { t: x, v: s } of senss) {
      const thr = thresholdToDbfs(s);
      const rows = thresholdRows(s.metricPeak);
      add(7, x, rows.rms ? thr : null);
      add(9, x, rows.rms ? thr : null);
      add(12, x, rows.peak ? thr : null);
      add(15, x, (s.potRaw / 4095) * 100);
    }
    for (const { t: x, v: c } of co2s) add(14, x, c.ppm);
    if (!xs.some((a) => a.length > 0)) return; // nothing received since the last tick
    // Every x axis follows the bottom one: set its range on the layout object first so the
    // single redraw done by extendTraces already shows [t-N, t].
    const lay = (chartEl as unknown as { layout?: Record<string, { range?: number[]; autorange?: boolean }> }).layout;
    const xa = lay?.[`xaxis${N_ROWS}`];
    if (xa) { xa.range = [t - windowS, t]; xa.autorange = false; }
    void Plotly.extendTraces(chartEl, { x: xs, y: ys }, xs.map((_, i) => i), MAX_POINTS);
  };

  const timer = setInterval(() => {
    if (document.hidden) { imuQ.clear(); sensQ.clear(); co2Q.clear(); return; }
    tick();
  }, TICK_MS);
  window.addEventListener('pagehide', () => {
    clearInterval(timer);
    clearInterval(specTimer);
    Plotly.purge(chartEl);
    Plotly.purge(specEl);
  });
}
