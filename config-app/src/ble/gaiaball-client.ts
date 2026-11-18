// Thin Web Bluetooth client over the tested codecs. Protocol: firmware/docs/protocol.md.
import {
  decodeAudioChunkPacket,
  decodeCo2Packet,
  decodeDiagPacket,
  decodeMagChunkPacket,
  decodeImuPacket,
  decodeNav,
  decodeSensorsPacket,
} from '../protocol/packets.ts';
import { buildCommand, decodeTlv, encodeTlv, type TlvWrite } from '../protocol/tlv.ts';
import { lockStateFrom, type LockState } from './unlock-state.ts';

export const SERVICE_UUID = '19b10000-e8f2-537e-4f6c-d104768a1214';
export const CHAR_IMU = '19b10001-e8f2-537e-4f6c-d104768a1214';
export const CHAR_COMMAND = '19b10002-e8f2-537e-4f6c-d104768a1214';
export const CHAR_CO2 = '19b10003-e8f2-537e-4f6c-d104768a1214';
export const CHAR_CONFIG = '19b10004-e8f2-537e-4f6c-d104768a1214';
export const CHAR_DIAG = '19b10005-e8f2-537e-4f6c-d104768a1214';
/** Globe navigation stream. Optional: firmware older than the navigation feature does not expose it. */
export const CHAR_NAV = '19b10006-e8f2-537e-4f6c-d104768a1214';

export const CMD_UNLOCK = 0x20;

/** Setup attempts per Conectar, and the pause before the next one (x attempt number). */
const CONNECT_ATTEMPTS = 3;
const CONNECT_RETRY_MS = 500;

/** An operation without a link (it dropped meanwhile): says what to do next, since the unlock went with it. */
const NOT_CONNECTED =
  'Não conectado. Conecte a bola de novo e, para mudar algo, desbloqueie com o PIN outra vez: o desbloqueio vale só para a conexão atual.';

/** Attempts per GATT operation, and the pause before the next one (x attempt number). */
const OP_ATTEMPTS = 3;
const OP_RETRY_MS = 250;

const errName = (err: unknown): unknown => (typeof err === 'object' && err !== null ? (err as { name?: unknown }).name : undefined);
const errText = (err: unknown): string => (typeof err === 'object' && err !== null ? String((err as { message?: unknown }).message ?? '') : '');

/** Chrome reports a link that dropped (or never came up) as a NetworkError DOMException. */
function isLinkDrop(err: unknown): boolean {
  return errName(err) === 'NetworkError';
}

/** The Windows hiccup on a healthy link (Chrome's GATT_UNKNOWN_FAILURE, a NotSupportedError). */
function isTransientGattFailure(err: unknown): boolean {
  return errText(err).includes('GATT operation failed for unknown reason');
}

// Minimal Web Bluetooth typings (not part of lib.dom).
interface BtCharacteristic extends EventTarget {
  readonly value: DataView | undefined;
  startNotifications(): Promise<BtCharacteristic>;
  stopNotifications(): Promise<unknown>;
  readValue(): Promise<DataView>;
  writeValue(value: Uint8Array): Promise<void>;
}
interface BtService {
  getCharacteristic(uuid: string): Promise<BtCharacteristic>;
}
interface BtServer {
  readonly connected?: boolean;
  connect(): Promise<BtServer>;
  disconnect(): void;
  getPrimaryService(uuid: string): Promise<BtService>;
}
interface BtDevice extends EventTarget {
  readonly name?: string;
  readonly gatt?: BtServer;
}
interface BtApi {
  requestDevice(options: { filters: { services: string[] }[] }): Promise<BtDevice>;
}

export type IngestSource = 'imu' | 'co2' | 'nav' | 'diag';

/** Which events each source feeds. The diag characteristic carries several packet types; each decoder ignores the others'. */
const DECODERS: Record<IngestSource, readonly (readonly [string, (dv: DataView) => unknown])[]> = {
  imu: [['imu', decodeImuPacket]],
  co2: [['co2', decodeCo2Packet]],
  nav: [['nav', decodeNav]],
  diag: [
    ['diag', decodeDiagPacket],
    ['sensors', decodeSensorsPacket],
    ['audio', decodeAudioChunkPacket],
    ['magchunk', decodeMagChunkPacket],
  ],
};
/** Sources whose raw bytes go to the sink (the Pd bridge). */
const RAW_SOURCES: ReadonlySet<IngestSource> = new Set<IngestSource>(['imu', 'co2', 'nav']);

export interface StatusDetail {
  connected: boolean;
  name: string | null;
}

function bluetoothApi(): BtApi | undefined {
  return (navigator as Navigator & { bluetooth?: BtApi }).bluetooth;
}

