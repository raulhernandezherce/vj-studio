export type Pt = [number, number];

export interface Mask { points: Pt[]; invert: boolean; smooth: boolean }

export interface Slice {
  id: number;
  name: string;
  sourceType: string;
  sourceIndex: number;
  ignoreBypass: boolean;
  bypass: boolean;
  inX: number;
  inY: number;
  inW: number;
  inH: number;
  inRotation: number;
  polygon: Pt[];
  masks: Mask[];
  corners: Pt[];
  warpMode: string;
  warpColumns: number;
  warpRows: number;
  warp: Pt[];
  flipH: boolean;
  flipV: boolean;
  brightness: number;
  contrast: number;
  red: number;
  green: number;
  blue: number;
  isKey: boolean;
  blackBg: boolean;
  blend: boolean;
  blendLeft: number;
  blendRight: number;
  blendTop: number;
  blendBottom: number;
  blendPower: number;
  blendLuminance: number;
  blendGamma: number;
  blackLevel: number;
  blendGammaRed: number;
  blendGammaGreen: number;
  blendGammaBlue: number;
  contentX: number;
  contentY: number;
  contentScale: number;
  contentRotation: number;
  outline: boolean;
  outlineOnly: boolean;
  outlineWidth: number;
  outlineRed: number;
  outlineGreen: number;
  outlineBlue: number;
}

export interface Screen {
  id: number;
  name: string;
  device: string;
  width: number;
  height: number;
  enabled: boolean;
  opacity: number;
  brightness: number;
  contrast: number;
  red: number;
  green: number;
  blue: number;
  slices: Slice[];
  masks: Mask[];
  inputGuide: string;
  outputGuide: string;
  guideOpacity: number;
  delay: number;
}

export interface Setup {
  format: string;
  version: number;
  name: string;
  screens: Screen[];
}

export function newSlice(id: number): Slice {
  return {
    id, name: `Slice ${id}`, sourceType: 'composition', sourceIndex: 0, ignoreBypass: false, bypass: false,
    inX: 0, inY: 0, inW: 1, inH: 1, inRotation: 0, polygon: [], masks: [], corners: [[0, 0], [1, 0], [1, 1], [0, 1]],
    warpMode: 'linear', warpColumns: 2, warpRows: 2, warp: [], flipH: false, flipV: false,
    brightness: 1, contrast: 1, red: 1, green: 1, blue: 1, isKey: false, blackBg: false,
    blend: false, blendLeft: 0, blendRight: 0, blendTop: 0, blendBottom: 0, blendPower: 2, blendLuminance: 0.5, blendGamma: 1.8, blackLevel: 0, blendGammaRed: 1, blendGammaGreen: 1, blendGammaBlue: 1,
    contentX: 0, contentY: 0, contentScale: 1, contentRotation: 0, outline: false, outlineOnly: false, outlineWidth: 0.005, outlineRed: 1, outlineGreen: 1, outlineBlue: 1,
  };
}

export function newScreen(id: number, device: string, width = 1920, height = 1080): Screen {
  return {
    id, name: `Pantalla ${id}`, device, width, height, enabled: true, opacity: 1, brightness: 1, contrast: 1, red: 1, green: 1, blue: 1,
    slices: [newSlice(1)], masks: [], inputGuide: '', outputGuide: '', guideOpacity: 0.5, delay: 0,
  };
}

export const nextId = (items: { id: number }[]) => items.reduce((m, i) => Math.max(m, i.id), 0) + 1;

// Parte la composición en n pantallas iguales, de izquierda a derecha.
export function splitHorizontally(setup: Setup): Setup {
  const n = setup.screens.length;
  return {
    ...setup,
    screens: setup.screens.map((s, i) => ({
      ...s,
      slices: [{ ...s.slices[0], inX: i / n, inY: 0, inW: 1 / n, inH: 1 }, ...s.slices.slice(1)],
    })),
  };
}

// Solape para edge blending entre pantallas vecinas: cada una toma un poco de la siguiente.
export function overlapForBlend(setup: Setup, overlap: number): Setup {
  const n = setup.screens.length;
  const base = 1 / (n - (n - 1) * overlap);
  return {
    ...setup,
    screens: setup.screens.map((s, i) => {
      const inX = i * base * (1 - overlap);
      return {
        ...s,
        slices: [{ ...s.slices[0], inX, inY: 0, inW: base, inH: 1, blend: true, blendLeft: i > 0 ? overlap : 0, blendRight: i < n - 1 ? overlap : 0 }, ...s.slices.slice(1)],
      };
    }),
  };
}

