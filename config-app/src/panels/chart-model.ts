// Pure chart logic for the sensors panel. Kept free of theme.ts (it touches `window`
// at import) so it can be unit-tested in plain node.

export const WINDOW_OPTIONS_S = [5, 10, 30, 60] as const;
export type WindowS = (typeof WINDOW_OPTIONS_S)[number];
export const DEFAULT_WINDOW_S: WindowS = 10;
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

/** Seconds of history every line keeps, whatever window is showing: the largest window. */
export const HISTORY_S: number = WINDOW_OPTIONS_S[WINDOW_OPTIONS_S.length - 1];
/**
 * Most points a line hands to the chart per redraw. Above it the line is drawn as the
 * minimum and maximum of each stretch (MAX_DRAWN_POINTS / 2 stretches), so a 60 s window at
 * 200 Hz costs the same as a short one and peaks and breath pulses still show.
 */
export const MAX_DRAWN_POINTS = 500;

/** Index of the first x >= cutoff in an ascending array (xs.length when there is none). */
export function firstIndexAtOrAfter(xs: readonly number[], cutoff: number): number {
  let lo = 0, hi = xs.length;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (xs[mid] < cutoff) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

/**
 * Min/max decimation. Lines at or under `maxPoints` come back untouched; longer ones are cut
 * into maxPoints / 2 stretches of equal count, each drawn as its lowest and highest value in
 * time order (one point when they coincide). A stretch holding only gaps (null) stays a gap.
 */
export function minMaxDecimate(
  xs: readonly number[], ys: readonly (number | null)[], maxPoints: number,
): { x: number[]; y: (number | null)[] } {
  const n = xs.length;
  if (n <= maxPoints) return { x: xs.slice(), y: ys.slice() };
  const buckets = Math.max(1, Math.floor(maxPoints / 2));
  const size = Math.ceil(n / buckets);
  const x: number[] = [];
  const y: (number | null)[] = [];
  for (let start = 0; start < n; start += size) {
    const end = Math.min(n, start + size);
    let iMin = -1, iMax = -1;
    for (let i = start; i < end; i++) {
      const v = ys[i];
      if (v === null) continue;
      if (iMin < 0 || v < (ys[iMin] as number)) iMin = i;
      if (iMax < 0 || v > (ys[iMax] as number)) iMax = i;
    }
    if (iMin < 0) { x.push(xs[start]); y.push(null); continue; }
    const a = Math.min(iMin, iMax), b = Math.max(iMin, iMax);
    x.push(xs[a]); y.push(ys[a]);
    if (b !== a) { x.push(xs[b]); y.push(ys[b]); }
  }
  return { x, y };
}

/** Bottom of the level meters on the microphone cards, in dBFS (same floor as the stage meter). */
export const LEVEL_FLOOR_DBFS = -90;

/** Position of a dBFS level on a meter from LEVEL_FLOOR_DBFS (0) to 0 dBFS (1), clamped. */
export function levelFraction(dbfs: number, floor: number = LEVEL_FLOOR_DBFS): number {
  if (!Number.isFinite(dbfs)) return 0;
  return Math.min(1, Math.max(0, (dbfs - floor) / -floor));
}

const MINUS = '−';

/**
 * Whole number with an explicit sign, for signed readings (angles, acceleration): "+93",
 * "−180", "0". No thousands separator: "1009", not "1.009" (which reads as a decimal in
 * pt-BR and changes the width). Shown right-aligned in a fixed-width box, the units digit
 * never moves.
 */
export function formatSigned(v: number): string {
  if (!Number.isFinite(v)) return '—';
  const n = Math.round(v) + 0; // + 0 turns -0 into 0
  return (n > 0 ? '+' : n < 0 ? MINUS : '') + String(Math.abs(n));
}

/** Whole dBFS (never positive in practice) with the typographic minus: "−57", "0". */
export function formatDb(v: number): string {
  if (!Number.isFinite(v)) return '—';
  const n = Math.round(v) + 0;
  return (n < 0 ? MINUS : '') + String(Math.abs(n));
}
