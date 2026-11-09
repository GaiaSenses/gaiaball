// Magnetometer hard/soft-iron calibration fit.
//
// WHY here and not on the device: the ball streams its raw magnetometer samples
// (MagChunkPacket) and the browser does the least-squares solve. The ESP32 would have
// to hold hundreds of samples and run the same solve while keeping the audio and BLE
// paths on time, for a result no better than this one.
//
// MODEL: the earth field has a constant magnitude, so rotating the ball should trace a
// SPHERE. Magnets and currents on the board shift that sphere (hard iron) and ferrous
// material stretches it into an ellipsoid (soft iron). The fit therefore finds the
// axis-aligned ellipsoid closest to the samples and returns the transform that maps it
// back onto a sphere:  m_c = S (m_u - h).
//
// WHY axis-aligned (6 parameters: centre + 3 radii) and not a general ellipsoid (9):
// the off-diagonal terms model rotational cross-coupling, which needs a far better
// covered sphere to estimate than a person waving a ball can produce — fitting them
// from a partial sweep makes the result worse, not better. The firmware still accepts
// a full 3x3 matrix, so a better fit can be dropped in later without touching it.

export interface MagCal {
  /** Hard-iron offset in uT, subtracted first. */
  hardIronUt: [number, number, number];
  /** Soft-iron matrix, row-major 3x3, applied after the offset. */
  softIron: number[];
}

export const IDENTITY_MAG_CAL: MagCal = {
  hardIronUt: [0, 0, 0],
  softIron: [1, 0, 0, 0, 1, 0, 0, 0, 1],
};

export const MAG_HARD_IRON_LSB_UT = 0.1;
export const MAG_SOFT_IRON_ONE = 4096;
/** Samples needed before a fit is attempted (about 2 s of a 50 Hz capture). */
export const MAG_FIT_MIN_SAMPLES = 60;

export function applyMagCal(c: MagCal, v: readonly [number, number, number]): [number, number, number] {
  const d0 = v[0] - c.hardIronUt[0];
  const d1 = v[1] - c.hardIronUt[1];
  const d2 = v[2] - c.hardIronUt[2];
  const s = c.softIron;
  return [
    s[0] * d0 + s[1] * d1 + s[2] * d2,
    s[3] * d0 + s[4] * d1 + s[5] * d2,
    s[6] * d0 + s[7] * d1 + s[8] * d2,
  ];
}

export interface MagFit {
  cal: MagCal;
  /** Radius of the sphere the calibration maps onto, in uT (the earth field strength). */
  radiusUt: number;
  /** RMS deviation from that sphere after correction, as a percentage of the radius. */
  residualPct: number;
  /** How many of the 8 sign-octants the samples reached: 8 = a full sweep. */
  octants: number;
  samples: number;
}

/** Gaussian elimination with partial pivoting; returns null on a singular system. */
function solve(a: number[][], b: number[]): number[] | null {
  const n = b.length;
  for (let col = 0; col < n; col++) {
    let pivot = col;
    for (let r = col + 1; r < n; r++) if (Math.abs(a[r][col]) > Math.abs(a[pivot][col])) pivot = r;
    if (!(Math.abs(a[pivot][col]) > 1e-12)) return null;
    [a[col], a[pivot]] = [a[pivot], a[col]];
    [b[col], b[pivot]] = [b[pivot], b[col]];
    for (let r = col + 1; r < n; r++) {
      const f = a[r][col] / a[col][col];
      if (f === 0) continue;
      for (let c = col; c < n; c++) a[r][c] -= f * a[col][c];
      b[r] -= f * b[col];
    }
  }
  const x = new Array<number>(n).fill(0);
  for (let r = n - 1; r >= 0; r--) {
    let s = b[r];
    for (let c = r + 1; c < n; c++) s -= a[r][c] * x[c];
    x[r] = s / a[r][r];
  }
  return x.every(Number.isFinite) ? x : null;
}

/**
 * Fits `samples` (raw uT triplets, chip frame) and returns the calibration plus its
 * quality, or null when the cloud cannot describe an ellipsoid (too few samples, a
 * sweep confined to one direction, a singular system).
 */
