// Forwards the raw bytes of IMU, CO2 and nav packets to the Pd object [gaiaball] (the vanilla
// abstraction pd-gaiaball/gaiaball.pd) over a local WebSocket. forward() sits on the hot path:
// it never throws, never waits, and touches neither the DOM nor timers. No origin is
// hard-coded: [gaiaball] accepts any page.
export const PD_DEFAULT_PORT = 9478;
export const PD_RETRY_MS = 2000;
export const PD_MAX_BUFFERED = 4096;
/** IMU, CO2 and nav; diagnostic/sensor/audio/mag packets are not for Pd. */
export const PD_FORWARD_TYPES: readonly number[] = [0x01, 0x02, 0x07];
export const PD_PORT_KEY = 'gaiaball-pd-port';
export const PD_ENABLED_KEY = 'gaiaball-pd-enabled';

export type PdState = 'off' | 'waiting' | 'connected' | 'blocked';
/** `wanted`: the user's "Enviar ao Pd" switch is on (the state is 'off' while BLE is down). */
export interface PdStatus { state: PdState; rate: number; dropped: number; wanted: boolean }

export interface WsLike {
  readonly readyState: number;
  readonly bufferedAmount: number;
  binaryType: string;
  send(d: Uint8Array): void;
  close(): void;
  onopen: (() => void) | null;
  onclose: (() => void) | null;
  onerror: (() => void) | null;
}

export type LoopbackPermission = 'granted' | 'denied' | 'prompt' | 'unknown';

export interface PdBridgeDeps {
  createSocket(url: string): WsLike;
  setTimeout: typeof setTimeout;
  clearTimeout: typeof clearTimeout;
  now(): number;
  queryLoopbackPermission(): Promise<LoopbackPermission>;
}

const WS_OPEN = 1;

export class PdBridge extends EventTarget {
  private enabled = false;
  private wanted = false;
  private port = PD_DEFAULT_PORT;
  private socket: WsLike | null = null;
  private state: PdState = 'off';
  private retryTimer: ReturnType<typeof setTimeout> | null = null;
  private rateTimer: ReturnType<typeof setTimeout> | null = null;
  private sentInWindow = 0;
  private rate = 0;
  private dropped = 0;

  private readonly deps: PdBridgeDeps;

  constructor(deps: PdBridgeDeps) {
    super();
    this.deps = deps;
  }

  get status(): PdStatus {
    return { state: this.state, rate: this.rate, dropped: this.dropped, wanted: this.wanted };
  }

  /** The user's switch, independent of the BLE link (only changes the status text). */
  setWanted(on: boolean): void {
    if (on === this.wanted) return;
    this.wanted = on;
    this.emit();
  }

  setEnabled(on: boolean): void {
    if (on === this.enabled) return;
    this.enabled = on;
    if (on) this.connect();
    else this.shutdown();
  }

  setPort(port: number): void {
    if (port === this.port) return;
    this.port = port;
    if (!this.enabled) return;
    this.teardown();
    this.connect();
  }

  forward(bytes: Uint8Array): void {
    if (!this.enabled || bytes.length < 2 || !PD_FORWARD_TYPES.includes(bytes[1])) return;
    try {
      const s = this.socket;
      if (!s || s.readyState !== WS_OPEN || s.bufferedAmount > PD_MAX_BUFFERED) {
        this.dropped++;
        return;
      }
      s.send(bytes);
      this.sentInWindow++;
    } catch {
      this.dropped++;
    }
  }

  private set(state: PdState): void {
    if (state === this.state) return;
    this.state = state;
    this.emit();
  }

  private emit(): void {
    this.dispatchEvent(new CustomEvent<PdStatus>('status', { detail: this.status }));
  }

  private connect(): void {
    if (!this.enabled) return;
    if (this.state !== 'blocked') this.set('waiting');
    let sock: WsLike;
    try {
      sock = this.deps.createSocket(`ws://127.0.0.1:${this.port}`);
    } catch {
      this.scheduleRetry();
      return;
    }
    sock.binaryType = 'arraybuffer';
    this.socket = sock;
    sock.onopen = () => {
      if (this.socket !== sock) return;
      this.set('connected');
      this.startRate();
    };
    const lost = (): void => {
      if (this.socket !== sock) return;
      this.socket = null;
      this.stopRate();
      if (this.state !== 'blocked') this.set('waiting');
      void this.checkPermission();
      this.scheduleRetry();
    };
    sock.onclose = lost;
    sock.onerror = lost;
  }

  private async checkPermission(): Promise<void> {
    try {
      const p = await this.deps.queryLoopbackPermission();
      if (!this.enabled || this.state === 'connected') return;
      if (p === 'denied') this.set('blocked');
      else if (this.state === 'blocked') this.set('waiting');
    } catch {
      // 'unknown' behaves like granted: keep retrying.
    }
  }

  private scheduleRetry(): void {
    if (!this.enabled || this.retryTimer !== null) return;
    this.retryTimer = this.deps.setTimeout(() => {
      this.retryTimer = null;
      this.connect();
    }, PD_RETRY_MS);
  }

  private startRate(): void {
    this.stopRate();
    this.sentInWindow = 0;
    const tick = (): void => {
      this.rate = this.sentInWindow;
      this.sentInWindow = 0;
      this.emit();
      this.rateTimer = this.deps.setTimeout(tick, 1000);
    };
    this.rateTimer = this.deps.setTimeout(tick, 1000);
  }

  private stopRate(): void {
    if (this.rateTimer !== null) this.deps.clearTimeout(this.rateTimer);
    this.rateTimer = null;
    this.rate = 0;
  }

  /** Drops the socket and timers without changing the enabled flag. */
  private teardown(): void {
    if (this.retryTimer !== null) this.deps.clearTimeout(this.retryTimer);
    this.retryTimer = null;
    this.stopRate();
    const s = this.socket;
    this.socket = null;
    if (s) {
      try { s.close(); } catch { /* already closed */ }
    }
  }

  private shutdown(): void {
    this.teardown();
    this.state = 'off';
    this.emit();
  }
}

/** Real browser wiring. Loopback needs the Chrome "Apps neste dispositivo" permission. */
export function browserPdDeps(): PdBridgeDeps {
  return {
    createSocket: (url) => new WebSocket(url) as unknown as WsLike,
    setTimeout: globalThis.setTimeout.bind(globalThis) as typeof setTimeout,
    clearTimeout: globalThis.clearTimeout.bind(globalThis) as typeof clearTimeout,
    now: () => Date.now(),
    queryLoopbackPermission: async () => {
      for (const name of ['loopback-network', 'local-network-access']) {
        try {
          const r = await navigator.permissions.query({ name: name as PermissionName });
          return r.state as LoopbackPermission;
        } catch {
          // Name not supported by this browser: try the next one.
        }
      }
      return 'unknown';
    },
  };
}
