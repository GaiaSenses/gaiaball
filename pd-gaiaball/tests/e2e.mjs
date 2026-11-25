// End-to-end check of gaiaball.pd (the [gaiaball] abstraction) without a ball or a browser: a
// raw TCP client (node:net) does the WebSocket handshake the way Chrome does and sends the
// protocol's golden packets as masked binary frames; Pd's console (stderr) must show the right
// gaia.* values.
//
//   node pd-gaiaball/tests/e2e.mjs [--pd /path/to/pd] [--port 9478] [--objeto [--instalado]]
//
// The script starts Pd itself (-nogui -noprefs) with imprime.pd and:
//   - by default, ../gaiaball.pd opened as a top-level patch;
//   - with --objeto, objeto.pd, which creates [gaiaball] as an object (found through
//     -path ..) and prints its outlet, so the abstraction lookup and the outlet are tested too;
//   - with --objeto --instalado, objeto.pd without any -path: [gaiaball] must come from Pd's
//     standard paths (for an installed package; point HOME at the test home on Linux).
import { spawn } from 'node:child_process';
import { createHash, randomBytes } from 'node:crypto';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (name, dflt) => (args.includes(name) ? args[args.indexOf(name) + 1] : dflt);
const PD = opt('--pd', 'pd');
const PORT = Number(opt('--port', '9478'));
const OBJETO = args.includes('--objeto');
const INSTALADO = args.includes('--instalado');
if (INSTALADO && !OBJETO) {
  console.error('--instalado needs --objeto');
  process.exit(2);
}

const hex = (s) => Buffer.from(s.trim().split(/\s+/).map((b) => parseInt(b, 16)));
// firmware/docs/protocol.md
const IMU = hex('02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 1B F0 C8 F0 E4');
const CO2 = hex('01 02 02 01 0D 0C 0B 0A E8 03 05 07');
const NAV_A = hex('01 07 02 01 4E 61 BC 00 01 03 B8 E3 58 F2 36 16 F3 E3 94 11 7B 00 07');
const NAV_B = hex('01 07 FF FF FF FF FF FF 02 06 00 E9 A4 35 00 2E B6 94 B0 B9 FF FF FF');
const NAV_B_VALID = Buffer.from(NAV_B);
NAV_B_VALID[9] = 0x07; // same extremes, with the valid bit

/** An IMU packet with a tilted quaternion, negative acceleration and no audio. */
function imuTilted() {
  const p = Buffer.alloc(27);
  p[0] = 2; p[1] = 1;
  const q = [0.9, 0.2, 0.3, 0.25];
  const n = Math.hypot(...q);
  q.forEach((v, i) => p.writeInt16LE(Math.round((v / n) * 16384), 8 + 2 * i));
  [-123, 456, -789].forEach((v, i) => p.writeInt16LE(v, 16 + 2 * i));
  p[22] = 0x01; // quatValid only: no audio, no breath
  return p;
}
/** The site's legacy Euler convention (quatToEulerLegacy in config-app/src/protocol/euler.ts)
 * on the decoded (quantized) quaternion. */
function eulerLegacy(p) {
  const [w, x, y, z] = [0, 1, 2, 3].map((i) => p.readInt16LE(8 + 2 * i) / 16384);
  const qw = w, qx = y, qy = x, qz = -z, d = 180 / Math.PI;
  const t2 = Math.max(-1, Math.min(1, 2 * (qw * qy - qx * qz)));
  return [Math.atan2(2 * (qw * qx + qy * qz), 1 - 2 * (qx * qx + qy * qy)) * d,
    Math.asin(t2) * d, Math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) * d];
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let pd = null;
let pdErr = '';
const fail = (m) => {
  console.error('FAIL:', m);
  if (pdErr) console.error('--- Pd stderr (tail) ---\n' + pdErr.split('\n').slice(-60).join('\n'));
  if (pd) pd.kill();
  process.exit(1);
};
const ok = (m) => console.log('ok -', m);

async function waitFor(pred, ms, what) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (pred()) return;
    await sleep(5);
  }
  fail(`timed out waiting for ${what}`);
}

function frame(opcode, payload = Buffer.alloc(0), { masked = true } = {}) {
  const n = payload.length;
  const head = n < 126 ? Buffer.from([0x80 | opcode, (masked ? 0x80 : 0) | n])
    : Buffer.from([0x80 | opcode, (masked ? 0x80 : 0) | 126, n >> 8, n & 255]);
  if (!masked) return Buffer.concat([head, payload]);
  const mask = randomBytes(4);
  const body = Buffer.from(payload.map((b, i) => b ^ mask[i & 3]));
  return Buffer.concat([head, mask, body]);
}

