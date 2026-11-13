// Light/dark theme state. An explicit choice persists in localStorage; without
// one, the OS preference applies (and keeps applying live when it changes).
export type ThemeChoice = 'light' | 'dark';

const KEY = 'gaiaball-theme';
const EVENT = 'gaiaball-theme';
const mql = window.matchMedia('(prefers-color-scheme: light)');

function stored(): ThemeChoice | null {
  try {
    const v = localStorage.getItem(KEY);
    return v === 'light' || v === 'dark' ? v : null;
  } catch {
    return null;
  }
}

export function resolvedTheme(): ThemeChoice {
  return stored() ?? (mql.matches ? 'light' : 'dark');
}

function apply(): void {
  document.documentElement.dataset.theme = resolvedTheme();
  window.dispatchEvent(new CustomEvent<ThemeChoice>(EVENT, { detail: resolvedTheme() }));
}

export function toggleTheme(): void {
  const next: ThemeChoice = resolvedTheme() === 'dark' ? 'light' : 'dark';
  try {
    localStorage.setItem(KEY, next);
  } catch {
    // Private window / blocked storage: the choice just won't survive a reload.
  }
  apply();
}

export function onThemeChange(cb: (t: ThemeChoice) => void): void {
  window.addEventListener(EVENT, (ev) => cb((ev as CustomEvent<ThemeChoice>).detail));
}

export function initTheme(): void {
  mql.addEventListener('change', () => {
    if (!stored()) apply();
  });
  apply();
}
