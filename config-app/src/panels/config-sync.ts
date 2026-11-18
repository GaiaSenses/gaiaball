// How a configuration read meets what is already in the form, and the Aplicar label.

/**
 * Which form fields a read overwrites and which edits survive it.
 *
 * "Edited" means: the person typed or picked a value, or loaded a preset, since the last
 * explicit read or apply (the panel's `dirty` set). An automatic read (on connect, the stage
 * bar's read-back) keeps those fields as they are, so a preset prepared offline or a half-typed
 * change is not wiped; they stay marked as differing from the ball. An explicit read
 * ("Ler do dispositivo") or the read-back after Aplicar discards the edits.
 */
export function populatePlan(
  reportedIds: Iterable<number>, edited: ReadonlySet<number>, keepEdits: boolean,
): { overwrite: number[]; edited: Set<number> } {
  const overwrite: number[] = [];
  for (const id of reportedIds) if (!(keepEdits && edited.has(id))) overwrite.push(id);
  return { overwrite, edited: keepEdits ? new Set(edited) : new Set() };
}

export const applyLabel = (pending: number): string => (pending > 0 ? `Aplicar (${pending})` : 'Aplicar');
