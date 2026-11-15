// Live per-sensor readouts + stacked time-series charts (Plotly), all subplots
// sharing one X axis (time in seconds since the panel started).
//
// Units (sources documented in firmware/docs/protocol.md and hardware-pinout.md):
//  - Orientation: degrees, legacy Euler convention (same output the site feeds to Pd).
//  - Acceleration: mg (converted by the firmware) — MPU-9250 full scale is ±4 g = ±4000 mg.
//  - Microphones (INMP441): dBFS per AES17 (0 dBFS = full-scale sine, FS = 2^23 for
//    the 32-bit I2S config with >>8), shown in whole dB.
//  - CO2 (SC8-CO2-5K): ppm.
//  - Potentiometer: % of full travel (raw 0..4095).
import Plotly from 'plotly.js-dist-min';

import type { GaiaballClient } from '../ble/gaiaball-client.ts';
import { computeSpectrum } from '../metrics/fft.ts';
import { onThemeChange, resolvedTheme } from '../theme.ts';
import { quatToEulerLegacy } from '../protocol/euler.ts';
import {
  potRawToDbfs,
  rmsToDbfs,
  thresholdToDbfs,
  type AudioChunkSample,
  type Co2Sample,
  type ImuSample,
  type SensorsSample,
} from '../protocol/packets.ts';
import { TLV } from '../protocol/tlv.ts';
import { renderSwitch } from '../ui/switch.ts';
import type { BandSource, Co2Source, FormBand } from './config-panel.ts';
import {
  HISTORY_S,
  MAX_DRAWN_POINTS,
  SampleQueue,
  WINDOW_OPTIONS_S,
  firstIndexAtOrAfter,
  formatDb,
  formatSigned,
  levelFraction,
  minMaxDecimate,
  Y_RANGES,
  co2Meter,
  co2Range,
  loadWindowS,
  saveWindowS,
  thresholdRows,
  type Co2Levels,
  type WindowS,
} from './chart-model.ts';
import { bandShading, loadSpectrumScale, saveSpectrumScale, specXRange, type SpectrumScale } from './spectrum-model.ts';

const TICK_MS = 100; // chart sampling period (10 Hz)
const FFT_SIZE = 512; // 256 ms window at 2 kHz -> 3.9 Hz bins
const FFT_MS = 250; // spectrum refresh period
// Spectrum frequency axis, up to Nyquist of the 2 kHz stream. The log scale (the default, as
// audio analysers draw it) starts at 10 Hz so the low band where breath lives is not squeezed
// into the left edge; the linear one starts at 0.
const SPEC_TICKS_HZ = [10, 20, 50, 100, 200, 500, 1000];

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
/** Whole number, never "-0". */
const fmtInt = (v: number): string => fmt(Math.round(v) + 0, 0);
/** Travel of the potentiometer in dBFS (potRawToDbfs maps 0..4095 onto -60..0). */
const POT_FLOOR_DBFS = -60;
/**
 * A card with two readings and a meter: microphones (RMS bar + peak tick, -90..0 dBFS)
 * and the potentiometer (travel, as a fader). The swatch before each label is drawn like
 * the part of the meter it names, so the bar reads as RMS and the tick as peak.
 */
const levelCard = (key: string, title: string, a: [string, string, string], b: [string, string, string],
  note: string, numCh: number, fader = false): string => `
      <div class="sensor-card level-card" data-el="card-${key}" style="--num-ch: ${numCh}">
        <div class="card-head"><h3>${title}</h3><span class="card-state" data-el="st-${key}"></span></div>
        <dl class="readings">
          <div class="reading reading-${a[0]}"><dt>${a[1]}</dt><dd><span class="reading-num" data-el="${a[0]}-${key}">—</span><span class="reading-unit">${a[2]}</span></dd></div>
          <div class="reading reading-${b[0]}"><dt>${b[1]}</dt><dd><span class="reading-num" data-el="${b[0]}-${key}">—</span><span class="reading-unit">${b[2]}</span></dd></div>
        </dl>
        <div class="meter${fader ? ' meter-fader' : ''}" aria-hidden="true"><span class="meter-fill" data-el="bar-${key}"></span>${fader ? '' : `<span class="meter-peak" data-el="tick-${key}" hidden></span>`}</div>
        <small>${note}</small>
      </div>`;
