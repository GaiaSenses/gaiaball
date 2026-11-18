// The PIN unlock, as the person sees it. Firmware with the read-only TLV 0x37 reports, on every
// configuration read, whether the READING connection is unlocked; older firmware cannot tell.
import { TLV } from '../protocol/tlv.ts';

export type LockState = 'locked' | 'unlocked' | 'locked-out' | 'unknown';

const BY_BYTE: readonly LockState[] = ['locked', 'unlocked', 'locked-out'];

/** This connection's unlock state from a configuration read; 'unknown' without a valid 0x37. */
export function lockStateFrom(regs: Map<number, DataView>): LockState {
  const dv = regs.get(TLV.unlockState);
  if (!dv || dv.byteLength !== 1) return 'unknown';
  return BY_BYTE[dv.getUint8(0)] ?? 'unknown';
}

/** What the Desbloquear button says once the ball has answered. */
export function unlockResultMessage(state: LockState): string {
  switch (state) {
    case 'unlocked':
      return 'Desbloqueada: comandos e Aplicar liberados até desconectar.';
    case 'locked':
      return 'PIN incorreto: a bola continua bloqueada. O PIN de fábrica é 1234 (se alguém trocou, use o novo).';
    case 'locked-out':
      return 'Muitas tentativas com PIN errado: a bola recusa novos PINs por 30 s. Espere e tente de novo.';
    default:
      return 'PIN enviado. O firmware desta bola não confirma o desbloqueio: o Aplicar confere cada valor e avisa se a bola recusar.';
  }
}

/** Short label next to the PIN field; empty when the firmware cannot tell. */
export function lockBadgeText(state: LockState): string {
  switch (state) {
    case 'locked': return 'Bloqueada';
    case 'unlocked': return 'Desbloqueada';
    case 'locked-out': return 'Bloqueada por 30 s';
    default: return '';
  }
}

/**
 * Chrome reports a link that dropped (or never answered) in the middle of an operation as a bare
 * English "GATT …" error. Says what happened and what to do; the original text stays as a detail.
 */
export function bleErrorMessage(e: unknown): string {
  const text = e instanceof Error || e instanceof DOMException ? e.message : String(e);
  const name = e instanceof Error || e instanceof DOMException ? e.name : '';
  if (name !== 'NetworkError' && !/\bGATT\b/.test(text)) return text;
  return 'A bola não respondeu pelo Bluetooth: a conexão caiu ou está instável (acontece quando a bola reinicia ou fica longe). '
    + 'Se o status mostrar Desconectado, conecte de novo e desbloqueie outra vez: o desbloqueio vale só para a conexão atual. '
    + `(detalhe: ${text})`;
}