export function warpGrid(cols: number, rows: number): Pt[] {
  const out: Pt[] = [];
  for (let r = 0; r < rows; ++r) for (let c = 0; c < cols; ++c) out.push([c / (cols - 1), r / (rows - 1)]);
  return out;
}

export const regularPolygon = (sides: number): Pt[] =>
  Array.from({ length: sides }, (_, i) => {
    const a = (i / sides) * Math.PI * 2 - Math.PI / 2;
    return [0.5 + Math.cos(a) * 0.5, 0.5 + Math.sin(a) * 0.5] as Pt;
  });

// Mismo trazado que el motor, para ver en el lienzo la curva que saldrá.
export function maskOutline(m: Mask): Pt[] {
  if (!m.smooth) return m.points;
  const n = m.points.length, out: Pt[] = [];
  const f = (a: number, b: number, c: number, d: number, t: number) => 0.5 * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t * t * t);
  for (let i = 0; i < n; ++i) for (let k = 0; k < 12; ++k) {
    const [a, b, c, d] = [m.points[(i + n - 1) % n], m.points[i], m.points[(i + 1) % n], m.points[(i + 2) % n]];
    out.push([f(a[0], b[0], c[0], d[0], k / 12), f(a[1], b[1], c[1], d[1], k / 12)]);
  }
  return out;
}

// Corazón de 16 puntos para usar con la máscara suave.
export const HEART: Pt[] = Array.from({ length: 16 }, (_, i) => {
  const t = (i / 16) * Math.PI * 2;
  return [0.5 + 0.024 * 16 * Math.sin(t) ** 3, 0.45 - 0.024 * (13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t))] as Pt;
});

export const clamp01 = (v: number) => Math.min(1, Math.max(0, v));

// Mueve la zona de entrada sin salirse de la composición.
export function moveInput(s: Slice, dx: number, dy: number): Slice {
  return { ...s, inX: Math.min(1 - s.inW, Math.max(0, s.inX + dx)), inY: Math.min(1 - s.inH, Math.max(0, s.inY + dy)) };
}

const snapValue = (v: number, targets: number[], range: number) => {
  for (const t of targets) if (Math.abs(v - t) < range) return t;
  return v;
};

// Imán a bordes y centro, como en el manual.
export const snap = (v: number, range = 0.012) => snapValue(v, [0, 0.25, 0.5, 0.75, 1], range);

export function duplicateSlice(screen: Screen, slice: Slice): { screen: Screen; id: number } {
  const id = nextId(screen.slices);
  const copy: Slice = { ...structuredClone(slice), id, name: `${slice.name} copia` };
  const at = screen.slices.findIndex((s) => s.id === slice.id) + 1;
  return { screen: { ...screen, slices: [...screen.slices.slice(0, at), copy, ...screen.slices.slice(at)] }, id };
}

// La copia nunca hereda la salida física: dos pantallas no pueden usar la misma.
export function duplicateScreen(setup: Setup, screen: Screen): { setup: Setup; id: number } {
  const id = nextId(setup.screens);
  const copy: Screen = { ...structuredClone(screen), id, name: `${screen.name} copia`, device: 'virtual' };
  return { setup: { ...setup, screens: [...setup.screens, copy] }, id };
}

export interface Rect { left: number; top: number; width: number; height: number }

export const cornersRect = (corners: Pt[]): Rect => {
  const xs = corners.map((c) => c[0]), ys = corners.map((c) => c[1]);
  const left = Math.min(...xs), top = Math.min(...ys);
  return { left, top, width: Math.max(...xs) - left, height: Math.max(...ys) - top };
};

export const rectCorners = (r: Rect): Pt[] => [[r.left, r.top], [r.left + r.width, r.top], [r.left + r.width, r.top + r.height], [r.left, r.top + r.height]];

