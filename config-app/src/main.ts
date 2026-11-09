import './style.css';
import '@fontsource/ibm-plex-sans/400.css';
import '@fontsource/ibm-plex-sans/500.css';
import '@fontsource/ibm-plex-sans/600.css';
import '@fontsource/ibm-plex-mono/400.css';
import '@fontsource/ibm-plex-mono/500.css';
import { GaiaballClient, type StatusDetail } from './ble/gaiaball-client.ts';
import { mountConnectionPanel } from './panels/connection-panel.ts';
import { StreamStats } from './metrics/stream-stats.ts';
import { mountConfigPanel } from './panels/config-panel.ts';
import { mountMagCalPanel } from './panels/magcal-panel.ts';
import { mountMetricsPanel } from './panels/metrics-panel.ts';
import { mountSensorsPanel } from './panels/sensors-panel.ts';
import { decorateInfo } from './panels/info.ts';
import { initTheme, onThemeChange, resolvedTheme, toggleTheme } from './theme.ts';
import { OrientationScene } from './viz/orientation-scene.ts';
import type { AudioChunkSample, Co2Sample, ImuSample, SensorsSample } from './protocol/packets.ts';

initTheme();

const app = document.querySelector<HTMLDivElement>('#app')!;
app.innerHTML = `
  <div id="banner"></div>
  <header id="topbar">
    <div class="brand">
      <span class="brand-mark" aria-hidden="true"></span>
      <div class="brand-text"><h1>Gaiaball</h1><p>painel de controle</p></div>
    </div>
    <div class="top-actions">
      <span class="chip" id="conn-chip" data-state="off">Desconectado</span>
      <button id="btn-theme" type="button"></button>
    </div>
  </header>
  <main>
    <div id="viz-wrap"><div id="viz">
      <div class="viz-controls">
        <label class="ctl">Modelo
          <select id="sel-model"><option value="plane">avião</option><option value="box">caixa</option></select>
        </label>
        <span class="ctl" data-info="O modelo mostra a orientação exatamente como ela chega por BLE: o quaternion já tratado pelo firmware (fusão, calibração de montagem, zero e, se ligada, a suavização slerp do próprio dispositivo). Para suavizar o movimento, use “Suavização do quaternion (slerp)” no painel Configuração — aquele campo altera o firmware de verdade.">Orientação recebida por BLE</span>
      </div>
      <div id="viz-stage">
        <canvas id="viz-canvas"></canvas>
      </div>
    </div></div>
    <aside id="panels">
      <section id="panel-conexao"></section>
      <section id="panel-config"></section>
      <section id="panel-magcal"></section>
      <section id="panel-metricas"></section>
    </aside>
    <section id="panel-sensores"></section>
  </main>`;
decorateInfo(app);

if (!(navigator as Navigator & { bluetooth?: unknown }).bluetooth) {
  app.querySelector('#banner')!.innerHTML =
    '<div class="banner">Web Bluetooth não está disponível neste navegador. Use Chrome ou Edge em localhost ou HTTPS.</div>';
}

const client = new GaiaballClient();
export const stats = new StreamStats();
// Reset on every (re)connect, otherwise a device reboot freezes the stats.
client.addEventListener('status', (ev) => {
  if ((ev as CustomEvent<{ connected: boolean }>).detail.connected) stats.reset();
});

// Top bar: connection chip mirrors the Conexão panel; theme button flips light/dark.
const chip = app.querySelector<HTMLElement>('#conn-chip')!;
client.addEventListener('status', (ev) => {
  const d = (ev as CustomEvent<StatusDetail>).detail;
  chip.dataset.state = d.connected ? 'on' : 'off';
  chip.textContent = d.connected ? (d.name ?? 'Conectado') : 'Desconectado';
});
const themeBtn = app.querySelector<HTMLButtonElement>('#btn-theme')!;
const renderThemeBtn = (): void => {
  const dark = resolvedTheme() === 'dark';
  themeBtn.textContent = dark ? '☀' : '☾';
  const tip = dark ? 'Mudar para o tema claro' : 'Mudar para o tema escuro';
  themeBtn.setAttribute('aria-label', tip);
  themeBtn.title = tip;
};
themeBtn.addEventListener('click', toggleTheme);
onThemeChange(renderThemeBtn);
renderThemeBtn();

mountConnectionPanel(app.querySelector<HTMLElement>('#panel-conexao')!, client);
mountConfigPanel(app.querySelector<HTMLElement>('#panel-config')!, client);
mountMagCalPanel(app.querySelector<HTMLElement>('#panel-magcal')!, client);
mountMetricsPanel(app.querySelector<HTMLElement>('#panel-metricas')!, client, stats);
mountSensorsPanel(app.querySelector<HTMLElement>('#panel-sensores')!, client);

// 3D view of the treated quaternion exactly as received over BLE (no UI smoothing:
// the real knob is "Suavização do quaternion (slerp)" in the Configuração panel).
const scene = new OrientationScene(app.querySelector<HTMLCanvasElement>('#viz-canvas')!);
scene.setDark(resolvedTheme() === 'dark');
onThemeChange((t) => scene.setDark(t === 'dark'));
window.addEventListener('pagehide', () => scene.dispose());
app.querySelector<HTMLSelectElement>('#sel-model')!.addEventListener('change', (e) =>
  scene.setModel((e.target as HTMLSelectElement).value as 'plane' | 'box'));

