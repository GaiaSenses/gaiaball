// plotly.js-dist-min ships no TypeScript types; the panel uses a narrow surface.
declare module 'plotly.js-dist-min' {
  export interface PlotlyStatic {
    newPlot(
      el: HTMLElement,
      data: unknown[],
      layout?: Record<string, unknown>,
      config?: Record<string, unknown>,
    ): Promise<unknown>;
    extendTraces(
      el: HTMLElement,
      update: { x: number[][]; y: (number | null)[][] },
      indices: number[],
      maxPoints?: number,
    ): Promise<unknown>;
    restyle(el: HTMLElement, update: Record<string, unknown>, indices?: number[]): Promise<unknown>;
    relayout(el: HTMLElement, update: Record<string, unknown>): Promise<unknown>;
    purge(el: HTMLElement): void;
  }
  const Plotly: PlotlyStatic;
  export default Plotly;
}