// Cambia el rectángulo de la salida conservando la deformación relativa de las esquinas.
export function fitCorners(corners: Pt[], next: Rect): Pt[] {
  const r = cornersRect(corners);
  return corners.map(([x, y]) => [
    next.left + (r.width ? (x - r.left) / r.width : 0) * next.width,
    next.top + (r.height ? (y - r.top) / r.height : 0) * next.height,
  ] as Pt);
}

// La salida copia la zona de entrada, en píxeles: una región de 500 px ocupa 500 px en la pantalla.
export function matchOutputToInput(s: Slice, comp: [number, number], out: [number, number]): Partial<Slice> {
  return { corners: rectCorners({ left: s.inX * comp[0] / out[0], top: s.inY * comp[1] / out[1], width: s.inW * comp[0] / out[0], height: s.inH * comp[1] / out[1] }), warp: [] };
}

export function matchInputToOutput(s: Slice, comp: [number, number], out: [number, number]): Partial<Slice> {
  const r = cornersRect(s.corners);
  const inW = Math.min(1, r.width * out[0] / comp[0]), inH = Math.min(1, r.height * out[1] / comp[1]);
  return { inW, inH, inX: Math.min(1 - inW, Math.max(0, r.left * out[0] / comp[0])), inY: Math.min(1 - inH, Math.max(0, r.top * out[1] / comp[1])) };
}

// Inserta un punto en el lado más cercano al clic.
export function insertPoint(points: Pt[], at: Pt): Pt[] {
  let best = 0, bestDistance = Infinity;
  points.forEach((a, i) => {
    const b = points[(i + 1) % points.length];
    const dx = b[0] - a[0], dy = b[1] - a[1];
    const t = clamp01(((at[0] - a[0]) * dx + (at[1] - a[1]) * dy) / (dx * dx + dy * dy || 1));
    const d = Math.hypot(a[0] + t * dx - at[0], a[1] + t * dy - at[1]);
    if (d < bestDistance) { bestDistance = d; best = i; }
  });
  return [...points.slice(0, best + 1), at, ...points.slice(best + 1)];
}

export const removePoint = (points: Pt[], index: number): Pt[] => (points.length > 3 ? points.filter((_, i) => i !== index) : points);

// Clic normal: solo ese. Con Cmd o Ctrl: lo añade o lo quita, sin dejar la selección vacía.
export function toggleSelection(selected: number[], id: number, additive: boolean): number[] {
  if (!additive) return [id];
  if (!selected.includes(id)) return [...selected, id];
  return selected.length > 1 ? selected.filter((s) => s !== id) : selected;
}

// Cálculo sencillo en un campo: "1920/3" o "100+20*2". Nunca evalúa código.
export function parseExpression(text: string): number | null {
  const tokens = text.replace(/\s+/g, '').replace(/,/g, '.').match(/\d*\.?\d+|[-+*/]/g);
  if (!tokens || tokens.join('') !== text.replace(/\s+/g, '').replace(/,/g, '.')) return null;
  const values: number[] = [];
  const ops: string[] = [];
  let i = 0;
  const number = (): number | null => {
    let sign = 1;
    while (tokens[i] === '-' || tokens[i] === '+') { if (tokens[i] === '-') sign = -sign; i += 1; }
    const v = Number(tokens[i]);
    if (!Number.isFinite(v) || tokens[i] === undefined) return null;
    i += 1;
    return sign * v;
  };
  const first = number();
  if (first === null) return null;
  values.push(first);
  while (i < tokens.length) {
    const op = tokens[i];
    i += 1;
    const v = number();
    if (v === null) return null;
    if (op === '*') values[values.length - 1] *= v;
    else if (op === '/') {
      if (v === 0) return null;
      values[values.length - 1] /= v;
    } else {
      ops.push(op);
      values.push(v);
    }
  }
  return ops.reduce((sum, op, k) => (op === '+' ? sum + values[k + 1] : sum - values[k + 1]), values[0]);
}

// Bordes y centro de los demás slices, para el imán entre slices.
export function snapTargets(slices: Slice[], ignore: number[]): { x: number[]; y: number[] } {
  const x = [0, 0.5, 1], y = [0, 0.5, 1];
  for (const s of slices) {
    if (ignore.includes(s.id)) continue;
    x.push(s.inX, s.inX + s.inW / 2, s.inX + s.inW);
    y.push(s.inY, s.inY + s.inH / 2, s.inY + s.inH);
  }
  return { x, y };
}

