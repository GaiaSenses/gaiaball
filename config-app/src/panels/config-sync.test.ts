import { describe, expect, it } from 'vitest';
import { applyLabel, populatePlan } from './config-sync';
import { formatClock, stripView } from './config-status';

describe('populatePlan (what a read does to the form)', () => {
  const ball = new Map<number, number>([[1, 10], [2, 20], [3, 30]]);

  it('an automatic read overwrites untouched fields and keeps the edited ones, still marked', () => {
    const plan = populatePlan(ball.keys(), new Set([2, 9]), true);
    expect(plan.overwrite).toEqual([1, 3]);
    // 9 was never reported by the ball but stays an edit: the person (or a preset) set it.
    expect([...plan.edited].sort()).toEqual([2, 9]);
  });

  it('an explicit read discards every edit and shows the ball', () => {
    const plan = populatePlan(ball.keys(), new Set([2, 9]), false);
    expect(plan.overwrite).toEqual([1, 2, 3]);
    expect(plan.edited.size).toBe(0);
  });

  it('with nothing edited both modes overwrite everything', () => {
    expect(populatePlan(ball.keys(), new Set(), true).overwrite).toEqual([1, 2, 3]);
  });

  it('does not mutate the set it was given', () => {
    const edited = new Set([2]);
    populatePlan(ball.keys(), edited, false);
    expect([...edited]).toEqual([2]);
  });
});

describe('applyLabel', () => {
  it('is plain "Aplicar" with nothing to send', () => expect(applyLabel(0)).toBe('Aplicar'));
  it('carries the count of fields to send', () => expect(applyLabel(3)).toBe('Aplicar (3)'));
});

describe('config status strip', () => {
  const at = new Date(2026, 10, 17, 9, 8, 7);
  const base = { connected: true, read: null, okAt: null, chip: 'ICM-20948 ok' } as const;

  it('formats the clock as HH:MM:SS with zero padding', () => expect(formatClock(at)).toBe('09:08:07'));

  it('is absent before anything was ever read or tried', () => {
    expect(stripView(base)).toBeNull();
    expect(stripView({ ...base, connected: false })).toBeNull();
  });

  it('says it is reading', () => {
    expect(stripView({ ...base, read: 'reading' })).toEqual({ text: 'Lendo a configuração da bola…', retry: false });
  });

  it('shows when the values were read and which IMU is running', () => {
    expect(stripView({ ...base, read: 'ok', okAt: at })).toEqual({
      text: 'Valores lidos da bola às 09:08:07 · IMU em uso: ICM-20948 ok', retry: false,
    });
  });

  it('offers a retry when the read failed', () => {
    expect(stripView({ ...base, read: 'failed' })).toEqual({ text: 'Não consegui ler a configuração da bola.', retry: true });
  });

  it('after a disconnect, says the values on screen are the last reading', () => {
    expect(stripView({ ...base, connected: false, read: 'ok', okAt: at })).toEqual({
      text: 'Desconectado — mostrando a última leitura (09:08:07)', retry: false,
    });
  });

  it('after a disconnect that followed only failures there is nothing to show', () => {
    expect(stripView({ ...base, connected: false, read: 'failed', okAt: null })).toBeNull();
  });
});
