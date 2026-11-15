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
  private isConnected = false;
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
      // Optional: an older ball simply lacks it and must still connect.
      let nav: BtCharacteristic | null = null;
      try {
        nav = await service.getCharacteristic(CHAR_NAV);
      } catch {
        nav = null;
      }
      this.subscribe(imu, 'imu', decodeImuPacket);
      this.subscribe(co2, 'co2', decodeCo2Packet);
      // The diag characteristic carries three packet types; each decoder ignores the others'.
      this.subscribe(diag, 'diag', decodeDiagPacket);
      this.subscribe(diag, 'sensors', decodeSensorsPacket);
      this.subscribe(diag, 'audio', decodeAudioChunkPacket);
      this.subscribe(diag, 'magchunk', decodeMagChunkPacket);
      await imu.startNotifications();
      await co2.startNotifications();
      await diag.startNotifications();
      if (nav) {
        this.subscribe(nav, 'nav', decodeNav);
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

  private subscribe<T>(
    ch: BtCharacteristic,
    event: string,
    decode: (dv: DataView) => T | null,
  ): void {
    ch.addEventListener('characteristicvaluechanged', () => {
      try {
        if (!ch.value) return;
        const sample = decode(ch.value);
        if (sample !== null) this.dispatchEvent(new CustomEvent(event, { detail: sample }));
      } catch {
        // Malformed packet: drop silently, never break the notification stream.
      }
    });
  }

  private cleanup(): void {
    this.isConnected = false;
    this.hasNav = false;
    this.cmdChar = null;
    this.cfgChar = null;
  }

  private emitStatus(): void {
    const detail: StatusDetail = { connected: this.isConnected, name: this.deviceName };
    this.dispatchEvent(new CustomEvent<StatusDetail>('status', { detail }));
  }
}