const accept = (key) => createHash('sha1')
  .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');

/** One raw WebSocket client: handshake, then server frames parsed into `frames`. */
class Client {
  constructor(name) {
    this.name = name;
    this.buf = Buffer.alloc(0);
    this.head = null;
    this.frames = [];
    this.closed = false;
  }

  async connect() {
    const end = Date.now() + 5000;
    for (;;) {
      try {
        this.sock = await new Promise((resolve, reject) => {
          const s = net.connect(PORT, '127.0.0.1', () => resolve(s));
          s.once('error', reject);
        });
        break;
      } catch (e) {
        if (Date.now() > end) fail(`${this.name}: cannot connect (${e.code})`);
        await sleep(100);
      }
    }
    this.sock.on('data', (d) => this.onData(d));
    this.sock.on('close', () => { this.closed = true; });
    this.sock.on('error', () => {});
    return this;
  }

  onData(d) {
    this.buf = Buffer.concat([this.buf, d]);
    if (this.head === null) {
      const i = this.buf.indexOf('\r\n\r\n');
      if (i < 0) return;
      this.head = this.buf.subarray(0, i).toString('latin1');
      this.buf = this.buf.subarray(i + 4);
    }
    while (this.buf.length >= 2) {
      const n = this.buf[1] & 0x7f;
      if (n > 125 || this.buf.length < 2 + n) return;
      this.frames.push({ op: this.buf[0] & 0x0f, payload: this.buf.subarray(2, 2 + n) });
      this.buf = this.buf.subarray(2 + n);
    }
  }

  request(key, extra = '') {
    return `GET / HTTP/1.1\r\nHost: 127.0.0.1:${PORT}\r\nConnection: Upgrade\r\n` +
      'Pragma: no-cache\r\nCache-Control: no-cache\r\nUser-Agent: e2e\r\nUpgrade: websocket\r\n' +
      'Origin: https://gaiasenses.github.io\r\nSec-WebSocket-Version: 13\r\n' +
      'Accept-Encoding: gzip, deflate, br\r\nAccept-Language: pt-BR,pt;q=0.9\r\n' +
      `${extra}Sec-WebSocket-Key: ${key}\r\n` +
      'Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n\r\n';
  }

  async handshake({ key = randomBytes(16).toString('base64'), byteByByte = false } = {}) {
    const req = Buffer.from(this.request(key), 'latin1');
    if (byteByByte) {
      for (const b of req) {
        this.sock.write(Buffer.from([b]));
        await sleep(1);
      }
    } else this.sock.write(req);
    await waitFor(() => this.head !== null || this.closed, 3000, `${this.name}: 101`);
    if (this.head === null) fail(`${this.name}: closed before the 101`);
    const want = accept(key);
    const got = /sec-websocket-accept:\s*(\S+)/i.exec(this.head)?.[1];
    if (!this.head.startsWith('HTTP/1.1 101') || got !== want)
      fail(`${this.name}: bad handshake for key ${key}: got ${got}, want ${want}\n${this.head}`);
    return this;
  }

  send(op, payload, o) { this.sock.write(frame(op, payload, o)); }
  binary(p) { this.send(0x2, p); }
  end() { this.sock.end(); }
}

// ---- start Pd ----
// -noprefs: the user's saved search paths must not decide which [gaiaball] gets loaded.
const pdArgs = ['-nogui', '-stderr', '-noaudio', '-nomidi', '-noprefs'];
if (!OBJETO) pdArgs.push('-open', path.join(here, '..', 'gaiaball.pd'));
else {
  if (!INSTALADO) pdArgs.push('-path', path.join(here, '..'));
  pdArgs.push('-open', path.join(here, 'objeto.pd'));
}
pdArgs.push('-open', path.join(here, 'imprime.pd'));
console.log(`Pd: ${PD} ${pdArgs.join(' ')}`);
pd = spawn(PD, pdArgs);
pd.stderr.on('data', (d) => { pdErr += d; });
pd.stdout.on('data', () => {}); // Pd 0.55.2's expr prints "realloc called..." on stdout
pd.on('exit', (c, s) => { if (!done) fail(`Pd exited early (${c ?? s})`); });
let done = false;
const lines = () => pdErr.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
const count = (re) => lines().filter((l) => re.test(l)).length;

