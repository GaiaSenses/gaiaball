import type { TlvWrite } from '../protocol/tlv.ts';

export interface Preset { name: string; savedAt: string; regs: TlvWrite[] }
export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

const KEY = 'gaiaball-presets';

function validReg(r: unknown): r is TlvWrite {
  if (typeof r !== 'object' || r === null) return false;
  const { id, value } = r as Record<string, unknown>;
  return typeof id === 'number' && Number.isFinite(id) &&
    ((typeof value === 'number' && Number.isFinite(value)) || typeof value === 'string');
}

/** Returns normalized presets, or null if the shape is invalid (all-or-nothing). */
function parse(json: string): Preset[] | null {
  let data: unknown;
  try { data = JSON.parse(json); } catch { return null; }
  if (typeof data !== 'object' || data === null) return null;
  const d = data as { version?: unknown; presets?: unknown };
  if (d.version !== 1 || !Array.isArray(d.presets)) return null;
  const out: Preset[] = [];
  for (const p of d.presets as unknown[]) {
    if (typeof p !== 'object' || p === null) return null;
    const { name, savedAt, regs } = p as Record<string, unknown>;
    if (typeof name !== 'string' || name === '' || !Array.isArray(regs) || !regs.every(validReg)) return null;
    out.push({
      name,
      savedAt: typeof savedAt === 'string' ? savedAt : new Date(0).toISOString(),
      regs: (regs as TlvWrite[]).map((r) => ({ id: r.id, value: r.value })),
    });
  }
  return out;
}

export class PresetStore {
  private mem: Preset[] | null = null; // in-memory fallback when storage is unusable
  private readonly storage: StorageLike | null;

  constructor(storage?: StorageLike) {
    if (storage) this.storage = storage;
    else {
      try { this.storage = globalThis.localStorage ?? null; } catch { this.storage = null; }
    }
  }

  list(): Preset[] {
    if (this.mem) return [...this.mem];
    try {
      const raw = this.storage?.getItem(KEY);
      if (raw) return parse(raw) ?? [];
    } catch { /* fall through */ }
    return [];
  }

  private write(presets: Preset[]): void {
    try {
      if (!this.storage) throw new Error('no storage');
      this.storage.setItem(KEY, JSON.stringify({ version: 1, presets }));
      this.mem = null;
    } catch {
      this.mem = presets; // quota / private mode
    }
  }

  save(name: string, regs: TlvWrite[]): void {
    const rest = this.list().filter((p) => p.name !== name);
    rest.push({ name, savedAt: new Date().toISOString(), regs: regs.map((r) => ({ id: r.id, value: r.value })) });
    this.write(rest);
  }

  remove(name: string): void {
    this.write(this.list().filter((p) => p.name !== name));
  }

  exportJson(): string {
    return JSON.stringify({ version: 1, presets: this.list() }, null, 2);
  }

  /** Merge by name (imported wins). Returns count imported; 0 and no change on invalid input. */
  importJson(json: string): number {
    const incoming = parse(json);
    if (!incoming || incoming.length === 0) return 0;
    const names = new Set(incoming.map((p) => p.name));
    this.write([...this.list().filter((p) => !names.has(p.name)), ...incoming]);
    return incoming.length;
  }
}
