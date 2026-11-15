import { describe, expect, it } from 'vitest';
import * as THREE from 'three';
import { VIEW_PRESETS, VIEW_RADIUS, cameraPose, sensorToWorld } from './view-presets';

const close = (a: THREE.Vector3, b: THREE.Vector3) => expect(a.distanceTo(b)).toBeLessThan(1e-9);
const mount = new THREE.Euler(-Math.PI / 2, 0, 0);

describe('view presets', () => {
  it('lists the views in order with pt-BR labels', () => {
    expect(VIEW_PRESETS.map((p) => p.label)).toEqual(
      ['Frente', 'Trás', 'Direita', 'Esquerda', 'Superior', 'Inferior', 'ISO']);
  });

  for (const p of VIEW_PRESETS) {
    describe(p.id, () => {
      const pose = cameraPose(p.id);

      it('places the camera along the view direction at VIEW_RADIUS', () => {
        close(pose.position, sensorToWorld(p.dir).multiplyScalar(VIEW_RADIUS));
        close(pose.position, p.dir.clone().multiplyScalar(VIEW_RADIUS).applyEuler(mount));
        expect(pose.position.length()).toBeCloseTo(VIEW_RADIUS, 9);
      });

      it('looks at the origin', () => {
        const fwd = new THREE.Vector3(0, 0, -1).applyQuaternion(pose.quaternion);
        close(fwd, pose.position.clone().negate().normalize());
      });

      it('has the preset up vector as screen-up', () => {
        const up = new THREE.Vector3(0, 1, 0).applyQuaternion(pose.quaternion);
        // The up hint is projected onto the image plane (a no-op except for the oblique iso view).
        const d = sensorToWorld(p.dir).normalize();
        const hint = sensorToWorld(p.up);
        close(up, hint.sub(d.clone().multiplyScalar(hint.dot(d))).normalize());
      });
    });
  }

  it('iso position has equal-magnitude components', () => {
    const { position } = cameraPose('iso');
    expect(Math.abs(position.x)).toBeCloseTo(Math.abs(position.y), 9);
    expect(Math.abs(position.y)).toBeCloseTo(Math.abs(position.z), 9);
  });

  it('slerp between opposite and perpendicular views stays finite and unit', () => {
    const front = cameraPose('front').quaternion;
    for (const to of ['back', 'top'] as const) {
      const q = front.clone().slerp(cameraPose(to).quaternion, 0.5);
      for (const c of [q.x, q.y, q.z, q.w]) expect(Number.isFinite(c)).toBe(true);
      expect(q.length()).toBeCloseTo(1, 9);
    }
  });
});