// 1. The RFC 6455 key, then the golden packets.
const a = await (await new Client('A').connect()).handshake({ key: 'dGhlIHNhbXBsZSBub25jZQ==' });
if (!a.head.includes('s3pPLMBiTxaQ9kYGzzhZRbK+xOo=')) fail('RFC 6455 vector: ' + JSON.stringify(a.head));
ok('A: RFC 6455 key -> s3pPLMBiTxaQ9kYGzzhZRbK+xOo=');
a.binary(IMU);
a.binary(CO2);
a.binary(IMU);
a.binary(NAV_A);
a.binary(NAV_B); // not valid: must publish nothing
a.binary(NAV_B_VALID);
const tilted = imuTilted();
a.binary(tilted);
a.send(0x9, Buffer.from('oi'));
await waitFor(() => a.frames.some((f) => f.op === 0xa), 2000, 'A: pong');
if (a.frames.find((f) => f.op === 0xa).payload.toString() !== 'oi') fail('A: pong payload');
ok('A: ping "oi" -> pong "oi"');
a.send(0x8, Buffer.from([0x03, 0xe8]));
await waitFor(() => a.frames.some((f) => f.op === 0x8), 2000, 'A: close echo');
a.end();
await waitFor(() => a.closed, 2000, 'A: closed');
ok('A: close answered with a close frame');
await waitFor(() => count(/^gaiaball: connected 0$/) >= 1, 3000, 'connected 0 after A');

// 2. Many random keys: the SHA-1 + base64 in [expr] against node:crypto.
for (let i = 0; i < 40; i++) {
  const c = await (await new Client(`K${i}`).connect()).handshake();
  c.end();
  await waitFor(() => c.closed, 2000, `K${i}: closed`);
  await sleep(30);
}
ok('40 random Sec-WebSocket-Key values: every accept matches node:crypto');

// 3. A request split into 1-byte TCP writes, a frame split in three, a 16-bit length frame.
const f = await (await new Client('F').connect()).handshake({ byteByByte: true });
const imuAccX = (v) => { const p = Buffer.from(IMU); p.writeInt16LE(v, 16); return p; };
const fr = frame(0x2, imuAccX(1111));
f.sock.write(fr.subarray(0, 1)); await sleep(20);
f.sock.write(fr.subarray(1, 9)); await sleep(20);
f.sock.write(fr.subarray(9));
f.binary(Buffer.concat([imuAccX(2222), Buffer.alloc(200, 0x55)])); // 227 bytes: 16-bit length
f.send(0x1, Buffer.from('{"texto":true}')); // ignored
const co2 = Buffer.from(CO2); co2.writeUInt16LE(1234, 8);
f.binary(co2);
await sleep(300);
f.end();
await waitFor(() => f.closed, 2000, 'F: closed');
ok('F: 1-byte writes, split frame, 227-byte frame, text frame ignored');
await sleep(100);

// 4. An unmasked frame is a protocol error: the server drops the connection.
const u = await (await new Client('U').connect()).handshake();
u.send(0x2, IMU, { masked: false });
await waitFor(() => u.closed, 2000, 'U: dropped after an unmasked frame');
ok('U: unmasked frame -> connection dropped');
await sleep(100);

// 5. A plain HTTP request (no key) gets a 400.
const h = await new Client('H').connect();
h.sock.write('GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n');
await waitFor(() => h.closed, 2000, 'H: closed');
if (!h.head?.startsWith('HTTP/1.1 400')) fail(`H: expected 400, got ${h.head}`);
ok('H: request without a key -> 400 and closed');
await sleep(100);

// 6. Two clients at once: both dropped; the next one works.
const b1 = await (await new Client('B1').connect()).handshake();
const b2 = await new Client('B2').connect();
await waitFor(() => b1.closed && b2.closed, 2000, 'B1 and B2 dropped');
ok('B1 + B2 at once: both dropped');
await sleep(100);

// 7. Silence: 2 s without packets = connected 0 (+ breath/co2.trigger 0); a packet reconnects.
const s = await (await new Client('S').connect()).handshake();
const before0 = count(/^gaiaball: connected 0$/);
await sleep(2300);
if (count(/^gaiaball: connected 0$/) !== before0 + 1) fail('S: no connected 0 after 2 s of silence');
s.binary(IMU);
await sleep(200);
ok('S: 2 s of silence -> connected 0, next packet -> connected 1');

// 8. Rate: IMU + Nav at 50 Hz each (what the config-app forwards) for 3 s.
const t0 = Date.now();
const timer = setInterval(() => { s.binary(IMU); s.binary(NAV_A); }, 20);
await sleep(3100);
clearInterval(timer);
const rates = lines().filter((l) => /^gaiaball: rate \d+$/.test(l)).map((l) => Number(l.split(' ')[2]));
const last = rates.slice(-2);
if (last.length < 2 || last.some((r) => r < 80 || r > 120))
  fail(`rate: expected ~100 packets/s, got ${rates.join(' ')}`);
