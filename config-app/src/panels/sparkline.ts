/** Fixed-size rolling buffer + tiny canvas 2D line chart. */
export class RingBuffer {
  private data: number[] = [];
  private cap: number;
  constructor(cap: number) { this.cap = cap; }
  push(v: number): void {
    this.data.push(v);
    if (this.data.length > this.cap) this.data.shift();
  }
  values(): readonly number[] { return this.data; }
}

export function drawSparkline(canvas: HTMLCanvasElement, values: readonly number[], color: string): void {
  const ctx = canvas.getContext('2d');
  if (!ctx) return;
  const { width: w, height: h } = canvas;
  ctx.clearRect(0, 0, w, h);
  if (values.length < 2) return;
  const max = Math.max(...values, 1e-9), min = Math.min(...values, 0);
  const span = max - min || 1;
  ctx.strokeStyle = color;
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  values.forEach((v, i) => {
    const x = (i / (values.length - 1)) * (w - 2) + 1;
    const y = h - 2 - ((v - min) / span) * (h - 4);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  });
  ctx.stroke();
}
