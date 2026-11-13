// Minimal radix-2 FFT + Hann-windowed magnitude spectrum for the live audio stream.
// The firmware ships RAW decimated audio (2 kHz mono int16, top 16 bits of the
// 24-bit samples); the FFT runs here, in the frontend, by design.

export interface Spectrum {
  /** Bin center frequencies in Hz (size/2 bins, binHz = fs/size). */
  freqsHz: Float64Array;
  /** Per-bin level in dBFS (0 dBFS = full-scale sine, int16 FS = 32768), floor -120. */
  dbfs: Float64Array;
}

function fftRadix2(re: Float64Array, im: Float64Array): void {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j |= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wr = Math.cos(ang);
    const wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) {
      let cr = 1;
      let ci = 0;
      for (let k = 0; k < len / 2; k++) {
        const a = i + k;
        const b = i + k + len / 2;
        const tr = re[b] * cr - im[b] * ci;
        const ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
        const ncr = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = ncr;
      }
    }
  }
}

/**
 * Computes the magnitude spectrum of the most recent `size` samples (power of two).
 * Hann window; mean removed so bin 0 does not swamp the scale.
 */
export function computeSpectrum(samples: Int16Array | number[], size: number, fsHz: number): Spectrum {
  if ((size & (size - 1)) !== 0 || size < 4) throw new RangeError('size must be a power of two >= 4');
  const n = Math.min(samples.length, size);
  const re = new Float64Array(size);
  const im = new Float64Array(size);
  let mean = 0;
  for (let i = 0; i < n; i++) mean += samples[samples.length - n + i] as number;
  mean = n > 0 ? mean / n : 0;
  for (let i = 0; i < n; i++) {
    const w = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (size - 1)));
    re[i] = ((samples[samples.length - n + i] as number) - mean) * w;
  }
  fftRadix2(re, im);
  const bins = size / 2;
  const freqsHz = new Float64Array(bins);
  const dbfs = new Float64Array(bins);
  const ampScale = 2 / (size * 0.5); // Hann coherent gain 0.5
  const fullScale = 32768;
  for (let k = 0; k < bins; k++) {
    freqsHz[k] = (k * fsHz) / size;
    const amp = Math.hypot(re[k], im[k]) * ampScale;
    dbfs[k] = amp > 0 ? Math.max(-120, 20 * Math.log10(amp / fullScale)) : -120;
  }
  return { freqsHz, dbfs };
}
