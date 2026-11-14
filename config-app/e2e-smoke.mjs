// Static smoke check: serves dist/ via `vite preview`, checks index.html and that the bundle
// contains the new panel strings. Browser-level check (demo canvas, console errors) was run via Playwright MCP.
import { spawn } from 'node:child_process';
import { readdirSync, readFileSync } from 'node:fs';

const port = 4199;
const srv = spawn('npx', ['vite', 'preview', '--port', String(port), '--strictPort'], { stdio: 'ignore' });
const fail = (m) => { console.error('FAIL:', m); srv.kill(); process.exit(1); };
try {
  await new Promise((r) => setTimeout(r, 2500));
  const res = await fetch(`http://localhost:${port}/?demo=1`);
  const html = await res.text();
  if (!res.ok || !html.includes('id="app"')) fail('index.html not served');
  const js = readdirSync('dist/assets').filter((f) => f.endsWith('.js')).map((f) => readFileSync(`dist/assets/${f}`, 'utf8')).join('');
  for (const s of ['Ler do dispositivo', 'Aplicar', 'Salvar preset', 'gaiaball-presets', 'Exportar JSON', 'Vistas da câmera', 'Superior', 'Inferior']) {
    if (!js.includes(s)) fail(`bundle missing "${s}"`);
  }
  console.log('OK: index served, bundle contains panel strings');
} finally { srv.kill(); }
