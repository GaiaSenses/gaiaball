// Didactic "i" buttons: every parameter gets a small round button whose tooltip
// (hover, keyboard focus or tap) explains what the parameter does and its unit.
// On narrow screens the tooltip renders as a fixed sheet at the bottom (CSS).

export function infoDot(text: string): HTMLButtonElement {
  const b = document.createElement('button');
  b.type = 'button';
  b.className = 'info-dot';
  b.dataset.tip = text;
  b.setAttribute('aria-label', text);
  b.textContent = 'i';
  // Inside a <label>, a click would activate the wrapped input; keep the tap on
  // the dot itself so touch users get the tooltip (shown while focused).
  b.addEventListener('click', (e) => {
    e.preventDefault();
    e.stopPropagation();
    b.focus();
  });
  return b;
}

/** Appends an info dot to every element carrying a data-info attribute. */
export function decorateInfo(root: ParentNode): void {
  for (const el of root.querySelectorAll<HTMLElement>('[data-info]')) {
    const text = el.dataset.info;
    if (text) el.appendChild(infoDot(text));
    el.removeAttribute('data-info');
  }
}
