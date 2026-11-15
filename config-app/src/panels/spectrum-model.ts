// Pure helpers for the audio spectrum chart (no DOM, no Plotly): the frequency-axis scale the
// person picked, and the stretches of the spectrum the band-pass removes.

export type SpectrumScale = 'linear' | 'log';
export const SPECTRUM_SCALE_KEY = 'gaiaball-spectrum-scale';
export const DEFAULT_SPECTRUM_SCALE: SpectrumScale = 'log';

/** Nyquist of the 2 kHz raw stream. */
export const SPEC_MAX_HZ = 1000;
/** Where the log axis starts: below it there are only the first two FFT bins. */
export const SPEC_LOG_MIN_HZ = 10;

/** Visible frequency range in Hz for each scale. */
export function specXRange(scale: SpectrumScale): [number, number] {
  return scale === 'log' ? [SPEC_LOG_MIN_HZ, SPEC_MAX_HZ] : [0, SPEC_MAX_HZ];
}

export function loadSpectrumScale(storage: Pick<Storage, 'getItem'> | null): SpectrumScale {
  try {
    const v = storage?.getItem(SPECTRUM_SCALE_KEY);
    return v === 'linear' || v === 'log' ? v : DEFAULT_SPECTRUM_SCALE;
  } catch {
    return DEFAULT_SPECTRUM_SCALE;
  }
}

export function saveSpectrumScale(storage: Pick<Storage, 'setItem'> | null, s: SpectrumScale): void {
  try {
    storage?.setItem(SPECTRUM_SCALE_KEY, s);
  } catch {
    /* private window or blocked storage: the choice just is not remembered */
  }
}

export interface Band { enabled: boolean; hpHz: number; lpHz: number }
export interface ShadeSpan { x0: number; x1: number }

/**
 * Stretches of [lo, hi] outside the pass band: below the high-pass and above the low-pass.
 * Nothing when the band-pass is off. A band with LP <= HP passes nothing, so it shades all.
 */
export function bandShading(band: Band, [lo, hi]: readonly [number, number]): ShadeSpan[] {
  if (!band.enabled) return [];
  if (band.lpHz <= band.hpHz) return [{ x0: lo, x1: hi }];
  const spans: ShadeSpan[] = [];
  if (band.hpHz > lo) spans.push({ x0: lo, x1: Math.min(band.hpHz, hi) });
  if (band.lpHz < hi) spans.push({ x0: Math.max(band.lpHz, lo), x1: hi });
  return spans;
}
