import { describe, expect, it } from 'vitest';
import { nextSwitchValue } from './switch';

const opts = [{ value: 0, label: 'A' }, { value: 1, label: 'B' }] as const;
const rev = [{ value: 1, label: 'Pot' }, { value: 0, label: 'Fixo' }] as const;

describe('nextSwitchValue', () => {
  it('toggles to the other option', () => {
    expect(nextSwitchValue(opts, 0)).toBe(1);
    expect(nextSwitchValue(opts, 1)).toBe(0);
    expect(nextSwitchValue(rev, 1)).toBe(0);
    expect(nextSwitchValue(rev, 0)).toBe(1);
  });
  it('arrow keys go to a specific side; unknown value falls to the first option', () => {
    expect(nextSwitchValue(opts, 0, 'right')).toBe(1);
    expect(nextSwitchValue(opts, 1, 'right')).toBe(1);
    expect(nextSwitchValue(opts, 1, 'left')).toBe(0);
    expect(nextSwitchValue(opts, 9)).toBe(0);
  });
});
