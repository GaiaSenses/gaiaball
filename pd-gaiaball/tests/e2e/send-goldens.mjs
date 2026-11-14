// End-to-end check of [gaiaball] without a ball or a browser: a raw TCP client (node:net)
// does the WebSocket handshake the way Chrome does (with an Origin header) and sends the
// golden packets as masked binary frames.
//
// Pd started by hand (checks the WebSocket side; read Pd's console for the channels):
//   pd -nogui -stderr -path pd-gaiaball -open pd-gaiaball/tests/e2e/listen.pd &
//   node pd-gaiaball/tests/e2e/send-goldens.mjs
//
// Or let the script start Pd and also check what listen.pd prints on Pd's stderr:
//   node pd-gaiaball/tests/e2e/send-goldens.mjs --spawn pd -nogui -stderr \
//     -path pd-gaiaball -open pd-gaiaball/tests/e2e/listen.pd
//
// Options (before --spawn): --port N (default 9478).
import { spawn } from 'node:child_process';
import { createHash, randomBytes } from 'node:crypto';
import net from 'node:net';

const args = process.argv.slice(2);
const spawnAt = args.indexOf('--spawn');
const own = spawnAt < 0 ? args : args.slice(0, spawnAt);
const spawnCmd = spawnAt < 0 ? null : args.slice(spawnAt + 1);
const portAt = own.indexOf('--port');
const PORT = portAt < 0 ? 9478 : Number(own[portAt + 1]);

const hex = (s) => Buffer.from(s.trim().split(/\s+/).map((b) => parseInt(b, 16)));
// firmware/docs/protocol.md and config-app/src/protocol/packets.test.ts
const IMU = hex('02 01 02 01 0D 0C 0B 0A 00 40 00 00 00 00 00 00 E8 03 0C FE 97 00 1B F0 C8 F0 E4');
const CO2 = hex('01 02 02 01 0D 0C 0B 0A E8 03 05 07');
const NAV_A = hex('01 07 02 01 4E 61 BC 00 01 03 B8 E3 58 F2 36 16 F3 E3 94 11 7B 00');

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

async function waitFor(pred, ms, what) {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (pred()) return;
    await sleep(10);
  }
  fail(`timed out waiting for ${what}`);
}

function frame(opcode, payload = Buffer.alloc(0)) {
  const n = payload.length;
  if (n > 125) throw new Error('test frames are short');
  const mask = randomBytes(4);
  const body = Buffer.from(payload.map((b, i) => b ^ mask[i & 3]));
  return Buffer.concat([Buffer.from([0x80 | opcode, 0x80 | n]), mask, body]);
}

/** One raw WebSocket client: handshake, then server frames parsed into `frames`. */
class Client {
  constructor(name) {
    this.name = name;
    this.buf = Buffer.alloc(0);
    this.head = null;
    this.frames = [];
    this.closed = false;
  }