export class GaiaballClient extends EventTarget {
  private device: BtDevice | null = null;
  private cmdChar: BtCharacteristic | null = null;
  private cfgChar: BtCharacteristic | null = null;
  private diagChar: BtCharacteristic | null = null;
  private rawSink: ((bytes: Uint8Array) => void) | null = null;
  private isConnected = false;
  /** Characteristics already listened to, so a retry or reconnect never doubles the stream. */
  private readonly subscribed = new WeakSet<BtCharacteristic>();
  /** Tail of the GATT operation chain: the link handles one operation at a time. */
  private gattQueue: Promise<unknown> = Promise.resolve();
  private readonly onDisconnected = (): void => {
    this.cleanup();
    this.emitStatus();
  };

  get connected(): boolean {
    return this.isConnected;
  }

  get deviceName(): string | null {
    return this.device?.name ?? null;
  }

  /**
   * Runs one GATT operation (read, write, start/stop notifications) after every earlier one
   * has settled. Web Bluetooth fails or misbehaves with two operations in flight, and several
   * callers can overlap (automatic config read, stage mode pausing the diag stream, the stage
   * bar). A failing operation rejects only its own caller; the chain always moves on.
   */
  private gatt<T>(op: () => Promise<T>): Promise<T> {
    const run = this.gattQueue.then(() => this.withRetry(op));
    this.gattQueue = run.then(() => undefined, () => undefined);
    return run;
  }

  /**
   * Chrome on Windows fails about 0.6% of reads and writes with "GATT operation failed for unknown
   * reason." while the ball's notifications flow (measured: 4 of 642 with the streams on, 0 of 654
   * without), and the link stays up: the same operation succeeds right after. Every operation that
   * goes through gatt() can be repeated (config read/write, notifications on/off, the bench
   * commands, the PIN), so a transient failure is retried before the caller hears of it. The one
   * cost: if the failed write had in fact reached the ball, a WRONG PIN counts twice toward the
   * 5-failure lockout.
   */
  private async withRetry<T>(op: () => Promise<T>): Promise<T> {
    for (let attempt = 1; ; attempt++) {
      try {
        return await op();
      } catch (err) {
        if (attempt >= OP_ATTEMPTS || !isTransientGattFailure(err) || !this.device?.gatt?.connected) throw err;
        await new Promise((r) => setTimeout(r, OP_RETRY_MS * attempt));
      }
    }
  }

  async connect(): Promise<void> {
    const bt = bluetoothApi();
    if (!bt) {
      throw new Error(
        'Web Bluetooth indisponível. Use Chrome ou Edge em localhost ou HTTPS.',
      );
    }
    const device = await bt.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
    // Chrome hands back the same device object every time, and a link that dropped on its own
    // (reboot, range) left the listener on it: it must not fire during this setup.
    device.removeEventListener('gattserverdisconnected', this.onDisconnected);
    this.device = device;
    try {
      const gatt = device.gatt;
      if (!gatt) throw new Error('Dispositivo sem servidor GATT.');
      for (let attempt = 1; ; attempt++) {
        try {
          await this.setupLink(gatt);
          break;
        } catch (err) {
          // Windows keeps the previous link up for a few seconds after a disconnect: a connect in
          // that window lands on the dying link, which then drops and fails the setup with a
          // NetworkError. The next attempt gets a fresh link (measured: 2 s after a disconnect,
          // the first attempt fails ~1-3 s later and the second connects). The Windows hiccup
          // that withRetry() absorbs can also hit discovery, which is not queued: retried here.
          if (attempt >= CONNECT_ATTEMPTS || !(isLinkDrop(err) || isTransientGattFailure(err))) throw err;
          this.cleanup();
          await new Promise((r) => setTimeout(r, CONNECT_RETRY_MS * attempt));
        }
      }
      // Watched only from here: a drop during setup is the retry loop's business, not a status.
      device.addEventListener('gattserverdisconnected', this.onDisconnected);
      if (!gatt.connected) throw new DOMException('GATT Server is disconnected.', 'NetworkError');
      this.isConnected = true;
      this.emitStatus();
    } catch (err) {
      this.disconnect();
      throw err;
    }
  }

