// Frequency response of the firmware's breath band-pass (firmware/Gaiaball/src/logic/FilterCascade.*),
// for the spectrum chart. Pure math, no DOM.
//
// The firmware runs, per microphone: HP then LP, each a Butterworth of order slope/6 (6..48 dB/oct)
// made of second-order sections (+ one first-order section for an odd order), discretised with the
// bilinear transform prewarped at the cutoff. Q is the gain exactly at the cutoff: it scales the
// highest-Q Butterworth section, so 0.71 is the flat Butterworth (-3 dB at the cutoff) at every slope.
// The response below is the analytic magnitude of that same design, so it is what the firmware does,
// not an idealised drawing.

/** Slopes the firmware accepts (TLV 0x38 / 0x39), in dB per octave. */
export const SLOPES_DB_OCT = [6, 12, 18, 24, 36, 48] as const;
export const DEFAULT_SLOPE_DB_OCT = 12;
/** Sample rate the filters run at in the firmware (the I2S rate, not the 2 kHz stream). */
export const AUDIO_FS_HZ = 16000;

export interface CutSpec {
  hz: number;
  /** Q x100, as on the wire (71 = 0.71). */
  qX100: number;
  slopeDbOct: number;
}
export interface BandSpec {
  enabled: boolean;
  hp: CutSpec;
  lp: CutSpec;
}

export const isValidSlope = (s: number): boolean => (SLOPES_DB_OCT as readonly number[]).includes(s);

/**
 * Q of each second-order section, highest first. Butterworth of `order`, with the first one
 * multiplied by q·√2 so that the gain at the cutoff is q (the Butterworth Qs multiply to 1/√2).
 */
export function sectionQs(order: number, q: number): number[] {
  const out: number[] = [];
  for (let i = 0; i < Math.floor(order / 2); i++) {
    let s = 1 / (2 * Math.sin(((2 * i + 1) * Math.PI) / (2 * order)));
    if (i === 0) s *= q * Math.SQRT2;
    out.push(s);
  }
  return out;
}

/** Gain in dB of one cut (HP or LP) at `hz`, with the firmware's clamps. */
export function cutGainDb(kind: 'hp' | 'lp', c: CutSpec, hz: number, fs = AUDIO_FS_HZ): number {
  const slope = isValidSlope(c.slopeDbOct) ? c.slopeDbOct : DEFAULT_SLOPE_DB_OCT;
  const fc = Math.min(Math.max(c.hz, 1), 0.45 * fs);
  const q = Math.min(Math.max(c.qX100 / 100, 0.1), 20);
  if (hz <= 0) return kind === 'hp' ? -Infinity : 0;
  if (hz >= fs / 2) return kind === 'lp' ? -Infinity : 0;
  const order = slope / 6;
  // Analog frequency normalised to the cutoff, after the bilinear prewarp.
  let x = Math.tan((Math.PI * hz) / fs) / Math.tan((Math.PI * fc) / fs);
  if (kind === 'hp') x = 1 / x; // HP(x) = LP(1/x) for these prototypes
  let mag2 = order % 2 ? 1 / (1 + x * x) : 1;
  for (const sq of sectionQs(order, q)) mag2 /= (1 - x * x) ** 2 + (x / sq) ** 2;
  return 10 * Math.log10(mag2);
}

/** Gain in dB of the whole band-pass (HP then LP) at `hz`; 0 dB with the band-pass off. */
export function bandGainDb(b: BandSpec, hz: number, fs = AUDIO_FS_HZ): number {
  if (!b.enabled) return 0;
  return cutGainDb('hp', b.hp, hz, fs) + cutGainDb('lp', b.lp, hz, fs);
}

/**
 * The band-pass response sampled over [lo, hi]: log-spaced points on a log axis (the first one at
 * `lo`, which must be > 0), linear ones otherwise. -Infinity (only possible at 0 Hz) becomes null.
 */
export function responseCurve(
  b: BandSpec, [lo, hi]: readonly [number, number], log: boolean, points = 240,
): { x: number[]; y: (number | null)[] } {
  const x: number[] = [];
  const y: (number | null)[] = [];
  for (let i = 0; i < points; i++) {
    const t = i / (points - 1);
    const hz = log ? lo * (hi / lo) ** t : lo + (hi - lo) * t;
    const g = bandGainDb(b, hz);
    x.push(hz);
    y.push(Number.isFinite(g) ? g : null);
  }
  return { x, y };
}

/** Short label of a slope for the chart and the form, e.g. "24 dB/oit". */
export const slopeLabel = (s: number): string => `${s} dB/oit`;