const MIC_NOTE = 'RMS é a média de 20 ms; pico é a amostra mais alta. Escala de −90 a 0 dBFS.';
/**
 * A card with three signed readings side by side (orientation, acceleration). Each value
 * sits right-aligned in a box as wide as the widest value it can take (`numCh` characters
 * of the monospace face), so the units digit stays put while the sign flips and the digit
 * count changes. The line before each label is the color of that axis in the charts.
 */
const axesCard = (key: string, title: string, axes: [string, string][], unit: { sign?: string; head?: string },
  note: string, numCh: number): string => `
      <div class="sensor-card" data-el="card-${key}" style="--num-ch: ${numCh}">
        <div class="card-head"><h3>${title}</h3><span class="card-state" data-el="st-${key}">${unit.head ?? ''}</span></div>
        <dl class="readings readings-3">
          ${axes.map(([id, label]) => `<div class="reading reading-line" data-el="rd-${id}"><dt>${label}</dt><dd><span class="reading-num" data-el="${id}">—</span>${unit.sign ? `<span class="reading-sign">${unit.sign}</span>` : ''}</dd></div>`).join('')}
        </dl>
        <small>${note}</small>
      </div>`;
/** Light grey over the parts of the spectrum the band-pass removes. */
const SHADE: Record<'dark' | 'light', string> = { dark: 'rgba(148, 163, 184, 0.16)', light: 'rgba(100, 116, 139, 0.14)' };
/** Card tint per sensor: the color of its line in the charts below. */
const cardTints = (p: ChartPalette): Record<string, string> => ({
  'card-ori': p.euler[0], 'card-acc': p.acc[0], 'card-micl': p.micL,
  'card-micr': p.micR, 'card-co2': p.co2, 'card-pot': p.pot,
});

export interface SensorsPanelOptions {
  /** Band-pass as the configuration form shows it; without it the spectrum follows the firmware's echo. */
  band?: BandSource;
  /** CO2 trigger and virtual peak as the form shows them; without it the meter uses what the ball reports. */
  co2?: Co2Source;
}