  /** One attempt: GATT connect, discovery and notifications. Throws if the link drops meanwhile. */
  private async setupLink(gatt: BtServer): Promise<void> {
    const server = await gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    const [imu, cmd, co2, cfg, diag, nav] = await Promise.all([
      service.getCharacteristic(CHAR_IMU),
      service.getCharacteristic(CHAR_COMMAND),
      service.getCharacteristic(CHAR_CO2),
      service.getCharacteristic(CHAR_CONFIG),
      service.getCharacteristic(CHAR_DIAG),
      service.getCharacteristic(CHAR_NAV),
    ]);
    this.cmdChar = cmd;
    this.cfgChar = cfg;
    this.diagChar = diag;
    this.subscribe(imu, 'imu');
    this.subscribe(co2, 'co2');
    this.subscribe(diag, 'diag');
    this.subscribe(nav, 'nav');
    await this.gatt(() => imu.startNotifications());
    await this.gatt(() => co2.startNotifications());
    await this.gatt(() => diag.startNotifications());
    await this.gatt(() => nav.startNotifications());
  }

  disconnect(): void {
    const device = this.device;
    if (device) {
      device.removeEventListener('gattserverdisconnected', this.onDisconnected);
      if (device.gatt) device.gatt.disconnect();
    }
    const wasConnected = this.isConnected;
    this.cleanup();
    if (wasConnected) this.emitStatus();
  }

  /**
   * Sends the PIN, then reads the configuration back: the read-only TLV 0x37 reports whether
   * this connection is now unlocked.
   * Firmware lockout: 5 straight failures -> 30 s refusal ('locked-out').
   */
  async unlock(pin: string): Promise<LockState> {
    if (!/^\d{4}$/.test(pin)) throw new Error('O PIN deve ter 4 dígitos.');
    await this.sendCommand(CMD_UNLOCK, new TextEncoder().encode(pin));
    return lockStateFrom(await this.readConfig());
  }

  async sendCommand(opcode: number, arg?: Uint8Array): Promise<void> {
    await this.gatt(() => this.requireChar(this.cmdChar).writeValue(buildCommand(opcode, arg)));
  }

  async readConfig(): Promise<Map<number, DataView>> {
    return this.gatt(async () => decodeTlv(await this.requireChar(this.cfgChar).readValue()));
  }

  /** Writes only; the firmware cannot report rejections, so the caller must read back. */
  async writeConfig(regs: TlvWrite[]): Promise<void> {
    await this.gatt(() => this.requireChar(this.cfgChar).writeValue(encodeTlv(regs)));
  }

  private requireChar(c: BtCharacteristic | null): BtCharacteristic {
    if (!c || !this.isConnected) throw new Error(NOT_CONNECTED);
    return c;
  }

  /** Pauses the diagnostic stream (heavy audio/mag chunks) without touching the orientation streams. */
  async pauseDiag(): Promise<void> {
    await this.gatt(async () => {
      if (this.diagChar && this.isConnected) await this.diagChar.stopNotifications();
    });
  }

  async resumeDiag(): Promise<void> {
    await this.gatt(async () => {
      if (this.diagChar && this.isConnected) await this.diagChar.startNotifications();
    });
  }

  /** Receives a copy of the raw bytes of every IMU, CO2 and nav packet, before anything is decoded. */
  setRawSink(fn: ((bytes: Uint8Array) => void) | null): void {
    this.rawSink = fn;
  }

  /**
   * The single entry for every notification (real or demo): raw bytes first (so Pd never waits
   * for decoding or drawing), then decode and dispatch.
   */
  ingest(source: IngestSource, dv: DataView): void {
    if (this.rawSink && RAW_SOURCES.has(source)) {
      try {
        this.rawSink(new Uint8Array(dv.buffer.slice(dv.byteOffset, dv.byteOffset + dv.byteLength)));
      } catch {
        // The sink must never break the UI path.
      }
    }
    try {
      for (const [event, decode] of DECODERS[source]) {
        const sample = decode(dv);
        if (sample !== null) this.dispatchEvent(new CustomEvent(event, { detail: sample }));
      }
    } catch {
      // Malformed packet: drop silently, never break the notification stream.
    }
  }

  private subscribe(ch: BtCharacteristic, source: IngestSource): void {
    // Chrome may hand back the same characteristic object on a later attempt or connection.
    if (this.subscribed.has(ch)) return;
    this.subscribed.add(ch);
    ch.addEventListener('characteristicvaluechanged', () => {
      if (ch.value) this.ingest(source, ch.value);
    });
  }

  private cleanup(): void {
    // An operation in flight on a dead link may never settle; the next connection starts a fresh chain.
    this.gattQueue = Promise.resolve();
    this.isConnected = false;
    this.cmdChar = null;
    this.cfgChar = null;
    this.diagChar = null;
  }

  private emitStatus(): void {
    const detail: StatusDetail = { connected: this.isConnected, name: this.deviceName };
    this.dispatchEvent(new CustomEvent<StatusDetail>('status', { detail }));
  }
}
