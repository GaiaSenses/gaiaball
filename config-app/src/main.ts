import './style.css';
import { GaiaballClient } from './ble/gaiaball-client.ts';
import { mountConnectionPanel } from './panels/connection-panel.ts';
import { StreamStats } from './metrics/stream-stats.ts';

const app = document.querySelector<HTMLDivElement>('#app')!;
app.innerHTML = `
  <div id="banner"></div>
  <header><h1>Gaiaball Config</h1></header>
  <main>
    <div id="viz"></div>
    <section id="panel-conexao"></section>
  </main>`;

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
mountConnectionPanel(app.querySelector<HTMLElement>('#panel-conexao')!, client);