export function mountSensorsPanel(
  root: HTMLElement, client: GaiaballClient, opts: SensorsPanelOptions = {},
): { setPaused(p: boolean): void } {
  root.innerHTML = `
    <h2>Sensores em tempo real</h2>
    <div class="sensor-cards">
      ${axesCard('ori', 'Orientação (IMU)', [['roll', 'Roll'], ['pitch', 'Pitch'], ['yaw', 'Yaw']], { sign: '°' },
        'Graus, de −180 a 180, na convenção legada de roll, pitch e yaw.', 4)}
      ${axesCard('acc', 'Aceleração (IMU)', [['ax', 'X'], ['ay', 'Y'], ['az', 'Z']], { head: 'em mg' },
        '1000 mg = 1 g; o fundo de escala é ±4000 mg.', 5)}
      ${levelCard('micl', 'Microfone esquerdo', ['rms', 'RMS', 'dBFS'], ['peak', 'Pico', 'dBFS'], MIC_NOTE, 4)}
      ${levelCard('micr', 'Microfone direito', ['rms', 'RMS', 'dBFS'], ['peak', 'Pico', 'dBFS'], MIC_NOTE, 4)}
      <div class="sensor-card level-card" data-el="card-co2" style="--num-ch: 5">
        <div class="card-head"><h3>CO₂ (SC8)</h3><span class="card-state" data-el="st-co2"></span></div>
        <dl class="readings">
          <div class="reading reading-bar"><dt>Concentração</dt><dd><span class="reading-num" data-el="ppm-co2">—</span><span class="reading-unit">ppm</span></dd></div>
          <div class="reading reading-tick"><dt>Gatilho</dt><dd><span class="reading-word" data-el="trig-co2">—</span></dd></div>
        </dl>
        <div class="meter" aria-hidden="true"><span class="meter-fill" data-el="bar-co2"></span><span class="meter-peak" data-el="tick-co2" hidden></span></div>
        <small data-el="note-co2"></small>
      </div>
      ${levelCard('pot', 'Potenciômetro', ['pos', 'Posição', '%'], ['sens', 'Sensibilidade', 'dBFS'],
        'O curso inteiro vai de −60 a 0 dBFS de sensibilidade.', 3, true)}
    </div>
    <div class="spec-head">
      <h3 class="spec-title">Espectro do áudio (cru) — ajuste do passa-banda</h3>
      <span class="spec-scale" data-el="spec-scale"></span>
    </div>
    <p class="msg">FFT de 512 pontos calculado aqui no navegador sobre o fluxo bruto de 2 kHz vindo da bola (janela de 256 ms, bins de ~3,9 Hz). A frequência pode ficar em escala linear (0 a 1000 Hz) ou log (10 a 1000 Hz). Sopre na bola e observe quais frequências sobem. As linhas tracejadas marcam os cortes HP/LP como estão na Configuração, e o cinza cobre o que o passa-banda corta; mudar os cortes lá move a faixa aqui na hora, antes mesmo de aplicar.</p>
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
    footEl.textContent = `Últimos ${windowS} s, com todos os pacotes recebidos. Quando uma linha passa de ${MAX_DRAWN_POINTS} pontos, cada trecho é desenhado pelo seu mínimo e máximo, para os picos continuarem visíveis. Os gráficos são redesenhados 10 vezes por segundo; isso não atrasa o envio ao Pd nem o site.`;
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
  let scale: SpectrumScale = loadSpectrumScale(safeStorage());
  /** Plotly x-axis settings for a scale; on a log axis the range is given in log10 units. */
  const xAxisFor = (s: SpectrumScale): Record<string, unknown> => {
    const [lo, hi] = specXRange(s);
    return s === 'log'
      ? { type: 'log', range: [Math.log10(lo), Math.log10(hi)], tickmode: 'array',
          tickvals: SPEC_TICKS_HZ, ticktext: SPEC_TICKS_HZ.map(String), title: { text: 'frequência (Hz, escala log)' } }
      : { type: 'linear', range: [lo, hi], tickmode: 'auto', tickvals: null, ticktext: null,
          title: { text: 'frequência (Hz, escala linear)' } };
  };
  client.addEventListener('audio', (ev) => {
    const c = (ev as CustomEvent<AudioChunkSample>).detail;
    if (paused) return;
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
      // r: room for the "1000" label at the right end of the frequency axis.
      margin: { l: 56, r: 24, t: 8, b: 40 },
      paper_bgcolor: 'rgba(0,0,0,0)',
      plot_bgcolor: 'rgba(0,0,0,0)',
      font: { color: p0.font, size: 11 },
      showlegend: false,
      xaxis: { gridcolor: p0.grid, zerolinecolor: p0.grid, color: p0.font, ...xAxisFor(scale) },
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
    // Bin 0 (0 Hz) has no place on a log axis.
    const freqs = Array.from(sp.freqsHz).slice(1);
    const levels = Array.from(sp.dbfs).slice(1);
    void Plotly.restyle(specEl, { x: [freqs], y: [levels] }, [0]);
    drawBand();
  };
  /**
   * Grey over what the band-pass removes, plus a dashed line at each cutoff. The band is the
   * one in the Configuração form (it moves as the person edits it); the firmware echoes the
   * band it is actually using in every chunk, and a cutoff that differs from that echo is
   * labelled "não aplicado". Without the form, the echo is drawn.
   */
  let formBand: FormBand | null = null;
  const currentBand = (): { enabled: boolean; hpHz: number; lpHz: number; pending: boolean } | null => {
    const echo = lastChunk;
    if (formBand && formBand.hpHz !== null && formBand.lpHz !== null) {
      const pending = echo !== null && (echo.bpfEnabled !== formBand.enabled
        || (formBand.enabled && (echo.hpHz !== formBand.hpHz || echo.lpHz !== formBand.lpHz)));
      return { enabled: formBand.enabled, hpHz: formBand.hpHz, lpHz: formBand.lpHz, pending };
    }
    return echo ? { enabled: echo.bpfEnabled, hpHz: echo.hpHz, lpHz: echo.lpHz, pending: false } : null;
  };
  const drawBand = (): void => {
    if (specIdle !== false) return; // no spectrum on screen: the idle notice owns the chart
    const band = currentBand();
    const theme = resolvedTheme();
    const key = band ? `${scale}|${theme}|${band.enabled}|${band.hpHz}|${band.lpHz}|${band.pending}` : `${scale}|none`;
    if (key === specShapesKey) return;
    specShapesKey = key;
    const range = specXRange(scale);
    const inView = (hz: number): boolean => hz >= range[0] && hz <= range[1];
    const cuts = band && band.enabled
      ? [{ hz: band.hpHz, label: 'HP', y: -4 }, { hz: band.lpHz, label: 'LP', y: -12 }].filter((c) => inView(c.hz))
      : [];
    const note = band?.pending ? ' (não aplicado)' : '';
    void Plotly.relayout(specEl, {
      // Shapes take data values (Hz) even on a log axis; annotations take log10(Hz) there.
      shapes: [
        ...(band ? bandShading(band, range) : []).map((s) => ({
          type: 'rect', xref: 'x', yref: 'paper', x0: s.x0, x1: s.x1, y0: 0, y1: 1,
          fillcolor: SHADE[theme], line: { width: 0 }, layer: 'below',
        })),
        ...cuts.map((c) => ({
          type: 'line', x0: c.hz, x1: c.hz, y0: -120, y1: 0,
          line: { color: pal().threshold, width: 1, dash: 'dot' },
        })),
      ],
      annotations: cuts.map((c) => ({
        x: scale === 'log' ? Math.log10(c.hz) : c.hz, y: c.y, text: `${c.label} ${c.hz} Hz${note}`,
        showarrow: false, font: { size: 10, color: pal().font }, xanchor: 'left',
      })),
    });
  };
  opts.band?.subscribe((b) => { formBand = b; drawBand(); });
  // Linear / log frequency axis, remembered in this browser.
  const scaleSwitch = renderSwitch({
    options: [{ value: 0, label: 'Linear' }, { value: 1, label: 'Log' }],
    value: scale === 'log' ? 1 : 0,
    label: 'Escala da frequência',
    onChange: (v) => {
      scale = v === 1 ? 'log' : 'linear';
      saveSpectrumScale(safeStorage(), scale);
      const ax = xAxisFor(scale);
      const upd: Record<string, unknown> = {};
      for (const [k, val] of Object.entries(ax)) upd[`xaxis.${k}`] = val;
      void Plotly.relayout(specEl, upd);
      specShapesKey = '';
      drawBand();
    },
  });
  q<HTMLElement>('[data-el=spec-scale]').append(scaleSwitch);
  let paused = false;
  const specTimer = setInterval(() => {
    if (!document.hidden && !paused) refreshSpectrum();
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

  /** Each card takes the color of its own line in the charts (CSS mixes it into the background). */
  const applyCardTints = (p: ChartPalette): void => {
    for (const [el, color] of Object.entries(cardTints(p))) {
      q<HTMLElement>(`[data-el=${el}]`).style.setProperty('--card-tint', color);
    }
    // The line before each axis label: that axis's color in the charts.
    const axisColors: [string, string][] = [
      ['rd-roll', p.euler[0]], ['rd-pitch', p.euler[1]], ['rd-yaw', p.euler[2]],
      ['rd-ax', p.acc[0]], ['rd-ay', p.acc[1]], ['rd-az', p.acc[2]],
    ];
    for (const [el, color] of axisColors) q<HTMLElement>(`[data-el=${el}]`).style.setProperty('--swatch', color);
  };
  applyCardTints(pal());

  // Re-skin both Plotly charts when the theme flips (axes, fonts and line colors).
  const applyChartTheme = (): void => {
    const p = pal();
    applyCardTints(p);
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
    if (paused) return;
    latest.imu = (ev as CustomEvent<ImuSample>).detail;
    imuQ.push(now(), latest.imu);
  });
  client.addEventListener('sensors', (ev) => {
    if (paused) return;
    latest.sensors = (ev as CustomEvent<SensorsSample>).detail;
    sensQ.push(now(), latest.sensors);
  });
  client.addEventListener('co2', (ev) => {
    if (paused) return;
    latest.co2 = (ev as CustomEvent<Co2Sample>).detail;
    co2Q.push(now(), latest.co2);
  });
  // The CO2 meter shares the chart's scale (0..max(5000, virtual peak)) and marks the trigger
  // level. Both follow the Configuração form, like the spectrum's band: the mark moves while
  // the person edits the trigger, and shows in the demo too. The values read from the ball
  // fill in an empty field and tell whether the form's trigger is applied yet.
  let formCo2: Co2Levels | null = null;
  let deviceCo2: Co2Levels | null = null;
  let co2Max = co2Range(0)[1];
  const syncCo2Scale = (): void => {
    const m = co2Meter(formCo2, deviceCo2);
    if (m.max !== co2Max) {
      co2Max = m.max;
      void Plotly.relayout(chartEl, { 'yaxis7.range': [0, m.max] });
    }
    const tick = q('[data-el=tick-co2]');
    tick.hidden = m.triggerPpm === null;
    if (m.triggerPpm !== null) tick.style.left = `${Math.min(1, Math.max(0, m.triggerPpm / m.max)) * 100}%`;
    q('[data-el=note-co2]').textContent = `Escala de 0 a ${m.max} ppm. ` + (m.triggerPpm === null
      ? 'Sem gatilho: o campo “Gatilho de CO₂” da Configuração está vazio.'
      : m.pending
        ? `O traço é o gatilho da Configuração (${m.triggerPpm} ppm), ainda não aplicado à bola.`
        : `O traço é o nível do gatilho (${m.triggerPpm} ppm).`);
  };
  // Registers 0x23 (virtual peak) and 0x29 (trigger) are u16 little-endian.
  client.addEventListener('config', (ev) => {
    const cfg = (ev as CustomEvent<Map<number, DataView>>).detail;
    const u16 = (id: number): number | null => {
      const dv = cfg.get(id);
      return dv && dv.byteLength === 2 ? dv.getUint16(0, true) : null;
    };
    deviceCo2 = { triggerPpm: u16(TLV.co2TriggerPpm), virtualPeakPpm: u16(TLV.virtualPeakPpm) };
    syncCo2Scale();
  });
  client.addEventListener('status', (ev) => {
    if ((ev as CustomEvent<{ connected: boolean }>).detail.connected) return;
    deviceCo2 = null; // another ball may connect next: nothing is "applied" until it is read
    syncCo2Scale();
  });
  syncCo2Scale();
  opts.co2?.subscribe((c) => { formCo2 = c; syncCo2Scale(); });

  const renderCards = (): void => {
    const { imu, sensors, co2 } = latest;
    if (imu) {
      const e = imu.quatValid ? quatToEulerLegacy(imu.quat) : null;
      q('[data-el=roll]').textContent = e ? formatSigned(e.rollDeg) : '—';
      q('[data-el=pitch]').textContent = e ? formatSigned(e.pitchDeg) : '—';
      q('[data-el=yaw]').textContent = e ? formatSigned(e.yawDeg) : '—';
      const stOri = q('[data-el=st-ori]'); // empty while valid: the degrees are on the numbers
      stOri.textContent = e ? '' : 'quaternion inválido';
      stOri.dataset.tone = e ? '' : 'danger';
      q('[data-el=ax]').textContent = formatSigned(imu.acc[0]);
      q('[data-el=ay]').textContent = formatSigned(imu.acc[1]);
      q('[data-el=az]').textContent = formatSigned(imu.acc[2]);
    }
    if (sensors) {
      // RMS and peak come from the orientation packet (firmware, 20 ms window at the
      // full 16 kHz) when they are live; the 5 Hz sensors packet is the fallback and
      // still carries the threshold, the pot and the health flags.
      const live = imu !== null && imu.audioValid;
      const dbL = live ? imu.audioRmsDbfs[0] : rmsToDbfs(sensors.rmsL);
      const dbR = live ? imu.audioRmsDbfs[1] : rmsToDbfs(sensors.rmsR);
      // Without live levels (fallback sensors packet) there is no peak: "—" and no tick.
      const setMic = (key: 'micl' | 'micr', rms: number, peak: number | null): void => {
        q(`[data-el=rms-${key}]`).textContent = formatDb(rms);
        q(`[data-el=peak-${key}]`).textContent = peak === null ? '—' : formatDb(peak);
        q(`[data-el=bar-${key}]`).style.width = `${levelFraction(rms) * 100}%`;
        const tick = q(`[data-el=tick-${key}]`);
        tick.hidden = peak === null;
        if (peak !== null) tick.style.left = `${levelFraction(peak) * 100}%`;
      };
      setMic('micl', dbL, live ? imu.audioPeakDbfs[0] : null);
      setMic('micr', dbR, live ? imu.audioPeakDbfs[1] : null);
      const [micState, micTone] = !sensors.audioOk
        ? ['falha de I²S', 'danger']
        : sensors.audioFiltered ? ['com passa-banda', ''] : ['sem filtro', ''];
      for (const key of ['micl', 'micr']) {
        const st = q(`[data-el=st-${key}]`);
        st.textContent = micState;
        st.dataset.tone = micTone;
      }
      const potDb = potRawToDbfs(sensors.potRaw);
      q('[data-el=pos-pot]').textContent = fmtInt((sensors.potRaw / 4095) * 100);
      q('[data-el=sens-pot]').textContent = formatDb(potDb);
      q('[data-el=bar-pot]').style.width = `${levelFraction(potDb, POT_FLOOR_DBFS) * 100}%`;
      // Whether the knob matters right now: it only sets the sensitivity when it is the
      // source and the detector is on.
      q('[data-el=st-pot]').textContent = !sensors.audioEnabled
        ? 'detector desligado'
        : sensors.potControls ? 'em uso pelo detector' : 'fora de uso (valor fixo)';
    }
    if (co2) {
      // Where the number comes from: the virtual pulse wins, then a stale or invalid sensor.
      const [src, tone] = co2.virtualActive ? ['pulso virtual', 'accent']
        : co2.stale ? ['sem leitura', 'warn']
        : co2.valid ? ['leitura real', ''] : ['inválido', 'danger'];
      const st = q('[data-el=st-co2]');
      st.textContent = src;
      st.dataset.tone = tone;
      q('[data-el=ppm-co2]').textContent = String(Math.round(co2.ppm));
      q('[data-el=bar-co2]').style.width = `${Math.min(1, Math.max(0, co2.ppm / co2Max)) * 100}%`;
      const trig = q('[data-el=trig-co2]');
      trig.textContent = co2.trigger ? 'ligado' : 'desligado';
      trig.dataset.on = String(co2.trigger);
    }
  };

  // --- sampler: one tick plots every packet queued since the previous one ------
  const NT = 16; // trace count (see `traces`)
  // Every line keeps HISTORY_S seconds of packets here, whatever window is showing, so a
  // larger window has its past at once. Each redraw hands the chart only the visible part,
  // reduced to its min/max envelope when it is longer than MAX_DRAWN_POINTS: the cost of a
  // redraw no longer grows with the window or the notify rate.
  const bufX: number[][] = [];
  const bufY: (number | null)[][] = [];
  const tick = (): void => {
    // Drain first: if renderCards throws, the queues must not grow without bound.
    const imus = imuQ.drain();
    const senss = sensQ.drain();
    const co2s = co2Q.drain();
    try { renderCards(); } catch (e) { console.warn('renderCards falhou', e); }
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
    const viewX: number[][] = [];
    const viewY: (number | null)[][] = [];
    for (let tr = 0; tr < NT; tr++) {
      const bx = (bufX[tr] ??= []);
      const by = (bufY[tr] ??= []);
      for (let i = 0; i < xs[tr].length; i++) { bx.push(xs[tr][i]); by.push(ys[tr][i]); }
      const old = firstIndexAtOrAfter(bx, t - HISTORY_S);
      if (old > 0) { bx.splice(0, old); by.splice(0, old); }
      const from = firstIndexAtOrAfter(bx, t - windowS);
      const v = minMaxDecimate(bx.slice(from), by.slice(from), MAX_DRAWN_POINTS);
      viewX.push(v.x);
      viewY.push(v.y);
    }
    // One redraw: the data and the time range together. Every x axis follows the bottom one.
    void Plotly.update(
      chartEl,
      { x: viewX, y: viewY },
      { [`xaxis${N_ROWS}.range`]: [t - windowS, t], [`xaxis${N_ROWS}.autorange`]: false },
      viewX.map((_, i) => i),
    );
  };

  const timer = setInterval(() => {
    if (document.hidden || paused) { imuQ.clear(); sensQ.clear(); co2Q.clear(); return; }
    tick();
  }, TICK_MS);
  window.addEventListener('pagehide', () => {
    clearInterval(timer);
    clearInterval(specTimer);
    Plotly.purge(chartEl);
    Plotly.purge(specEl);
  });
  return {
    setPaused(p: boolean): void {
      paused = p;
      // Stale samples would plot as a burst of old points on resume.
      imuQ.clear(); sensQ.clear(); co2Q.clear();
      audioRing.length = 0;
    },
  };
}
