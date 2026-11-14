import * as THREE from 'three';
import { VIEW_RADIUS, cameraPose, type ViewId } from './view-presets';

export interface Quat { w: number; x: number; y: number; z: number }
export type ModelKind = 'plane' | 'box';

/** Dart-shaped paper plane, nose toward +X (sensor forward). Flat-shaded vertex colors. */
function buildPlane(): THREE.Object3D {
  // Sensor frame: +X nose, +Y the model's left, +Z up (so -Y is the right wing).
  const nose = [1, 0, 0], l = [-0.6, -0.8, 0], r = [-0.6, 0.8, 0], k = [-0.6, 0, -0.3], t = [-0.6, 0, 0.0];
  const tri = (a: number[], b: number[], c: number[], col: number[]) =>
    ({ v: [...a, ...b, ...c], c: [...col, ...col, ...col] });
  const faces = [
    tri(nose, l, t, [0.9, 0.9, 0.95]), // right wing (-Y)
    tri(nose, t, r, [0.75, 0.78, 0.88]), // left wing (+Y)
    tri(nose, k, l, [0.35, 0.6, 0.95]), // right keel side (-Y)
    tri(nose, r, k, [0.95, 0.6, 0.3]), // left keel side (+Y)
    tri(k, t, l, [0.5, 0.5, 0.55]), // rear right (-Y)
    tri(k, r, t, [0.4, 0.4, 0.45]), // rear left (+Y)
  ];
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(faces.flatMap((f) => f.v), 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(faces.flatMap((f) => f.c), 3));
  geo.computeVertexNormals();
  const g = new THREE.Group();
  g.add(new THREE.Mesh(geo, new THREE.MeshStandardMaterial({ vertexColors: true, flatShading: true, side: THREE.DoubleSide })));
  const tip = new THREE.Mesh(new THREE.SphereGeometry(0.1, 16, 12), new THREE.MeshStandardMaterial({ color: 0xff3b30, emissive: 0x551100 }));
  tip.position.set(1, 0, 0); // nose marker
  g.add(tip);
  return g;
}

/** Box with a distinct color per face (+X red ... -Z dark) so rotation reads clearly. */
function buildBox(): THREE.Object3D {
  const cols = [0xe74c3c, 0x7a2a22, 0x2ecc71, 0x1b6b3d, 0x3498db, 0x1d4f75];
  const mats = cols.map((color) => new THREE.MeshStandardMaterial({ color }));
  return new THREE.Mesh(new THREE.BoxGeometry(1.2, 0.8, 0.5), mats);
}

const VIEW_ANIM_MS = 450;

/** Renders the treated quaternion exactly as received over BLE — no extra smoothing
 *  in the UI, so what you see is what the firmware sends (including its own slerp,
 *  when enabled in the device config). */