// Corrección para que un rango [a, b] caiga en el objetivo más cercano; 0 si ninguno queda cerca.
export function snapOffset(a: number, b: number, targets: number[], range = 0.008): number {
  let best = 0, bestDistance = range;
  for (const t of targets) for (const v of [a, (a + b) / 2, b]) {
    const d = Math.abs(t - v);
    if (d < bestDistance) { bestDistance = d; best = t - v; }
  }
  return best;
}

export const intersects = (s: Slice, r: Rect) => s.inX < r.left + r.width && s.inX + s.inW > r.left && s.inY < r.top + r.height && s.inY + s.inH > r.top;

// Divide un slice en n partes iguales, entrada y salida a la vez.
export function splitSlice(screen: Screen, slice: Slice, n: number, vertical: boolean): Screen {
  const out = cornersRect(slice.corners);
  const parts: Slice[] = [];
  let id = nextId(screen.slices);
  for (let i = 0; i < n; ++i) {
    const part: Slice = { ...structuredClone(slice), id: i === 0 ? slice.id : id++, name: `${slice.name} ${i + 1}` };
    const f = i / n, w = 1 / n;
    if (vertical) {
      part.inY = slice.inY + slice.inH * f;
      part.inH = slice.inH * w;
      part.corners = rectCorners({ left: out.left, top: out.top + out.height * f, width: out.width, height: out.height * w });
    } else {
      part.inX = slice.inX + slice.inW * f;
      part.inW = slice.inW * w;
      part.corners = rectCorners({ left: out.left + out.width * f, top: out.top, width: out.width * w, height: out.height });
    }
    part.warp = [];
    parts.push(part);
  }
  const at = screen.slices.findIndex((s) => s.id === slice.id);
  return { ...screen, slices: [...screen.slices.slice(0, at), ...parts, ...screen.slices.slice(at + 1)] };
}

// Pasa un slice a otra pantalla. Una pantalla siempre conserva al menos uno.
export function moveSliceToScreen(setup: Setup, from: number, to: number, sliceId: number): Setup {
  const source = setup.screens.find((s) => s.id === from), target = setup.screens.find((s) => s.id === to);
  const slice = source?.slices.find((s) => s.id === sliceId);
  if (!source || !target || !slice || source === target || source.slices.length < 2) return setup;
  return {
    ...setup,
    screens: setup.screens.map((s) => {
      if (s.id === from) return { ...s, slices: s.slices.filter((l) => l.id !== sliceId) };
      if (s.id === to) return { ...s, slices: [...s.slices, { ...slice, id: nextId(s.slices) }] };
      return s;
    }),
  };
}

const crossesAt = (a: Pt, b: Pt, c: Pt, d: Pt) => {
  const side = (p: Pt, q: Pt, r: Pt) => (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0]);
  return side(a, b, c) * side(a, b, d) < 0 && side(c, d, a) * side(c, d, b) < 0;
};

// Mismo criterio que el motor: un polígono que se cruza no se puede triangular.
export function selfCrosses(points: Pt[]): boolean {
  const n = points.length;
  for (let i = 0; i < n; ++i) for (let k = i + 2; k < n; ++k) {
    if (i === 0 && k === n - 1) continue;
    if (crossesAt(points[i], points[(i + 1) % n], points[k], points[(k + 1) % n])) return true;
  }
  return false;
}

// Crea un slice poligonal a partir de puntos dibujados sobre la composición.
export function polygonSlice(id: number, points: Pt[]): Slice {
  const r = cornersRect(points);
  const inW = Math.max(r.width, 0.001), inH = Math.max(r.height, 0.001);
  const polygon = points.map(([x, y]) => [(x - r.left) / inW, (y - r.top) / inH] as Pt);
  return { ...newSlice(id), name: `Polígono ${id}`, inX: r.left, inY: r.top, inW, inH, polygon, corners: rectCorners({ left: r.left, top: r.top, width: inW, height: inH }) };
}

export type SliceSide = 'input' | 'output';

export const sliceRect = (s: Slice, side: SliceSide): Rect =>
  side === 'input' ? { left: s.inX, top: s.inY, width: s.inW, height: s.inH } : cornersRect(s.corners);