  async connect({ retryMs = 0 } = {}) {
    const end = Date.now() + retryMs;
    for (;;) {
      try {
        this.sock = await new Promise((resolve, reject) => {
          const s = net.connect(PORT, '127.0.0.1');
          s.once('connect', () => resolve(s));
          s.once('error', reject);
        });
        break;
      } catch (e) {
        if (Date.now() > end) fail(`${this.name}: cannot connect to 127.0.0.1:${PORT} (${e.code})`);
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
    if (!this.head) {
      const end = this.buf.indexOf('\r\n\r\n');
      if (end < 0) return;
      this.head = this.buf.subarray(0, end).toString('latin1');
      this.buf = this.buf.subarray(end + 4);
    }
    while (this.buf.length >= 2) {
      const op = this.buf[0] & 0x0f;
      let n = this.buf[1] & 0x7f;
      let h = 2;
      if (this.buf[1] & 0x80) fail(`${this.name}: server frame is masked`);
      if (n === 126) {
        if (this.buf.length < 4) return;
        n = this.buf.readUInt16BE(2);
        h = 4;
      } else if (n === 127) fail(`${this.name}: unexpected 64-bit frame`);
      if (this.buf.length < h + n) return;
      this.frames.push({ op, payload: Buffer.from(this.buf.subarray(h, h + n)) });
      this.buf = this.buf.subarray(h + n);
    }
  }

  async handshake() {
    const key = randomBytes(16).toString('base64');
    this.sock.write(
      `GET / HTTP/1.1\r\nHost: 127.0.0.1:${PORT}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n` +
      `Origin: http://localhost\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${key}\r\n\r\n`);
    await waitFor(() => this.head || this.closed, 2000, `${this.name}: 101`);
    if (!this.head) fail(`${this.name}: closed before the handshake`);
    const want = createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    if (!/^HTTP\/1\.1 101 /.test(this.head)) fail(`${this.name}: expected 101, got ${this.head.split('\r\n')[0]}`);
    const got = /\r\nSec-WebSocket-Accept: *(\S+)/i.exec(this.head)?.[1];
    if (got !== want) fail(`${this.name}: Sec-WebSocket-Accept ${got} != ${want}`);
    return this;
  }

  send(op, payload) { this.sock.write(frame(op, payload)); }
  binary(p) { this.send(0x2, p); }

  closeFrame() { return this.frames.find((f) => f.op === 0x8); }
  closeCode() {
    const f = this.closeFrame();
    return f && f.payload.length >= 2 ? f.payload.readUInt16BE(0) : null;
  }
}

if (spawnCmd) {
  if (!spawnCmd.length) fail('--spawn needs a command');
  pd = spawn(spawnCmd[0], spawnCmd.slice(1), { stdio: ['ignore', 'ignore', 'pipe'] });
  pd.stderr.on('data', (d) => { pdErr += d; });
  pd.on('error', (e) => fail(`cannot start ${spawnCmd[0]}: ${e.message}`));
}

// 1. Plain HTTP (not a WebSocket): 400, nothing else changes.
{
  const c = await new Client('http').connect({ retryMs: spawnCmd ? 10000 : 0 });
  c.sock.write(`GET / HTTP/1.1\r\nHost: 127.0.0.1:${PORT}\r\n\r\n`);
  await waitFor(() => c.closed, 2000, 'http: server closes');
  if (!/^HTTP\/1\.1 400 /.test(c.head ?? '')) fail(`http: expected 400, got ${c.head}`);
  ok('a plain HTTP request gets 400 and is closed');
}

// 2. First client: every golden, then a ping.
const a = await (await new Client('A').connect()).handshake();
ok('A: handshake with Origin: http://localhost accepted (101, correct accept key)');
a.binary(IMU); // gaia.sensors carries CO2 = 0: no CO2 packet yet
a.binary(CO2);
a.binary(IMU);
a.binary(NAV_A);
a.send(0x9, Buffer.from('gaia'));
await waitFor(() => a.frames.some((f) => f.op === 0xa), 2000, 'A: pong');
if (a.frames.find((f) => f.op === 0xa).payload.toString() !== 'gaia') fail('A: pong payload differs');
ok('A: IMU, CO2, IMU, Nav A sent; ping answered with pong');

// 3. Second client replaces the first.
const b = await (await new Client('B').connect()).handshake();
await waitFor(() => a.closed, 2000, 'A: closed by the server after B connected');
ok(`B: connected; A was closed by the server (close code ${a.closeCode()})`);
b.binary(IMU);

// 4. Silence: 2 s without packets = disconnected (connected 0, gaia.breath 0), socket kept.
await sleep(2600);
if (b.closed) fail('B: the server closed the socket on silence (it should only report it)');
b.binary(IMU); // packets again: connected 1
await sleep(200);
ok('B: 2.6 s of silence, then one more IMU on the same socket');

// 5. Clean close: the server echoes the close code.
b.send(0x8, Buffer.from([0x03, 0xe8])); // 1000
await waitFor(() => b.closed, 2000, 'B: server closes after the close frame');
if (b.closeCode() !== 1000) fail(`B: expected the close code 1000 echoed, got ${b.closeCode()}`);
ok('B: close 1000 echoed, socket closed');

// 6. Malformed: a text frame is refused with 1003 and the object keeps working.
const c = await (await new Client('C').connect()).handshake();
c.send(0x1, Buffer.from('{"json":true}'));
await waitFor(() => c.closed, 2000, 'C: server closes after a text frame');
if (c.closeCode() !== 1003) fail(`C: expected close 1003, got ${c.closeCode()}`);
ok('C: text frame refused with close 1003');

const d = await (await new Client('D').connect()).handshake();
d.binary(IMU);
await sleep(200);
d.sock.end();
await waitFor(() => d.closed, 2000, 'D: closed');
ok('D: works after the malformed client; closed without a close frame');

// 7. Pd's side: the reset after closing.
await sleep(2200);

if (!pd) {
  console.log(`
WebSocket side OK. In Pd's console (stderr) you should now see, in this order:
  gaiaball: connected 1
  gaia.acc.x: 1000 / gaia.acc.y: -500 / gaia.acc.z: 151 / gaia.quat: 1 0 0 0
  gaia.mic.rms: 0 -20 / gaia.mic.peak: 0 -6 / gaia.breath: 1
  gaia.sensors: 0 0 0 1000 -500 151 0   (CO2 = 0 before the first CO2 packet)
  gaia.co2: 1000 / gaia.co2.trigger: 0
  gaia.sensors: 0 0 0 1000 -500 151 1000
  gaia.lat: -22.9056 / gaia.lon: -47.0608 / gaia.speed: 12.3 / gaia.bearing: 45 / gaia.settled: 1
  gaiaball: rate N / gaiaball: gap N (every second while connected)
  gaiaball: connected 0 + gaia.breath: 0 + gaia.co2.trigger: 0 (B's silence), connected 1 again,
  and the same 0s after each close; no "connected 0" when B replaced A.`);
  process.exit(0);
}

pd.kill();
await new Promise((r) => (pd.exitCode !== null || pd.signalCode !== null ? r() : pd.once('exit', r)));
const lines = pdErr.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
const expect = [
  'gaiaball: connected 1', // A
  'gaia.acc.x: 1000', 'gaia.acc.y: -500', 'gaia.acc.z: 151', 'gaia.quat: 1 0 0 0',
  'gaia.mic.rms: 0 -20', 'gaia.mic.peak: 0 -6', 'gaia.breath: 1',
  /^gaia\.sensors: -?0 -?0 -?0 1000 -500 151 0$/,
  'gaia.co2: 1000', 'gaia.co2.trigger: 0',
  /^gaia\.sensors: -?0 -?0 -?0 1000 -500 151 1000$/,
  'gaia.lat: -22.9056', 'gaia.lon: -47.0608', 'gaia.speed: 12.3', 'gaia.bearing: 45', 'gaia.settled: 1',
  'gaia.acc.x: 1000', // B, after replacing A
  /^gaiaball: rate \d+$/, /^gaiaball: gap \d+$/, // 1 s after A's handshake, while B is silent
  'gaiaball: connected 0', 'gaia.breath: 0', 'gaia.co2.trigger: 0', // B silent for 2 s
  'gaiaball: connected 1', 'gaia.breath: 1', // B again
  'gaiaball: connected 0', 'gaia.breath: 0', // B closed
  'gaiaball: connected 1', 'gaiaball: connected 0', 'gaia.breath: 0', // C refused
  'gaiaball: connected 1', 'gaia.acc.x: 1000', 'gaiaball: connected 0', 'gaia.breath: 0', // D
];
let i = 0;
for (const l of lines) {
  const e = expect[i];
  if (e !== undefined && (typeof e === 'string' ? l === e : e.test(l))) i++;
}
if (i < expect.length) fail(`Pd output: missing "${expect[i]}" (after ${i} of ${expect.length} expected lines)`);
ok('Pd printed every expected channel value, in order');
const count = (s) => lines.filter((l) => l === s).length;
if (count('gaiaball: connected 1') !== 4 || count('gaiaball: connected 0') !== 4)
  fail(`expected 4x connected 1 and 4x connected 0 (no blip when B replaced A), got ` +
    `${count('gaiaball: connected 1')} and ${count('gaiaball: connected 0')}`);
ok('status: 4x connected 1 / 4x connected 0 (B replacing A did not disconnect)');
if (!lines.some((l) => /gaiaball: porta \d+ em uso/.test(l))) fail('the second [gaiaball] did not report its port in use');
ok('the second [gaiaball] on the same port reported "porta ... em uso"');
console.log('\nALL OK');
