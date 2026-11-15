import { describe, expect, it, vi } from 'vitest';
import { parseSensitivity } from './stage-bar.ts';
import { StageController, UI_MODE_KEY, loadUiMode, type StageDeps } from './stage-mode.ts';

function mk(connected = true, stored: string | null = null) {
  const store = new Map<string, string>();
  if (stored !== null) store.set(UI_MODE_KEY, stored);
  const calls: string[] = [];
  const panels = [{ setPaused: vi.fn() }, { setPaused: vi.fn() }];
  const client = {
    connected,
    pauseDiag: vi.fn(async () => { calls.push('pauseDiag'); }),
    resumeDiag: vi.fn(async () => { calls.push('resumeDiag'); }),
  };
  const deps: StageDeps = {
    scene: { pause: vi.fn(() => { calls.push('pause'); }), resume: vi.fn(() => { calls.push('resume'); }) },
    panels,
    client,
    setLocked: vi.fn(),
    showBar: vi.fn(),
    storage: { getItem: (k) => store.get(k) ?? null, setItem: (k, v) => { store.set(k, v); } },
  };
  return { deps, panels, client, store, calls, ctl: new StageController(deps) };
}

describe('loadUiMode', () => {
  it('defaults to ajuste', () => {
    expect(loadUiMode(null)).toBe('ajuste');
    expect(loadUiMode({ getItem: () => 'xyz' })).toBe('ajuste');
    expect(loadUiMode({ getItem: () => { throw new Error('blocked'); } })).toBe('ajuste');
  });
  it('reads a stored palco', () => {
    expect(loadUiMode({ getItem: () => 'palco' })).toBe('palco');
  });
});

describe('StageController', () => {
  it('starts in ajuste', () => {
    expect(mk().ctl.mode).toBe('ajuste');
  });

  it('entering palco pauses everything and persists', async () => {
    const { ctl, deps, panels, client, store } = mk();
    await ctl.setMode('palco');
    expect(ctl.mode).toBe('palco');
    expect(deps.scene.pause).toHaveBeenCalledOnce();
    for (const p of panels) expect(p.setPaused).toHaveBeenCalledWith(true);
    expect(client.pauseDiag).toHaveBeenCalledOnce();
    expect(deps.setLocked).toHaveBeenCalledWith(true);
    expect(deps.showBar).toHaveBeenCalledWith(true);
    expect(store.get(UI_MODE_KEY)).toBe('palco');
  });

  it('leaving palco resumes everything', async () => {
    const { ctl, deps, panels, client, store } = mk();
    await ctl.setMode('palco');
    await ctl.setMode('ajuste');
    expect(deps.scene.resume).toHaveBeenCalledOnce();
    for (const p of panels) expect(p.setPaused).toHaveBeenLastCalledWith(false);
    expect(client.resumeDiag).toHaveBeenCalledOnce();
    expect(deps.setLocked).toHaveBeenLastCalledWith(false);
    expect(deps.showBar).toHaveBeenLastCalledWith(false);
    expect(store.get(UI_MODE_KEY)).toBe('ajuste');
  });

  it('does not touch diag while disconnected, but onConnected unsubscribes in palco', async () => {
    const { ctl, client } = mk(false);
    await ctl.setMode('palco');
    expect(client.pauseDiag).not.toHaveBeenCalled();
    client.connected = true;
    await ctl.onConnected();
    expect(client.pauseDiag).toHaveBeenCalledOnce();
  });

  it('onConnected does nothing in ajuste', async () => {
    const { ctl, client } = mk();
    await ctl.onConnected();
    expect(client.pauseDiag).not.toHaveBeenCalled();
  });

  it('repeating setMode(palco) pauses diag once', async () => {
    const { ctl, client, deps } = mk();
    await ctl.setMode('palco');
    await ctl.setMode('palco');
    await ctl.onConnected();
    expect(client.pauseDiag).toHaveBeenCalledOnce();
    expect(deps.scene.pause).toHaveBeenCalledOnce();
  });

  it('a pauseDiag failure does not block the mode change', async () => {
    const { ctl, client, deps } = mk();
    client.pauseDiag.mockRejectedValueOnce(new Error('gatt'));
    await ctl.setMode('palco');
    expect(ctl.mode).toBe('palco');
    expect(deps.showBar).toHaveBeenCalledWith(true);
  });

  it('does not resume diag when it was never paused', async () => {
    const { ctl, client } = mk(false);
    await ctl.setMode('palco');
    await ctl.setMode('ajuste');
    expect(client.resumeDiag).not.toHaveBeenCalled();
  });
});

describe('parseSensitivity', () => {
  it('rejects empty and non-numeric input', () => {
    for (const raw of ['', '  ', 'abc', '-', '1e999']) expect(parseSensitivity(raw)).toBeNull();
  });
  it('rejects out of range and accepts the limits', () => {
    expect(parseSensitivity('-91')).toBeNull();
    expect(parseSensitivity('0.5')).toBeNull();
    expect(parseSensitivity('-90')).toBe(-90);
    expect(parseSensitivity('0')).toBe(0);
    expect(parseSensitivity('-35.5')).toBe(-35.5);
  });
});
