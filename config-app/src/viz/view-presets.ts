import * as THREE from 'three';

export type ViewId = 'front' | 'back' | 'right' | 'left' | 'top' | 'bottom' | 'iso';

export interface ViewPreset {
  id: ViewId;
  label: string;
  /** Camera direction from the origin, sensor frame (+X nose, +Y left, +Z up). */
  dir: THREE.Vector3;
  /** Screen-up vector, sensor frame. */
  up: THREE.Vector3;
}

const v = (x: number, y: number, z: number) => new THREE.Vector3(x, y, z);
const S = 1 / Math.sqrt(3);

export const VIEW_PRESETS: readonly ViewPreset[] = [
  { id: 'front', label: 'Frente', dir: v(1, 0, 0), up: v(0, 0, 1) },
  { id: 'back', label: 'Trás', dir: v(-1, 0, 0), up: v(0, 0, 1) },
  { id: 'right', label: 'Direita', dir: v(0, -1, 0), up: v(0, 0, 1) },
  { id: 'left', label: 'Esquerda', dir: v(0, 1, 0), up: v(0, 0, 1) },
  // CAD third-angle convention: looking down, the nose points down on screen.
  { id: 'top', label: 'Superior', dir: v(0, 0, 1), up: v(-1, 0, 0) },
  { id: 'bottom', label: 'Inferior', dir: v(0, 0, -1), up: v(1, 0, 0) },
  { id: 'iso', label: 'ISO', dir: v(S, -S, S), up: v(0, 0, 1) },
];

/** Distance of the initial camera (0, 2.2, 5.2) from the origin. */
export const VIEW_RADIUS = Math.hypot(2.2, 5.2);

/** Sensor frame (Z-up) to three.js world (Y-up): (x, y, z) -> (x, z, -y). */
export function sensorToWorld(s: THREE.Vector3): THREE.Vector3 {
  return new THREE.Vector3(s.x, s.z, -s.y);
}

export function cameraPose(id: ViewId): { position: THREE.Vector3; quaternion: THREE.Quaternion } {
  const p = VIEW_PRESETS.find((x) => x.id === id)!;
  const position = sensorToWorld(p.dir).multiplyScalar(VIEW_RADIUS);
  const m = new THREE.Matrix4().lookAt(position, new THREE.Vector3(), sensorToWorld(p.up));
  return { position, quaternion: new THREE.Quaternion().setFromRotationMatrix(m) };
}