export function fitMagCalibration(
  samples: readonly (readonly [number, number, number])[],
  minSamples: number = MAG_FIT_MIN_SAMPLES,
): MagFit | null {
  if (samples.length < minSamples) return null;

  // Least squares on  a x^2 + b y^2 + c z^2 + d x + e y + f z = 1  (linear in a..f).
  const ata: number[][] = Array.from({ length: 6 }, () => new Array<number>(6).fill(0));
  const atb = new Array<number>(6).fill(0);
  for (const s of samples) {
    if (!s.every(Number.isFinite)) continue;
    const row = [s[0] * s[0], s[1] * s[1], s[2] * s[2], s[0], s[1], s[2]];
    for (let i = 0; i < 6; i++) {
      for (let j = 0; j < 6; j++) ata[i][j] += row[i] * row[j];
      atb[i] += row[i];
    }
  }
  const p = solve(ata, atb);
  if (p === null) return null;

  const [a, b, c, d, e, f] = p;
  const quad = [a, b, c];
  const lin = [d, e, f];
  if (quad.some((v) => !(v > 0))) return null;  // not an ellipsoid: open or inverted

  // a(u - centre)^2 summed = 1 + sum(a_i centre_i^2) = K, so semi-axis_i = sqrt(K/a_i).
  const centre = quad.map((q, i) => -lin[i] / (2 * q)) as [number, number, number];
  const k = 1 + quad.reduce((acc, q, i) => acc + q * centre[i] * centre[i], 0);
  if (!(k > 0)) return null;
  const radii = quad.map((q) => Math.sqrt(k / q));
  if (radii.some((r) => !(r > 1e-6) || !Number.isFinite(r))) return null;

  // Normalise to the geometric mean so the corrected field keeps its real magnitude.
  const radiusUt = Math.cbrt(radii[0] * radii[1] * radii[2]);
  const gains = radii.map((r) => radiusUt / r);
  const cal: MagCal = {
    hardIronUt: centre,
    softIron: [gains[0], 0, 0, 0, gains[1], 0, 0, 0, gains[2]],
  };

  // Quality: how far the corrected samples sit from the sphere, and how much of the
  // sphere they covered (a sweep that only saw one side gives a confident bad fit).
  let sumSq = 0;
  let used = 0;
  let octantMask = 0;
  const dead = 0.2 * radiusUt;  // ignore components near the axes for the coverage test
  for (const s of samples) {
    if (!s.every(Number.isFinite)) continue;
    const q = applyMagCal(cal, s);
    const n = Math.hypot(q[0], q[1], q[2]);
    const err = (n - radiusUt) / radiusUt;
    sumSq += err * err;
    used++;
    if (Math.abs(q[0]) > dead && Math.abs(q[1]) > dead && Math.abs(q[2]) > dead) {
      octantMask |= 1 << ((q[0] > 0 ? 1 : 0) | (q[1] > 0 ? 2 : 0) | (q[2] > 0 ? 4 : 0));
    }
  }
  if (used < minSamples) return null;
  let octants = 0;
  for (let i = 0; i < 8; i++) if (octantMask & (1 << i)) octants++;

  return {
    cal,
    radiusUt,
    residualPct: Math.sqrt(sumSq / used) * 100,
    octants,
    samples: used,
  };
}

function sat16(v: number): number {
  if (!Number.isFinite(v)) return 0;
  return Math.max(-32768, Math.min(32767, Math.round(v)));
}

/** Encodes a calibration into the firmware's fixed point (TLV 0x1E). */
export function magCalToFixed(c: MagCal): { hard: Int16Array; soft: Int16Array } {
  const hard = new Int16Array(3);
  const soft = new Int16Array(9);
  for (let i = 0; i < 3; i++) hard[i] = sat16(c.hardIronUt[i] / MAG_HARD_IRON_LSB_UT);
  for (let i = 0; i < 9; i++) soft[i] = sat16(c.softIron[i] * MAG_SOFT_IRON_ONE);
  return { hard, soft };
}
