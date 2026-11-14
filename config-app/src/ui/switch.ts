/**
 * Slide switch for a two-way choice ("Absoluto ⟷ Adaptativo"). Both labels stay visible; the
 * track knob sits on the side of the selected one. Checked (aria-checked = true) means the SECOND
 * option. Space/Enter toggle, ArrowLeft/ArrowRight pick the first/second option.
 */
export interface SwitchOption { value: number; label: string }
export type SwitchOptions = readonly [SwitchOption, SwitchOption];

export interface SwitchSpec {
  options: SwitchOptions;
  value: number;
  onChange(v: number): void;
  /** Accessible name of the whole control. */
  label: string;
}

/** Pure toggling logic. `side` forces the first ('left') or second ('right') option. */
export function nextSwitchValue(options: SwitchOptions, value: number, side?: 'left' | 'right'): number {
  if (side === 'left') return options[0].value;
  if (side === 'right') return options[1].value;
  return value === options[0].value ? options[1].value : options[0].value;
}

/** Accessible name that carries the state: aria-checked only says "second option", which means nothing alone. */
export function switchAriaLabel(label: string, options: SwitchOptions, value: number): string {
  const cur = value === options[1].value ? options[1] : options[0];
  return `${label}: ${cur.label}`;
}

const NAMES = new WeakMap<HTMLElement, string>();
const OPTIONS = new WeakMap<HTMLElement, SwitchOptions>();

/** Moves an already rendered switch to `value` without firing onChange (programmatic updates). */
export function setSwitchValue(el: HTMLElement, value: number): void {
  const options = OPTIONS.get(el);
  if (!options) return;
  const second = value === options[1].value;
  el.setAttribute('aria-checked', String(second));
  el.dataset.value = String(value);
  el.classList.toggle('on', second);
  el.setAttribute('aria-label', switchAriaLabel(NAMES.get(el) ?? '', options, value));
}

export function renderSwitch(opts: SwitchSpec): HTMLElement {
  const el = document.createElement('button');
  el.type = 'button';
  el.className = 'switch';
  el.setAttribute('role', 'switch');
  OPTIONS.set(el, opts.options);
  NAMES.set(el, opts.label);
  const mk = (cls: string, text: string): HTMLSpanElement => {
    const s = document.createElement('span');
    s.className = cls;
    s.textContent = text;
    return s;
  };
  const track = mk('switch-track', '');
  track.append(mk('switch-knob', ''));
  track.setAttribute('aria-hidden', 'true');
  el.append(mk('switch-opt switch-opt-a', opts.options[0].label), track, mk('switch-opt switch-opt-b', opts.options[1].label));
  setSwitchValue(el, opts.value);

  const set = (side?: 'left' | 'right'): void => {
    const cur = Number(el.dataset.value);
    const next = nextSwitchValue(opts.options, cur, side);
    if (next === cur) return;
    setSwitchValue(el, next);
    opts.onChange(next);
  };
  el.addEventListener('click', () => set());
  el.addEventListener('keydown', (ev) => {
    if (ev.key === 'ArrowLeft') { ev.preventDefault(); set('left'); }
    else if (ev.key === 'ArrowRight') { ev.preventDefault(); set('right'); }
    // Space and Enter already fire "click" on a <button>.
  });
  return el;
}
