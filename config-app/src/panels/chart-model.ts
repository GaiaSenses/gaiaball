// Pure chart logic for the sensors panel. Kept free of theme.ts (it touches `window`
// at import) so it can be unit-tested in plain node.

export const WINDOW_OPTIONS_S = [5, 8, 12] as const;
export type WindowS = (typeof WINDOW_OPTIONS_S)[number];
export const DEFAULT_WINDOW_S: WindowS = 12;
export const WINDOW_STORAGE_KEY = 'gaiaball-chart-window';

/** Fixed Y axis ranges (CO2 depends on the configured virtual peak: see co2Range). */
export const Y_RANGES = {
  orientation: [-180, 180],
  accel: [-4000, 4000],
  micRms: [-120, 0],
  micPeak: [-120, 0],
  breath: [-0.1, 1.1],
  pot: [0, 100],
} as const;

export function co2Range(virtualPeakPpm: number): [number, number] {
  return [0, Math.max(5000, virtualPeakPpm)];
}

/** Sparkline range: rate 0..1.2 x configured notify Hz, loss 0..100 %. */
export function sparkRange(kind: 'rate' | 'loss', configuredHz: number): [number, number] {
  return kind === 'rate' ? [0, 1.2 * configuredHz] : [0, 100];
}

export function loadWindowS(storage: Pick<Storage, 'getItem'> | null): WindowS {
  try {
    const n = Number(storage?.getItem(WINDOW_STORAGE_KEY));
    return (WINDOW_OPTIONS_S as readonly number[]).includes(n) ? (n as WindowS) : DEFAULT_WINDOW_S;
  } catch {
    return DEFAULT_WINDOW_S;
  }
}

export function saveWindowS(storage: Pick<Storage, 'setItem'> | null, w: WindowS): void {
  try {
    storage?.setItem(WINDOW_STORAGE_KEY, String(w));
  } catch {
    /* storage blocked: the choice just is not remembered */
  }
}

/** Packets received between two chart ticks, each with its arrival time (s). */
export class SampleQueue<T> {
  private items: { t: number; v: T }[] = [];
  push(t: number, v: T): void { this.items.push({ t, v }); }
  drain(): { t: number; v: T }[] {
    const out = this.items;
    this.items = [];
    return out;
  }
  clear(): void { this.items = []; }
}

/** Which rows draw the threshold line: only the rows of the active metric. */
export function thresholdRows(metricPeak: boolean): { rms: boolean; peak: boolean } {
  return { rms: !metricPeak, peak: metricPeak };
}
