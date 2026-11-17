// Automatic configuration read on connect, with retries. The panels listen to 'config' (the
// register map) and 'config-read' (what the read is doing), both dispatched on the client.
export type ConfigReadState = 'reading' | 'ok' | 'failed';
export interface ConfigReadDetail {
  state: ConfigReadState;
  /** When the successful read finished (state 'ok' only). */
  at?: Date;
}

interface AutoReadClient extends EventTarget {
  readonly connected: boolean;
  readConfig(): Promise<Map<number, DataView>>;
}

/**
 * Pauses between attempts. "3 tentativas (300, 800 e 1500 ms)" is read as three attempts, the
 * first one immediate, with a growing wait between them: attempt, 300 ms, attempt, 800 ms,
 * attempt, then give up. The 1500 ms of the original wording would only matter with a fourth
 * attempt, which is not wanted: three tries already take over a second.
 */
export const RETRY_DELAYS_MS: readonly number[] = [300, 800];

export interface AutoReadOptions {
  retryDelaysMs?: readonly number[];
  now?: () => Date;
  /** Whether there is a ball to read (default: the client is connected); the demo has none but fakes one. */
  isLive?: () => boolean;
}

const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

/**
 * Reads the configuration on every connect (and on 'config-retry'). Dispatches 'config-read'
 * with state 'reading' -> 'ok' | 'failed' and, on success, 'config' with the registers just
 * before 'ok'. A dropped link ends the run silently: the 'status' event already says so. The
 * explicit "Ler do dispositivo" button is separate and keeps working.
 */
export function attachAutoRead(client: AutoReadClient, opts: AutoReadOptions = {}): { run(): Promise<void> } {
  const delays = opts.retryDelaysMs ?? RETRY_DELAYS_MS;
  const isLive = opts.isLive ?? ((): boolean => client.connected);
  const now = opts.now ?? ((): Date => new Date());
  /** Bumped by every new run and every disconnect: a run whose number is stale must stop. */
  let generation = 0;
  const emit = (detail: ConfigReadDetail): void => {
    client.dispatchEvent(new CustomEvent<ConfigReadDetail>('config-read', { detail }));
  };

  const run = async (): Promise<void> => {
    const mine = ++generation;
    const alive = (): boolean => mine === generation && isLive();
    emit({ state: 'reading' });
    for (let attempt = 0; attempt <= delays.length; attempt++) {
      if (attempt > 0) {
        await sleep(delays[attempt - 1]);
      }
      if (!alive()) return;
      try {
        const regs = await client.readConfig();
        if (!alive()) return;
        client.dispatchEvent(new CustomEvent('config', { detail: regs }));
        emit({ state: 'ok', at: now() });
        return;
      } catch (e) {
        console.warn(`leitura automática da configuração falhou (tentativa ${attempt + 1})`, e);
      }
    }
    if (alive()) emit({ state: 'failed' });
  };

  client.addEventListener('status', (ev) => {
    if ((ev as CustomEvent<{ connected: boolean }>).detail.connected) void run();
    else generation++;
  });
  client.addEventListener('config-retry', () => { if (isLive()) void run(); });
  return { run };
}
