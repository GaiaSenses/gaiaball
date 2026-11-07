import { describe, expect, it } from 'vitest';
import { PresetStore, type StorageLike } from './preset-store.ts';

function memStorage(): StorageLike & { data: Record<string, string> } {
  const data: Record<string, string> = {};
  return {
    data,
    getItem: (k) => (k in data ? data[k] : null),
    setItem: (k, v) => { data[k] = v; },
  };
}
const regs = [{ id: 0x02, value: 30 }, { id: 0x40, value: 'Globo_2' }];

describe('PresetStore', () => {
  it('save/list roundtrip persists to storage', () => {
    const st = memStorage();
    const s = new PresetStore(st);
    s.save('a', regs);
    expect(s.list().map((p) => p.name)).toEqual(['a']);
    expect(s.list()[0].regs).toEqual(regs);
    expect(new PresetStore(st).list()[0].regs).toEqual(regs);
    expect(JSON.parse(st.data['gaiaball-presets']).version).toBe(1);
  });
  it('overwrites same name', () => {
    const s = new PresetStore(memStorage());
    s.save('a', regs);
    s.save('a', [{ id: 0x02, value: 10 }]);
    expect(s.list()).toHaveLength(1);
    expect(s.list()[0].regs).toEqual([{ id: 0x02, value: 10 }]);
  });
  it('removes', () => {
    const s = new PresetStore(memStorage());
    s.save('a', regs); s.save('b', regs);
    s.remove('a');
    expect(s.list().map((p) => p.name)).toEqual(['b']);
  });
  it('export -> import roundtrip into a fresh store', () => {
    const s = new PresetStore(memStorage());
    s.save('a', regs); s.save('b', []);
    const t = new PresetStore(memStorage());
    expect(t.importJson(s.exportJson())).toBe(2);
    expect(t.list().map((p) => p.name).sort()).toEqual(['a', 'b']);
    expect(t.list().find((p) => p.name === 'a')!.regs).toEqual(regs);
  });
  it('invalid JSON or shape -> 0 and store unchanged', () => {
    const s = new PresetStore(memStorage());
    s.save('a', regs);
    const before = s.exportJson();
    for (const bad of [
      'not json', '{}', 'null', '[]',
      '{"version":2,"presets":[]}',
      '{"version":1,"presets":"x"}',
      '{"version":1,"presets":[{"name":"z","regs":[{"id":"1","value":1}]}]}',
      '{"version":1,"presets":[{"name":"ok","regs":[]},{"name":5,"regs":[]}]}',
      '{"version":1,"presets":[{"name":"z","regs":[{"id":1,"value":null}]}]}',
    ]) {
      expect(s.importJson(bad)).toBe(0);
    }
    expect(s.exportJson()).toBe(before);
  });
  it('import merges by name, imported wins', () => {
    const s = new PresetStore(memStorage());
    s.save('a', regs); s.save('keep', regs);
    const n = s.importJson(JSON.stringify({ version: 1, presets: [
      { name: 'a', savedAt: 'x', regs: [{ id: 3, value: 7 }] },
      { name: 'new', regs: [] },
    ] }));
    expect(n).toBe(2);
    expect(s.list().map((p) => p.name).sort()).toEqual(['a', 'keep', 'new']);
    expect(s.list().find((p) => p.name === 'a')!.regs).toEqual([{ id: 3, value: 7 }]);
  });
  it('falls back to memory when storage throws', () => {
    const bad: StorageLike = { getItem: () => { throw new Error('x'); }, setItem: () => { throw new Error('q'); } };
    const s = new PresetStore(bad);
    s.save('a', regs);
    expect(s.list()).toHaveLength(1);
  });
});