export class OrientationScene {
  private renderer: THREE.WebGLRenderer;
  private scene = new THREE.Scene();
  private camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100);
  // Sensor frame is Z-up, three.js is Y-up: this fixed -90 deg rotation about X maps
  // sensor +Z to screen-up. The model lives inside it, so quaternions are applied as-is.
  private container = new THREE.Object3D();
  private body = new THREE.Object3D();
  private model: THREE.Object3D | null = null;
  private grid: THREE.GridHelper | null = null;
  private raf = 0;
  private disposed = false;
  private ro: ResizeObserver;
  private anim: { from: THREE.Quaternion; to: THREE.Quaternion; t0: number } | null = null;
  private view: ViewId | null = null;
  private onVis = () => { if (document.hidden) this.stop(); else this.start(); };

  private canvas: HTMLCanvasElement;

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.container.rotation.x = -Math.PI / 2; // fixed mount rotation, not an orientation path
    this.scene.add(this.container);
    const holder = new THREE.Object3D();
    holder.add(this.body);
    holder.add(new THREE.AxesHelper(1.1));
    this.container.add(holder);
    this.setDark(true);
    this.scene.add(new THREE.HemisphereLight(0xcfd8ff, 0x22252b, 1.1));
    this.camera.position.set(0, 2.2, 5.2);
    this.camera.lookAt(0, 0, 0);
    // Headlight: a child of the camera, offset so that in the initial view it sits at
    // world (3, 5, 4) exactly as the old fixed light did; it then follows view changes.
    this.scene.add(this.camera);
    this.camera.updateMatrixWorld(true);
    const sun = new THREE.DirectionalLight(0xffffff, 1.4);
    sun.position.copy(this.camera.worldToLocal(new THREE.Vector3(3, 5, 4)));
    this.camera.add(sun);
    sun.target.position.set(0, 0, 0);
    this.scene.add(sun.target);
    this.setModel('plane');
    this.ro = new ResizeObserver(() => this.resize());
    this.ro.observe(canvas.parentElement ?? canvas);
    this.resize();
    document.addEventListener('visibilitychange', this.onVis);
    this.start();
  }

  setQuaternion(q: Quat): void {
    this.body.quaternion.set(q.x, q.y, q.z, q.w); // THREE order is (x, y, z, w)
  }

  setModel(kind: ModelKind): void {
    if (this.model) {
      this.body.remove(this.model);
      disposeTree(this.model);
    }
    this.model = kind === 'plane' ? buildPlane() : buildBox();
    this.body.add(this.model);
  }

  /** Snaps the camera to a CAD-style preset view (animated unless reduced motion). */
  setView(id: ViewId): void {
    const pose = cameraPose(id);
    this.view = id;
    const reduce = typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (reduce) {
      this.anim = null;
      this.applyCamera(pose.quaternion);
    } else {
      this.anim = { from: this.camera.quaternion.clone(), to: pose.quaternion, t0: performance.now() };
    }
  }

  /** The last preset chosen, or null while the initial view is untouched. */
  currentView(): ViewId | null { return this.view; }

  private applyCamera(q: THREE.Quaternion): void {
    this.camera.quaternion.copy(q);
    this.camera.position.set(0, 0, VIEW_RADIUS).applyQuaternion(q);
  }

  private stepAnim(now: number): void {
    const a = this.anim;
    if (!a) return;
    const k = Math.min(1, (now - a.t0) / VIEW_ANIM_MS);
    const e = k < 0.5 ? 2 * k * k : 1 - Math.pow(-2 * k + 2, 2) / 2; // ease-in-out quad
    this.applyCamera(a.from.clone().slerp(a.to, e));
    if (k >= 1) this.anim = null;
  }

  /** Matches the scene to the page theme (background + floor grid contrast). */
  setDark(dark: boolean): void {
    this.scene.background = new THREE.Color(dark ? 0x10161d : 0xe3eae7);
    if (this.grid) {
      this.scene.remove(this.grid);
      disposeTree(this.grid);
    }
    this.grid = dark
      ? new THREE.GridHelper(8, 16, 0x3a4552, 0x242d37)
      : new THREE.GridHelper(8, 16, 0xa8b8b2, 0xc8d4d0);
    this.grid.position.y = -0.9;
    this.scene.add(this.grid);
  }

  dispose(): void {
    this.disposed = true;
    this.stop();
    document.removeEventListener('visibilitychange', this.onVis);
    this.ro.disconnect();
    this.scene.traverse((o) => disposeObj(o));
    this.renderer.dispose();
  }

  private resize(): void {
    const host = this.canvas.parentElement ?? this.canvas;
    const w = Math.max(1, host.clientWidth), h = Math.max(1, host.clientHeight);
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  private start(): void {
    if (this.raf || this.disposed) return;
    const loop = () => {
      this.raf = requestAnimationFrame(loop);
      this.stepAnim(performance.now());
      this.renderer.render(this.scene, this.camera);
    };
    this.raf = requestAnimationFrame(loop);
  }

  private stop(): void {
    if (this.raf) cancelAnimationFrame(this.raf);
    this.raf = 0;
  }
}

function disposeObj(o: THREE.Object3D): void {
  const m = o as THREE.Mesh;
  m.geometry?.dispose?.();
  const mat = m.material;
  if (Array.isArray(mat)) mat.forEach((x) => x.dispose());
  else mat?.dispose?.();
}
function disposeTree(o: THREE.Object3D): void { o.traverse(disposeObj); }
