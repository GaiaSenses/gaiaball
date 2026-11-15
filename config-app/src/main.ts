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
import { StageController, loadUiMode, type UiMode } from './stage/stage-mode.ts';
import { mountStageBar } from './stage/stage-bar.ts';
import { renderSwitch, setSwitchValue } from './ui/switch.ts';
import { OrientationScene } from './viz/orientation-scene.ts';
import { VIEW_PRESETS, type ViewId } from './viz/view-presets.ts';
import { PdBridge, browserPdDeps } from './pd/pd-bridge.ts';
import { loadPdPrefs, mountPdBlock, mountPdChip } from './pd/pd-panel.ts';
import { encodeCo2Packet, encodeImuPacket, encodeNavPacket, encodeSensorsPacket } from './protocol/encode.ts';
import { AUDIO_FULL_SCALE, type AudioChunkSample, type Co2Sample, type ImuSample, type NavSample, type SensorsSample } from './protocol/packets.ts';

initTheme();

const app = document.querySelector<HTMLDivElement>('#app')!;
app.innerHTML = `
  <div id="banner"></div>
  <header id="topbar">
    <div class="brand">
      <span class="brand-mark" aria-hidden="true"></span>
      <div class="brand-text"><h1>Gaiaball</h1><p>painel de controle</p></div>
    </div>
    <div class="mode-box">
      <div id="mode-switch"></div>
      <p id="mode-text" class="mode-text" role="status"></p>
    </div>
    <div class="top-actions">
      <span class="chip" id="pd-chip" data-state="off">Pd: desligado</span>
      <span class="chip" id="conn-chip" data-state="off">Desconectado</span>
      <button id="btn-theme" type="button"></button>
    </div>
  </header>
  <div id="stage-bar" class="stage-bar" role="region" aria-label="Barra do modo palco" hidden></div>
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
        <div class="view-picker">
          <button id="btn-view" type="button" class="view-btn" aria-label="Vistas da câmera" title="Vistas da câmera" aria-haspopup="menu" aria-expanded="false">
            <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" aria-hidden="true"><path d="M12 3 20 7.5v9L12 21l-8-4.5v-9z"/><path d="M4 7.5 12 12l8-4.5M12 12v9"/></svg>
          </button>
          <div id="view-menu" class="view-menu" role="menu" aria-label="Vistas da câmera" hidden></div>
        </div>
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
client.addEventListener('firmware-outdated', () => {
  app.querySelector('#banner')!.innerHTML =
    '<div class="banner">Firmware da bola desatualizado: grave o firmware novo (o pacote de movimento agora é a versão 2).</div>';
});
export const stats = new StreamStats();
// Reset on every (re)connect, otherwise a device reboot freezes the stats.
client.addEventListener('status', (ev) => {
  if ((ev as CustomEvent<{ connected: boolean }>).detail.connected) stats.reset();
});

// Read the configuration as soon as a link comes up, so the config panel's snapshot (the
// "read before applying" guard) and the IMU status line are filled without a click. The
// panels listen to 'config'; a failed read is non-fatal: "Ler do dispositivo" still works.
client.addEventListener('status', (ev) => {
  if (!(ev as CustomEvent<{ connected: boolean }>).detail.connected) return;
  void client.readConfig()
    .then((regs) => client.dispatchEvent(new CustomEvent('config', { detail: regs })))
    .catch((e) => console.warn('leitura automática da configuração falhou', e));
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

const isDemo = Boolean(new URLSearchParams(location.search).get('demo'));
const bridge = new PdBridge(browserPdDeps());
client.setRawSink((b) => bridge.forward(b));
const pdPrefs = loadPdPrefs();
// The bridge only runs while the ball is connected (or in demo) and the user left it on.
const syncPd = (): void => {
  bridge.setPort(pdPrefs.port);
  bridge.setEnabled(pdPrefs.enabled && (client.connected || isDemo));
};
client.addEventListener('status', syncPd);
mountPdChip(app.querySelector<HTMLElement>('#pd-chip')!, bridge);

mountConnectionPanel(app.querySelector<HTMLElement>('#panel-conexao')!, client);
mountPdBlock(app.querySelector<HTMLElement>('#panel-conexao')!, bridge, pdPrefs, syncPd);
syncPd();
mountConfigPanel(app.querySelector<HTMLElement>('#panel-config')!, client);
const pausable = [
  mountMagCalPanel(app.querySelector<HTMLElement>('#panel-magcal')!, client),
  mountMetricsPanel(app.querySelector<HTMLElement>('#panel-metricas')!, client, stats),
  mountSensorsPanel(app.querySelector<HTMLElement>('#panel-sensores')!, client),
];

// 3D view of the treated quaternion exactly as received over BLE (no UI smoothing:
// the real knob is "Suavização do quaternion (slerp)" in the Configuração panel).
const scene = new OrientationScene(app.querySelector<HTMLCanvasElement>('#viz-canvas')!);
scene.setDark(resolvedTheme() === 'dark');
onThemeChange((t) => scene.setDark(t === 'dark'));
window.addEventListener('pagehide', () => scene.dispose());

// CAD-style view menu (top-right corner of the 3D stage).
{
  const btn = app.querySelector<HTMLButtonElement>('#btn-view')!;
  const menu = app.querySelector<HTMLElement>('#view-menu')!;
  menu.innerHTML = VIEW_PRESETS.map((p) =>
    `<button type="button" role="menuitemradio" aria-checked="false" data-view="${p.id}" tabindex="-1">${p.label}</button>`).join('');
  const items = [...menu.querySelectorAll<HTMLButtonElement>('button')];
  const setOpen = (open: boolean) => {
    menu.hidden = !open;
    btn.setAttribute('aria-expanded', String(open));
    if (open) {
      const cur = scene.currentView();
      items.forEach((b) => b.setAttribute('aria-checked', String(b.dataset.view === cur)));
      (items.find((b) => b.dataset.view === cur) ?? items[0]).focus();
    }
  };
  btn.addEventListener('click', () => setOpen(btn.getAttribute('aria-expanded') !== 'true'));
  menu.addEventListener('click', (e) => {
    const b = (e.target as HTMLElement).closest<HTMLButtonElement>('button[data-view]');
    if (!b) return;
    scene.setView(b.dataset.view as ViewId);
    setOpen(false);
    btn.focus();
  });
  menu.addEventListener('keydown', (e) => {
    if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
    e.preventDefault();
    const i = items.indexOf(document.activeElement as HTMLButtonElement);
    items[(i + (e.key === 'ArrowDown' ? 1 : items.length - 1)) % items.length].focus();
  });
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape' && !menu.hidden) { setOpen(false); btn.focus(); }
  });
  document.addEventListener('click', (e) => {
    if (!menu.hidden && !(e.target as HTMLElement).closest('.view-picker')) setOpen(false);
  });
}
app.querySelector<HTMLSelectElement>('#sel-model')!.addEventListener('change', (e) =>
  scene.setModel((e.target as HTMLSelectElement).value as 'plane' | 'box'));

client.addEventListener('imu', (ev) => {
  if (stage.mode === 'palco') return; // the screen is paused; the Pd forwarding already happened in ingest
  const s = (ev as CustomEvent<ImuSample>).detail;
  stats.push(s.seq, s.tUs, performance.now());
  scene.setQuaternion(s.quat);
});

// Stage mode: only the Pd forwarding (in client.ingest) and the stage bar stay alive.
const LOCKED_IDS = ['viz-wrap', 'panels', 'panel-sensores'];
const MODE_TEXT: Record<UiMode, string> = {
  palco: 'Para apresentar. Os dados vão para o Pd com o menor atraso possível. O 3D, os gráficos e os ajustes ficam pausados.',
  ajuste: 'Para preparar e ajustar. Mostra tudo. O envio ao Pd continua, mas pode atrasar alguns milissegundos quando a tela está ocupada.',
};
const modeTextEl = app.querySelector<HTMLElement>('#mode-text')!;
const stageBar = mountStageBar(app.querySelector<HTMLElement>('#stage-bar')!, client, bridge, () => void changeMode('ajuste'));
const uiStorage = ((): Storage | null => { try { return window.localStorage; } catch { return null; } })();
const stage = new StageController({
  scene,
  panels: pausable,
  client,
  setLocked(on) {
    for (const id of LOCKED_IDS) {
      const el = app.querySelector<HTMLElement>('#' + id)!;
      el.classList.toggle('stage-locked', on);
      el.toggleAttribute('inert', on);
      if (on) el.dataset.lockLabel = '🔒 Pausado no modo palco'; else delete el.dataset.lockLabel;
    }
    if (on && app.contains(document.activeElement) && !document.activeElement?.closest('#topbar, #stage-bar')) {
      (document.activeElement as HTMLElement).blur();
    }
  },
  showBar: (on) => stageBar.show(on),
  storage: uiStorage,
});
const modeSwitch = renderSwitch({
  options: [{ value: 0, label: 'Modo ajuste' }, { value: 1, label: 'Modo palco' }],
  value: loadUiMode(uiStorage) === 'palco' ? 1 : 0,
  onChange: (v) => void changeMode(v === 1 ? 'palco' : 'ajuste'),
  label: 'Modo da tela',
});
app.querySelector('#mode-switch')!.append(modeSwitch);
async function changeMode(m: UiMode): Promise<void> {
  setSwitchValue(modeSwitch, m === 'palco' ? 1 : 0);
  modeTextEl.textContent = MODE_TEXT[m];
  await stage.setMode(m);
  if (m === 'ajuste') {
    stats.reset(); // the pause left a gap in the sequence numbers
    modeSwitch.focus(); // the stage bar (and its focused button) just got hidden
  }
}
client.addEventListener('status', (ev) => {
  if ((ev as CustomEvent<{ connected: boolean }>).detail.connected) void stage.onConnected();
  else stage.onDisconnected();
});
modeTextEl.textContent = MODE_TEXT.ajuste;
if (loadUiMode(uiStorage) === 'palco') void changeMode('palco');

if (isDemo) {
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
  // Packets with a wire format are encoded to bytes and go through client.ingest, so the screen
  // and the Pd bridge get exactly what a real ball would send. Only the raw 2 kHz audio chunk
  // stays a decoded object (no encoder for it: it is diagnostics, never forwarded to Pd).
  const asView = (b: Uint8Array): DataView => new DataView(b.buffer, b.byteOffset, b.byteLength);
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
      acc: [Math.round(Math.sin(t * 1.3) * 250), Math.round(Math.cos(t * 0.9) * 250), Math.round(1000 + Math.sin(t * 3) * 40)],
      flags: 0b1101,
      quatValid: true,
      nineAxis: false,
      moving: true,
      // Microphone envelope, as the firmware publishes it on every orientation packet:
      // a breath every ~6 s on top of a -58 dBFS room floor, peak ~8 dB above the RMS.
      audioValid: true,
      breath: t % 6 < 0.1, // latched flag: a few packets at the start of each blow
      audioRmsDbfs: [
        -58 + (Math.sin(t * 1.05) > 0.85 ? 30 : 0) + 2 * Math.sin(t * 7),
        -60 + (Math.sin(t * 1.05) > 0.85 ? 28 : 0) + 2 * Math.sin(t * 6.3),
      ],
      audioPeakDbfs: [
        -50 + (Math.sin(t * 1.05) > 0.85 ? 30 : 0) + 2 * Math.sin(t * 9),
        -52 + (Math.sin(t * 1.05) > 0.85 ? 28 : 0) + 2 * Math.sin(t * 8.1),
      ],
    };
    client.ingest('imu', asView(encodeImuPacket(s))); // the 'imu' listener above drives the scene and the stats
  }, 20);
  let sensSeq = 0;
  setInterval(() => {
    const t = performance.now() / 1000;
    const rms = (base: number, f: number) => base * (1 + 0.6 * Math.sin(t * f)) + (Math.sin(t * 31) > 0.95 ? 400000 : 0);
    // The active metric alternates every 20 s so both threshold rows can be seen;
    // -35.5 dBFS expressed in counts of the metric in use.
    const demoPeak = Math.floor(t / 20) % 2 === 1;
    const demoThreshold = Math.round(AUDIO_FULL_SCALE * 10 ** (-35.5 / 20) / (demoPeak ? 1 : Math.SQRT2));
    const detail: SensorsSample = {
      seq: sensSeq++ & 0xffff,
      tMs: Math.round(t * 1000),
      rmsL: Math.max(0, Math.round(rms(22000, 0.8))),
      rmsR: Math.max(0, Math.round(rms(18000, 1.1))),
      threshold: demoThreshold,
      potRaw: Math.round(2048 + 1800 * Math.sin(t * 0.15)),
      flags: 0b1111 | (demoPeak ? 0b10000 : 0),
      audioOk: true,
      audioEnabled: true,
      potControls: true,
      audioFiltered: true,
      metricPeak: demoPeak,
      adaptive: false,
    };
    client.ingest('diag', asView(encodeSensorsPacket(detail)));
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
  // Navigation: lat/lon/bearing drift slowly around the default reference; the globe alternates
  // between moving (~6 s) and settled (~4 s). Cadence 20 Hz, enough for the demo (the real
  // default navNotifyHz is 50 Hz).
  let navSeq = 0;
  setInterval(() => {
    const t = performance.now() / 1000;
    const moving = (t % 10) < 6;
    const detail: NavSample = {
      seq: navSeq++ & 0xffff,
      tUs: Math.round(t * 1e6) >>> 0,
      mode: 1,
      flags: moving ? 0b101 : 0b011,
      valid: true,
      settled: !moving,
      moving,
      lat: -22.905556 + 8 * Math.sin(t * 0.07),
      lon: -47.060833 + 12 * Math.sin(t * 0.05),
      bearingDeg: ((t * 9 + 180) % 360) - 180,
      speedDegS: moving ? 30 + 20 * Math.sin(t * 0.8) : 0,
    };
    client.ingest('nav', asView(encodeNavPacket(detail)));
  }, 50);
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
      trigger: false,
    };
    client.ingest('co2', asView(encodeCo2Packet(detail)));
  }, 1000);
}
