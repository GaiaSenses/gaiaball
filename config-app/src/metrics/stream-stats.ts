// Stream quality metrics for a notify stream. Pure logic, no DOM/BLE dependencies.

const WINDOW_MS = 2000;
const HALF_SEQ = 0x8000;

export class StreamStats {
  private arrivals: number[] = [];
  private lastSeq = -1;
  private lastLocalMs = -Infinity;
  private expected = 0;
  private lost = 0;

  /** tDeviceUs is accepted for API symmetry; metrics use local arrival time. */
  push(seq: number, _tDeviceUs: number, tLocalMs: number): void {
    this.arrivals.push(tLocalMs);
    const cutoff = tLocalMs - WINDOW_MS;
    let drop = 0;
    while (drop < this.arrivals.length - 1 && this.arrivals[drop] < cutoff) drop++;
    if (drop) this.arrivals.splice(0, drop);
    this.lastLocalMs = tLocalMs;

    // seq increments per packet SENT, so any gap is loss. u16 wraparound via modular delta.
    if (this.lastSeq < 0) {
      this.expected = 1;
    } else {
      const delta = (seq - this.lastSeq) & 0xffff;
      if (delta === 0 || delta >= HALF_SEQ) return; // duplicate or reordered: ignore
      this.expected += delta;
      this.lost += delta - 1;
    }
    this.lastSeq = seq & 0xffff;
  }

  /** Arrival rate over the last ~2 s window (packets / window span). */
  hz(): number {
    const a = this.arrivals;
    if (a.length < 2) return 0;
    const span = a[a.length - 1] - a[0];
    return span > 0 ? ((a.length - 1) * 1000) / span : 0;
  }

  /** Jitter = mean |dt - meanDt| of inter-arrival deltas in the window, in ms. */
  jitterMs(): number {
    const a = this.arrivals;
    if (a.length < 3) return 0;
    const d: number[] = [];
    for (let i = 1; i < a.length; i++) d.push(a[i] - a[i - 1]);
    const mean = d.reduce((s, v) => s + v, 0) / d.length;
    return d.reduce((s, v) => s + Math.abs(v - mean), 0) / d.length;
  }

  /** Percentage of packets lost (seq gaps) since reset, 0 when nothing expected. */
  lossPct(): number {
    return this.expected > 0 ? (this.lost / this.expected) * 100 : 0;
  }

  /** Milliseconds since the last arrival; Infinity if none yet. */
  ageMs(nowMs: number = performance.now()): number {
    return nowMs - this.lastLocalMs;
  }

  reset(): void {
    this.arrivals = [];
    this.lastSeq = -1;
    this.lastLocalMs = -Infinity;
    this.expected = 0;
    this.lost = 0;
  }
}
