// The status strip at the top of the Configuração panel: what the automatic read is doing and,
// once it worked, when the values on screen were taken from the ball.
import type { ConfigReadState } from '../ble/auto-read.ts';

export interface StripInput {
  connected: boolean;
  /** Last state announced by the automatic read on the current link (null before any). */
  read: ConfigReadState | null;
  /** Time of the last successful read, on this or an earlier link. */
  okAt: Date | null;
  /** Name of the IMU running on the ball, from the last read. */
  chip: string;
}
export interface StripView { text: string; retry: boolean }

const pad = (n: number): string => String(n).padStart(2, '0');
export const formatClock = (d: Date): string => `${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;

/** What the strip says, or null when it should not show (never connected, or only failures before a drop). */
export function stripView(i: StripInput): StripView | null {
  if (!i.connected) {
    return i.okAt ? { text: `Desconectado — mostrando a última leitura (${formatClock(i.okAt)})`, retry: false } : null;
  }
  if (i.read === 'reading') return { text: 'Lendo a configuração da bola…', retry: false };
  if (i.read === 'failed') return { text: 'Não consegui ler a configuração da bola.', retry: true };
  if (i.read === 'ok' && i.okAt) {
    return { text: `Valores lidos da bola às ${formatClock(i.okAt)} · IMU em uso: ${i.chip}`, retry: false };
  }
  return null;
}