client.addEventListener('imu', (ev) => {
  const s = (ev as CustomEvent<ImuSample>).detail;
  stats.push(s.seq, s.tUs, performance.now());
  scene.setQuaternion(s.quat);
});

if (new URLSearchParams(location.search).get('demo')) {
  chip.dataset.state = 'demo';
  chip.textContent = 'modo demo';
  // Synthetic slow compound rotation (quaternion products only), no BLE needed.
  type Q = { w: number; x: number; y: number; z: number };
  const mul = (a: Q, b: Q) => ({
    w: a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    x: a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
    y: a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
    z: a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
  });
  const axisAngle = (ax: number, ay: number, az: number, ang: number) => {
    const s = Math.sin(ang / 2);
    return { w: Math.cos(ang / 2), x: ax * s, y: ay * s, z: az * s };
  };
  // Synthetic packets are dispatched as client events, so every panel (3D, métricas,
  // sensores) exercises the same code path as a real device.
  const emit = <T>(event: string, detail: T): void => {
    client.dispatchEvent(new CustomEvent(event, { detail }));
  };
  let seq = 0;
  setInterval(() => {
    const t = performance.now() / 1000;
    // Small fast wobble on top, like real sensor noise.
    const q = mul(mul(axisAngle(0, 0, 1, t * 0.5), axisAngle(0, 1, 0, Math.sin(t * 0.7) * 0.6)), axisAngle(1, 0, 0, Math.sin(t * 25) * 0.04));
    const s: ImuSample = {
      seq: seq++ & 0xffff,
      tUs: Math.round(t * 1e6),
      quat: q,
      acc: [Math.round(Math.sin(t * 1.3) * 2048), Math.round(Math.cos(t * 0.9) * 2048), Math.round(8192 + Math.sin(t * 3) * 300)],
      flags: 0b1101,
      quatValid: true,
      nineAxis: false,
      moving: true,
      // Microphone envelope, as the firmware publishes it on every orientation packet:
      // a breath every ~6 s on top of a -58 dBFS room floor, peak ~8 dB above the RMS.
      audioValid: true,
      audioRmsDbfs: [
        -58 + (Math.sin(t * 1.05) > 0.85 ? 30 : 0) + 2 * Math.sin(t * 7),
        -60 + (Math.sin(t * 1.05) > 0.85 ? 28 : 0) + 2 * Math.sin(t * 6.3),
      ],
      audioPeakDbfs: [
        -50 + (Math.sin(t * 1.05) > 0.85 ? 30 : 0) + 2 * Math.sin(t * 9),
        -52 + (Math.sin(t * 1.05) > 0.85 ? 28 : 0) + 2 * Math.sin(t * 8.1),
      ],
    };
    emit('imu', s); // the 'imu' listener above drives the scene and the stats
  }, 20);
  let sensSeq = 0;
  setInterval(() => {
    const t = performance.now() / 1000;
    const rms = (base: number, f: number) => base * (1 + 0.6 * Math.sin(t * f)) + (Math.sin(t * 31) > 0.95 ? 400000 : 0);
    const detail: SensorsSample = {
      seq: sensSeq++ & 0xffff,
      tMs: Math.round(t * 1000),
      rmsL: Math.max(0, Math.round(rms(22000, 0.8))),
      rmsR: Math.max(0, Math.round(rms(18000, 1.1))),
      threshold: 100000,
      potRaw: Math.round(2048 + 1800 * Math.sin(t * 0.15)),
      flags: 0b1111,
      audioOk: true,
      audioEnabled: true,
      potControls: true,
      audioFiltered: true,
    };
    emit('sensors', detail);
  }, 200);
  // Raw 2 kHz audio chunks: low hum + breath-band noise bursts every ~6 s.
  let audioSeq = 0;
  let audioIdx = 0;
  setInterval(() => {
    const samples = new Int16Array(96);
    for (let i = 0; i < 96; i++, audioIdx++) {
      const ts = audioIdx / 2000;
      const breath = Math.sin(ts * 1.05) > 0.85 ? 1 : 0; // periodic "blow"
      let v = 600 * Math.sin(2 * Math.PI * 80 * ts); // 80 Hz hum
      v += 300 * (Math.random() - 0.5); // noise floor
      if (breath) v += 9000 * (Math.random() - 0.5) * (1 + Math.sin(2 * Math.PI * 3 * ts)); // turbulence < ~500 Hz after ring smoothing
      samples[i] = Math.max(-32768, Math.min(32767, Math.round(v)));
    }
    const detail: AudioChunkSample = {
      seq: audioSeq++ & 0xffff,
      tMs: Math.round(performance.now()),
      sampleRateHz: 2000,
      samples,
      flags: 1,
      bpfEnabled: true,
      hpHz: 10,
      lpHz: 200,
    };
    emit('audio', detail);
  }, 48);
  let co2Seq = 0;
  setInterval(() => {
    const t = performance.now() / 1000;
    const detail: Co2Sample = {
      seq: co2Seq++ & 0xffff,
      tMs: Math.round(t * 1000),
      ppm: Math.round(430 + 40 * Math.sin(t * 0.05)),
      flags: 0b001,
      ageS: 0,
      valid: true,
      stale: false,
      virtualActive: false,
    };
    emit('co2', detail);
  }, 1000);
}
