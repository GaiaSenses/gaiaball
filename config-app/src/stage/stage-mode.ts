// Stage mode ("Modo palco") vs adjust mode ("Modo ajuste"). Pure controller: no DOM, so it
// can be tested in plain node. Entering the stage pauses the 3D scene, the panels and the
// diagnostic subscription (…0005); the Pd forwarding lives in GaiaballClient.ingest and is
// never touched here.

export type UiMode = 'ajuste' | 'palco';
export const UI_MODE_KEY = 'gaiaball-ui-mode';

export function loadUiMode(storage: Pick<Storage, 'getItem'> | null): UiMode {
  try {
    return storage?.getItem(UI_MODE_KEY) === 'palco' ? 'palco' : 'ajuste';
  } catch {
    return 'ajuste';
  }
}

export interface StageDeps {
  scene: { pause(): void; resume(): void };
  panels: { setPaused(p: boolean): void }[];
  client: { connected: boolean; pauseDiag(): Promise<void>; resumeDiag(): Promise<void> };
  /** inert + .stage-locked on the locked regions. */
  setLocked(on: boolean): void;
  showBar(on: boolean): void;
  storage: Pick<Storage, 'getItem' | 'setItem'> | null;
}

export class StageController {
  private _mode: UiMode = 'ajuste';
  /** True while the diagnostic characteristic is unsubscribed by us on the current link. */
  private diagPaused = false;

  private readonly deps: StageDeps;

  constructor(deps: StageDeps) {
    this.deps = deps;
  }

  get mode(): UiMode {
    return this._mode;
  }

  async setMode(m: UiMode): Promise<void> {
    if (m === this._mode) return;
    this._mode = m;
    const d = this.deps;
    const stage = m === 'palco';
    if (stage) d.scene.pause(); else d.scene.resume();
    for (const p of d.panels) p.setPaused(stage);
    d.setLocked(stage);
    d.showBar(stage);
    try { d.storage?.setItem(UI_MODE_KEY, m); } catch { /* per-viewer convenience only */ }
    if (stage) await this.pauseDiag(); else await this.resumeDiag();
  }

  /** On a (re)connection, in the stage, drop the diag subscription again (onDisconnected cleared the flag). */
  async onConnected(): Promise<void> {
    if (this._mode === 'palco') await this.pauseDiag();
  }

  /** The link is gone, so there is no subscription left to restore. */
  onDisconnected(): void {
    this.diagPaused = false;
  }

  private async pauseDiag(): Promise<void> {
    if (this.diagPaused || !this.deps.client.connected) return;
    this.diagPaused = true;
    try { await this.deps.client.pauseDiag(); } catch (e) { console.warn('pauseDiag falhou', e); }
  }

  private async resumeDiag(): Promise<void> {
    if (!this.diagPaused) return;
    this.diagPaused = false;
    try { await this.deps.client.resumeDiag(); } catch (e) { console.warn('resumeDiag falhou', e); }
  }
}
