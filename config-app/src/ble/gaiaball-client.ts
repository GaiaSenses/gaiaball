// Thin Web Bluetooth client over the tested codecs. Protocol: firmware/docs/protocol.md.
import {
  decodeAudioChunkPacket,
  decodeCo2Packet,
  decodeDiagPacket,
  decodeMagChunkPacket,
  decodeImuPacket,
  decodeNav,
  decodeSensorsPacket,
  isLegacyImuPacket,
} from '../protocol/packets.ts';
import { buildCommand, decodeTlv, encodeTlv, type TlvWrite } from '../protocol/tlv.ts';

export const SERVICE_UUID = '19b10000-e8f2-537e-4f6c-d104768a1214';
export const CHAR_IMU = '19b10001-e8f2-537e-4f6c-d104768a1214';
export const CHAR_COMMAND = '19b10002-e8f2-537e-4f6c-d104768a1214';
export const CHAR_CO2 = '19b10003-e8f2-537e-4f6c-d104768a1214';
export const CHAR_CONFIG = '19b10004-e8f2-537e-4f6c-d104768a1214';
export const CHAR_DIAG = '19b10005-e8f2-537e-4f6c-d104768a1214';
/** Globe navigation stream. Optional: firmware older than the navigation feature does not expose it. */
export const CHAR_NAV = '19b10006-e8f2-537e-4f6c-d104768a1214';

export const CMD_UNLOCK = 0x20;

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
  private outdatedReported = false;
  private hasNav = false;
  private readonly onDisconnected = (): void => {
    this.cleanup();
    this.emitStatus();
  };

  get connected(): boolean {
    return this.isConnected;
  }

  /** True when the connected ball exposes the navigation characteristic (newer firmware). */
  get navAvailable(): boolean {
    return this.hasNav;
  }

  get deviceName(): string | null {
    return this.device?.name ?? null;
  }

  async connect(): Promise<void> {
    const bt = bluetoothApi();
    if (!bt) {
      throw new Error(
        'Web Bluetooth indisponível. Use Chrome ou Edge em localhost ou HTTPS.',
      );
    }
    const device = await bt.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
    device.addEventListener('gattserverdisconnected', this.onDisconnected);
    this.device = device;
    try {
      if (!device.gatt) throw new Error('Dispositivo sem servidor GATT.');
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(SERVICE_UUID);
      const [imu, cmd, co2, cfg, diag] = await Promise.all([
        service.getCharacteristic(CHAR_IMU),
        service.getCharacteristic(CHAR_COMMAND),
        service.getCharacteristic(CHAR_CO2),
        service.getCharacteristic(CHAR_CONFIG),
        service.getCharacteristic(CHAR_DIAG),
      ]);
      this.cmdChar = cmd;
      this.cfgChar = cfg;
      this.diagChar = diag;
      // Optional: an older ball simply lacks it and must still connect.
      let nav: BtCharacteristic | null = null;
      try {
        nav = await service.getCharacteristic(CHAR_NAV);
      } catch {
        nav = null;
      }
      this.subscribe(imu, 'imu');
      this.subscribe(co2, 'co2');
      this.subscribe(diag, 'diag');
      await imu.startNotifications();
      await co2.startNotifications();
      await diag.startNotifications();
      if (nav) {
        this.subscribe(nav, 'nav');
        try {
          await nav.startNotifications();
          this.hasNav = true;
        } catch {
          this.hasNav = false;
        }
      }
      this.isConnected = true;
      this.emitStatus();
    } catch (err) {
      this.disconnect();
      throw err;
    }
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
   * Sends the PIN. The firmware does not ACK: callers must treat this as optimistic;
   * with a wrong PIN, sensitive commands and config writes are silently ignored.
   * Firmware lockout: 5 straight failures -> 30 s refusal.
   */
  async unlock(pin: string): Promise<void> {
    if (!/^\d{4}$/.test(pin)) throw new Error('O PIN deve ter 4 dígitos.');
    await this.sendCommand(CMD_UNLOCK, new TextEncoder().encode(pin));
  }

  async sendCommand(opcode: number, arg?: Uint8Array): Promise<void> {
    await this.requireChar(this.cmdChar).writeValue(buildCommand(opcode, arg));
  }

  async readConfig(): Promise<Map<number, DataView>> {
    return decodeTlv(await this.requireChar(this.cfgChar).readValue());
  }

  /** Writes only; the firmware cannot report rejections, so the caller must read back. */
  async writeConfig(regs: TlvWrite[]): Promise<void> {
    await this.requireChar(this.cfgChar).writeValue(encodeTlv(regs));
  }

  private requireChar(c: BtCharacteristic | null): BtCharacteristic {
    if (!c || !this.isConnected) throw new Error('Não conectado.');
    return c;
  }

  /** Pauses the diagnostic stream (heavy audio/mag chunks) without touching the orientation streams. */
  async pauseDiag(): Promise<void> {
    if (this.diagChar && this.isConnected) await this.diagChar.stopNotifications();
  }

  async resumeDiag(): Promise<void> {
    if (this.diagChar && this.isConnected) await this.diagChar.startNotifications();
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
      let decoded = false;
      for (const [event, decode] of DECODERS[source]) {
        const sample = decode(dv);
        if (sample !== null) {
          decoded = true;
          this.dispatchEvent(new CustomEvent(event, { detail: sample }));
        }
      }
      if (!decoded && source === 'imu' && !this.outdatedReported && isLegacyImuPacket(dv)) {
        this.outdatedReported = true;
        this.dispatchEvent(new CustomEvent('firmware-outdated'));
      }
    } catch {
      // Malformed packet: drop silently, never break the notification stream.
    }
  }

  private subscribe(ch: BtCharacteristic, source: IngestSource): void {
    ch.addEventListener('characteristicvaluechanged', () => {
      if (ch.value) this.ingest(source, ch.value);
    });
  }

  private cleanup(): void {
    this.isConnected = false;
    this.outdatedReported = false;
    this.hasNav = false;
    this.cmdChar = null;
    this.cfgChar = null;
    this.diagChar = null;
  }

  private emitStatus(): void {
    const detail: StatusDetail = { connected: this.isConnected, name: this.deviceName };
    this.dispatchEvent(new CustomEvent<StatusDetail>('status', { detail }));
  }
}