export const placeSlice = (s: Slice, side: SliceSide, r: Rect): Partial<Slice> =>
  side === 'input' ? { inX: r.left, inY: r.top, inW: r.width, inH: r.height } : { corners: fitCorners(s.corners, r), warp: [] };

export type SliceArea = 'left' | 'right' | 'top' | 'bottom' | 'whole';

export const AREAS: Record<SliceArea, Rect> = {
  left: { left: 0, top: 0, width: 0.5, height: 1 },
  right: { left: 0.5, top: 0, width: 0.5, height: 1 },
  top: { left: 0, top: 0, width: 1, height: 0.5 },
  bottom: { left: 0, top: 0.5, width: 1, height: 0.5 },
  whole: { left: 0, top: 0, width: 1, height: 1 },
};

// Rectángulo al escalar hacia `to` desde `from`: su esquina fija o, con `centered`, el centro.
export function scaledRect(s: Slice, to: { x: number; y: number }, from: { x: number; y: number }, centered: boolean, keepRatio: boolean): Partial<Slice> {
  const reach = centered ? 2 : 1;
  let inW = Math.max(0.005, Math.abs(to.x - from.x) * reach), inH = Math.max(0.005, Math.abs(to.y - from.y) * reach);
  if (keepRatio) {
    const ratio = s.inW / s.inH;
    if (inW / inH > ratio) inH = inW / ratio;
    else inW = inH * ratio;
  }
  const left = centered ? from.x - inW / 2 : to.x < from.x ? from.x - inW : from.x;
  const top = centered ? from.y - inH / 2 : to.y < from.y ? from.y - inH : from.y;
  return { inX: left, inY: top, inW, inH };
}

export function centerSlice(s: Slice, side: SliceSide, axis: 'x' | 'y'): Partial<Slice> {
  const r = sliceRect(s, side);
  return placeSlice(s, side, axis === 'x' ? { ...r, left: (1 - r.width) / 2 } : { ...r, top: (1 - r.height) / 2 });
}

// Lleva el slice al lado opuesto de la zona, sin voltear la imagen.
export function mirrorSlice(s: Slice, side: SliceSide, axis: 'x' | 'y'): Partial<Slice> {
  const r = sliceRect(s, side);
  return placeSlice(s, side, axis === 'x' ? { ...r, left: 1 - r.left - r.width } : { ...r, top: 1 - r.top - r.height });
}

// Círculo real en píxeles: el lado es el menor de los dos y se conserva el centro.
export function makeCircle(s: Slice, side: SliceSide, size: [number, number]): Partial<Slice> {
  const r = sliceRect(s, side);
  const px = Math.min(r.width * size[0], r.height * size[1]);
  const width = px / size[0], height = px / size[1];
  const placed = placeSlice(s, side, { left: r.left + (r.width - width) / 2, top: r.top + (r.height - height) / 2, width, height });
  return { ...placed, polygon: regularPolygon(64) };
}

export function swapShapes(s: Slice, comp: [number, number], out: [number, number]): Partial<Slice> {
  return { ...matchOutputToInput(s, comp, out), ...matchInputToOutput(s, comp, out) };
}

// El último de la lista se dibuja encima.
export function reorderSlice(screen: Screen, id: number, delta: number): Screen {
  const from = screen.slices.findIndex((s) => s.id === id);
  const to = from + delta;
  if (from < 0 || to < 0 || to >= screen.slices.length) return screen;
  const slices = [...screen.slices];
  const [moved] = slices.splice(from, 1);
  slices.splice(to, 0, moved);
  return { ...screen, slices };
}

export function pasteSlices(screen: Screen, copied: Slice[]): { screen: Screen; ids: number[] } {
  let id = nextId(screen.slices);
  const pasted = copied.map((s) => ({ ...structuredClone(s), id: id++ }));
  return { screen: { ...screen, slices: [...screen.slices, ...pasted] }, ids: pasted.map((s) => s.id) };
}

export const RESOLUTIONS: [string, number, number][] = [['HD 1280 × 720', 1280, 720], ['Full HD 1920 × 1080', 1920, 1080], ['2K 2560 × 1440', 2560, 1440], ['4K 3840 × 2160', 3840, 2160], ['Vertical 1080 × 1920', 1080, 1920], ['Cuadrado 1080 × 1080', 1080, 1080]];