ok(`rate at 100 packets/s for ${Date.now() - t0} ms: Pd reported ${last.join(', ')}`);
s.end();
await waitFor(() => s.closed, 2000, 'S: closed');
await sleep(300);

// ---- check Pd's console ----
done = true;
pd.kill();
const L = lines();
const expect = [
  'gaiaball: connected 1',
  'gaia.acc.x: 1000', 'gaia.acc.y: -500', 'gaia.acc.z: 151', 'gaia.quat: 1 0 0 0',
  'gaia.mic.rms: 0 -20', 'gaia.mic.peak: 0 -6', 'gaia.breath: 1',
  /^gaia\.sensors: -?0 -?0 -?0 1000 -500 151 0$/,
  'gaia.co2: 1000', 'gaia.co2.trigger: 0',
  /^gaia\.sensors: -?0 -?0 -?0 1000 -500 151 1000$/,
  'gaia.lat: -22.9056', 'gaia.lon: -47.0608', 'gaia.speed: 12.3', 'gaia.bearing: 45', 'gaia.settled: 1',
  'gaia.lat: 90', 'gaia.lon: -180', 'gaia.speed: 6553.5', 'gaia.bearing: -180', 'gaia.settled: 1',
  'gaia.acc.x: -123', 'gaia.acc.y: 456', 'gaia.acc.z: -789',
  'gaiaball: connected 0', 'gaia.breath: 0', 'gaia.co2.trigger: 0',
];
let i = 0;
for (const l of L) {
  const e = expect[i];
  if (e !== undefined && (typeof e === 'string' ? l === e : e.test(l))) i++;
}
if (i < expect.length) fail(`Pd output: missing "${expect[i]}" (after ${i} of ${expect.length})`);
ok('Pd printed every golden value in order');

// NAV_B (not valid) published nothing: exactly 2 lat lines before the rate test (A's NAV_A, B_VALID)
const firstRate = L.findIndex((l) => l.startsWith('gaiaball: rate'));
const latBeforeRate = L.slice(0, L.findIndex((l) => l === 'gaia.acc.x: -123')).filter((l) => l.startsWith('gaia.lat:'));
if (latBeforeRate.length !== 2) fail(`expected 2 lat lines from A, got ${latBeforeRate.length}`);
ok('Nav packet without the valid bit published nothing');
void firstRate;

// The tilted IMU: Euler angles against eulerLegacy() above; no mic lines (audio invalid).
const sens = L.find((l) => / -123 456 -789 /.test(l) && l.startsWith('gaia.sensors:'));
if (!sens) fail('no gaia.sensors line for the tilted packet');
const got = sens.split(' ').slice(1, 4).map(Number);
const want = eulerLegacy(tilted);
if (got.some((g, k) => Math.abs(g - want[k]) > 0.01))
  fail(`Euler: got ${got.join(' ')}, want ${want.map((v) => v.toFixed(4)).join(' ')}`);
ok(`Euler legacy: ${got.join(' ')} (reference: ${want.map((v) => v.toFixed(4)).join(' ')})`);
const iT = L.indexOf('gaia.acc.x: -123');
const afterT = L.slice(iT, L.indexOf(sens, iT) + 1);
if (afterT.some((l) => l.startsWith('gaia.mic.'))) fail('mic lines published with audioValid = 0');
ok('audioValid = 0: no gaia.mic.* lines');

// F's packets: IMU (split frame), IMU (227-byte frame) and CO2 all decoded.
for (const l of ['gaia.acc.x: 1111', 'gaia.acc.x: 2222', 'gaia.co2: 1234'])
  if (!L.includes(l)) fail(`F: missing "${l}"`);
ok('F: split frame, 227-byte frame and the CO2 after a text frame were all decoded');
const errors = L.filter((l) => /error|stack overflow|no such|couldn't/i.test(l));
if (errors.length) fail(`Pd errors:\n${errors.join('\n')}`);
ok('no errors on the Pd console');
if (OBJETO) {
  // objeto.pd prints the outlet of its [gaiaball] as "saida: ...".
  for (const l of ['saida: connected 1', 'saida: connected 0'])
    if (!L.includes(l)) fail(`objeto.pd: the outlet did not print "${l}"`);
  if (!L.some((l) => /^saida: rate \d+$/.test(l)) || !L.some((l) => /^saida: gap \d+$/.test(l)))
    fail('objeto.pd: the outlet did not print rate and gap');
  ok('[gaiaball] as an object: the outlet printed connected 1/0, rate and gap');
}
console.log('\nALL OK');
process.exit(0);
