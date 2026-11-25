// Pure helpers for the audio spectrum chart (no DOM, no Plotly): the frequency-axis scale the
// person picked and the axes of the chart. The band-pass response itself is in metrics/filter-response.ts.
import type { BandSpec } from '../metrics/filter-response.ts';

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

/** Left axis (spectrum, dBFS) and right axis (filter gain, dB): same 120 dB span, ticks every 20 dB. */
export const SPEC_DBFS_RANGE: [number, number] = [-120, 0];
export const SPEC_GAIN_RANGE: [number, number] = [-100, 20];

/** One string per distinct band, for caches and redraw keys. */
export function bandKey(b: BandSpec): string {
  return `${b.enabled}|${b.hp.hz}|${b.hp.qX100}|${b.hp.slopeDbOct}|${b.lp.hz}|${b.lp.qX100}|${b.lp.slopeDbOct}`;
}
