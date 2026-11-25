// Checks the help patch (../gaiaball-help.pd) in a real Pd, without a ball or a browser:
//
//   1. opening the help does NOT take port 9478 (it would steal it from the user's patch);
//   2. its "TESTAR DAQUI" toggle creates a [gaiaball] that answers the WebSocket handshake;
//   3. switching the toggle off frees the port, and on again works again;
//   4. with --com-objeto, a user patch with its own [gaiaball] is opened first: the help's
//      toggle then fails to bind ("Address already in use") and the user's [gaiaball] keeps
//      answering, before and after the toggle goes off again.
//
//   node pd-gaiaball/tests/ajuda.mjs [--pd /path/to/pd] [--com-objeto]
//
// The toggle is driven from here through a [netreceive 9612] added to a temporary copy of
// the help, placed next to a copy of gaiaball.pd (as in an installed package).
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const PD = args.includes('--pd') ? args[args.indexOf('--pd') + 1] : 'pd';
const WITH_USER = args.includes('--com-objeto');
const PORT = 9478;
const CTL = 9612;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let pd = null;
let pdErr = '';
const fail = (m) => {
  console.error('FAIL:', m);
  if (pdErr) console.error('--- Pd stderr ---\n' + pdErr);
  if (pd) pd.kill();
  process.exit(1);
};
const ok = (m) => console.log('ok -', m);

/** Resolves to a socket, or null when nothing listens on the port. */
function connect(port) {
  return new Promise((resolve) => {
    const s = net.connect(port, '127.0.0.1');
    s.once('connect', () => resolve(s));
    s.once('error', () => resolve(null));
  });
}

/** true when a [gaiaball] on PORT answers the RFC 6455 handshake. */
async function answers() {
  const s = await connect(PORT);
  if (!s) return false;
  const key = 'dGhlIHNhbXBsZSBub25jZQ==';
  let head = '';
  s.on('data', (d) => { head += d.toString('latin1'); });
  s.write(`GET / HTTP/1.1\r\nHost: 127.0.0.1:${PORT}\r\nUpgrade: websocket\r\n` +
    `Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${key}\r\n\r\n`);
  const end = Date.now() + 3000;
  while (!head.includes('\r\n\r\n') && Date.now() < end) await sleep(10);
  s.destroy();
  await sleep(150); // let the server see the client go before the next probe
  const want = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64');
  return head.startsWith('HTTP/1.1 101') && head.includes(want);
}

async function nothingListens() {
  const s = await connect(PORT);
  if (s) s.destroy();
  return !s;
}

/** Sets the help's toggle (through the [netreceive] of the temporary copy). */
async function toggle(v) {
  const s = await connect(CTL);
  if (!s) fail('remote control of the help is gone');
  s.end(`${v};\n`);
  await sleep(400);
}

// ---- temporary package folder: gaiaball.pd + a remote-controlled copy of the help ----
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gaiaball-ajuda-'));
const dir = path.join(tmp, 'gaiaball');
fs.mkdirSync(dir);
fs.copyFileSync(path.join(here, '..', 'gaiaball.pd'), path.join(dir, 'gaiaball.pd'));
const help = fs.readFileSync(path.join(here, '..', 'gaiaball-help.pd'), 'utf8')
  .split('\n').filter(Boolean);
let depth = 0;
let top = 0;
let tgl = -1;
for (const l of help.slice(1)) { // count top-level objects, as #X connect does
  if (l.startsWith('#N canvas')) { depth++; continue; }
  if (l.startsWith('#X restore')) { if (--depth === 0) top++; continue; }
  if (depth > 0 || l.startsWith('#X connect')) continue;
  if (tgl < 0 && /^#X obj -?\d+ -?\d+ tgl /.test(l)) tgl = top;
  top++;
}
if (tgl < 0) fail('the help has no toggle');
const firstConnect = help.findIndex((l) => l.startsWith('#X connect'));
help.splice(firstConnect, 0, `#X obj 900 900 netreceive ${CTL};`);
help.push(`#X connect ${top} 0 ${tgl} 0;`);
const helpCopy = path.join(dir, 'gaiaball-help-teste.pd');
fs.writeFileSync(helpCopy, help.join('\n') + '\n');
const user = path.join(tmp, 'usuario.pd');
fs.writeFileSync(user, '#N canvas 0 0 300 100 12;\n#X obj 20 20 gaiaball;\n');

if (!(await nothingListens())) fail(`port ${PORT} is already in use: close the other program`);

const pdArgs = ['-nogui', '-stderr', '-noaudio', '-nomidi', '-noprefs', '-path', dir];
if (WITH_USER) pdArgs.push('-open', user);
pdArgs.push('-open', helpCopy);
pd = spawn(PD, pdArgs);
pd.stderr.on('data', (d) => { pdErr += d; });
pd.stdout.on('data', () => {});
let done = false;
pd.on('exit', (c, s) => { if (!done) fail(`Pd exited early (${c ?? s})`); });

const end = Date.now() + 5000;
for (;;) {
  const s = await connect(CTL);
  if (s) { s.destroy(); break; }
  if (Date.now() > end) fail('the help copy did not open');
  await sleep(50);
}
await sleep(300);

if (!WITH_USER) {
  if (!(await nothingListens())) fail(`the help took port ${PORT} as soon as it opened`);
  ok(`help opened: port ${PORT} is free`);
  await toggle(1);
  if (!(await answers())) fail('toggle on: no [gaiaball] answering');
  ok('toggle on: its [gaiaball] answers the RFC 6455 handshake');
  await toggle(0);
  if (!(await nothingListens())) fail(`toggle off: port ${PORT} still taken`);
  ok(`toggle off: port ${PORT} free again`);
  await toggle(1);
  if (!(await answers())) fail('toggle on again: no [gaiaball] answering');
  ok('toggle on again: answering again');
  await toggle(0);
} else {
  if (!(await answers())) fail('the user\'s [gaiaball] is not answering');
  ok('user patch first: its [gaiaball] answers; the help opened beside it');
  await toggle(1);
  if (!/listen failed: Address already in use/.test(pdErr))
    fail('toggle on: expected "Address already in use" from the help\'s [gaiaball]');
  if (!(await answers())) fail('toggle on: the user\'s [gaiaball] stopped answering');
  ok('toggle on: "Address already in use" on the console; the user\'s [gaiaball] still answers');
  await toggle(0);
  if (!(await answers())) fail('toggle off: the user\'s [gaiaball] stopped answering');
  ok('toggle off: the user\'s [gaiaball] still answers');
}

done = true;
pd.kill();
fs.rmSync(tmp, { recursive: true, force: true });
const errors = pdErr.split('\n').filter((l) => /error|couldn't|no such/i.test(l));
const expected = WITH_USER ? errors.filter((l) => /listen failed: Address already in use/.test(l)) : [];
if (errors.length !== expected.length) fail(`Pd errors:\n${errors.join('\n')}`);
ok(WITH_USER ? 'the only console error is the expected "Address already in use"'
  : 'no errors on the Pd console');
console.log('\nALL OK');
process.exit(0);
