import * as THREE from 'three';

export interface Quat { w: number; x: number; y: number; z: number }
export type ModelKind = 'plane' | 'box';
export type ViewMode = 'both' | 'raw' | 'filtered';

const OFFSET_X = 1.2;

/** Dart-shaped paper plane, nose toward +X (sensor forward). Flat-shaded vertex colors. */
function buildPlane(): THREE.Object3D {
  const nose = [1, 0, 0], l = [-0.6, -0.8, 0], r = [-0.6, 0.8, 0], k = [-0.6, 0, -0.3], t = [-0.6, 0, 0.0];
  const tri = (a: number[], b: number[], c: number[], col: number[]) =>
    ({ v: [...a, ...b, ...c], c: [...col, ...col, ...col] });
  const faces = [
    tri(nose, l, t, [0.9, 0.9, 0.95]), // left wing
    tri(nose, t, r, [0.75, 0.78, 0.88]), // right wing
    tri(nose, k, l, [0.35, 0.6, 0.95]), // left keel side
    tri(nose, r, k, [0.95, 0.6, 0.3]), // right keel side
    tri(k, t, l, [0.5, 0.5, 0.55]), // rear left
    tri(k, r, t, [0.4, 0.4, 0.45]), // rear right
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

export class OrientationScene {
  private renderer: THREE.WebGLRenderer;
  private scene = new THREE.Scene();
  private camera = new THREE.PerspectiveCamera(45, 1, 0.1, 100);
  // Sensor frame is Z-up, three.js is Y-up: this fixed -90 deg rotation about X maps
  // sensor +Z to screen-up. Models live inside it, so quaternions are applied as-is.
  private container = new THREE.Object3D();
  private rawHolder = new THREE.Object3D();
  private filtHolder = new THREE.Object3D();
  private rawBody = new THREE.Object3D();
  private filtBody = new THREE.Object3D();
  private rawModel: THREE.Object3D | null = null;
  private filtModel: THREE.Object3D | null = null;
  private target = new THREE.Quaternion();
  private grid: THREE.GridHelper | null = null;
  private smoothing = 0;
  private view: ViewMode = 'both';
  private raf = 0;
  private disposed = false;
  private ro: ResizeObserver;
  private onVis = () => { if (document.hidden) this.stop(); else this.start(); };

  private canvas: HTMLCanvasElement;

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
    this.container.rotation.x = -Math.PI / 2; // fixed mount rotation, not an orientation path
    this.scene.add(this.container);
    for (const [holder, body] of [[this.rawHolder, this.rawBody], [this.filtHolder, this.filtBody]] as const) {
      holder.add(body);
      holder.add(new THREE.AxesHelper(1.1));
      this.container.add(holder);
    }
    this.setDark(true);
    this.scene.add(new THREE.HemisphereLight(0xcfd8ff, 0x22252b, 1.1));
    const sun = new THREE.DirectionalLight(0xffffff, 1.4);
    sun.position.set(3, 5, 4);
    this.scene.add(sun);
    this.camera.position.set(0, 2.2, 5.2);
    this.camera.lookAt(0, 0, 0);
    this.setModel('plane');
    this.applyView();
    this.ro = new ResizeObserver(() => this.resize());
    this.ro.observe(canvas.parentElement ?? canvas);
    this.resize();
    document.addEventListener('visibilitychange', this.onVis);
    this.start();
  }

  setQuaternion(q: Quat): void {
    this.target.set(q.x, q.y, q.z, q.w); // THREE order is (x, y, z, w)
    this.rawBody.quaternion.copy(this.target);
  }

  setSmoothing(amount: number): void {
    this.smoothing = Math.min(1, Math.max(0, amount));
  }

  setModel(kind: ModelKind): void {
    for (const [body, old] of [[this.rawBody, this.rawModel], [this.filtBody, this.filtModel]] as const) {
      if (old) { body.remove(old); disposeTree(old); }
    }
    this.rawModel = kind === 'plane' ? buildPlane() : buildBox();
    this.filtModel = kind === 'plane' ? buildPlane() : buildBox();
    this.rawBody.add(this.rawModel);
    this.filtBody.add(this.filtModel);
  }

  setView(mode: ViewMode): void {
    this.view = mode;
    this.applyView();
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

  private applyView(): void {
    this.rawHolder.visible = this.view !== 'filtered';
    this.filtHolder.visible = this.view !== 'raw';
    const both = this.view === 'both';
    this.rawHolder.position.x = both ? -OFFSET_X : 0;
    this.filtHolder.position.x = both ? OFFSET_X : 0;
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
      // 0 -> instant copy; higher smoothing -> smaller slerp step per frame.
      this.filtBody.quaternion.slerp(this.target, 1 - this.smoothing * 0.9);
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
