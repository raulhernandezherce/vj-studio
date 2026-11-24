import { useCallback, useEffect, useRef, useState, type PointerEvent as ReactPointerEvent } from 'react';
import { useConnection, useEngine, useParam } from '@vj/api-client';
import { scaledRect, HEART, type Mask, maskOutline, AREAS, centerSlice, makeCircle, mirrorSlice, pasteSlices, placeSlice, reorderSlice, type SliceArea, swapShapes, cornersRect, duplicateScreen, intersects, moveSliceToScreen, parseExpression, polygonSlice, RESOLUTIONS, selfCrosses, snapOffset, snapTargets, splitSlice, duplicateSlice, fitCorners, insertPoint, matchInputToOutput, matchOutputToInput, newScreen, newSlice, nextId, overlapForBlend, type Pt, type Rect, regularPolygon, removePoint, type Screen, toggleSelection, type Setup, type Slice, snap, splitHorizontally, warpGrid, clamp01, moveInput } from './model';

interface Display {
  index: number;
  name: string;
  width: number;
  height: number;
  refresh: number;
}

function parseDisplays(text: string): Display[] {
  try {
    const l = JSON.parse(text);
    return Array.isArray(l) ? l : [];
  } catch {
    return [];
  }
}

function Num(props: { label: string; value: number; min: number; max: number; step?: number; onChange: (v: number) => void }) {
  return (
    <label className="slider">
      <span>{props.label}</span>
      <input type="range" min={props.min} max={props.max} step={props.step ?? 0.01} value={props.value} onChange={(e) => props.onChange(Number(e.target.value))} />
      <output>{props.value.toFixed(2)}</output>
    </label>
  );
}

function Check(props: { label: string; on: boolean; onChange: (v: boolean) => void }) {
  return <button type="button" className={props.on ? 'toggle on' : 'toggle'} aria-pressed={props.on} onClick={() => props.onChange(!props.on)}>{props.label}</button>;
}

function Monitor() {
  const engine = useEngine();
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => engine.onMonitor((f) => {
    if (f.monitor !== 0 || !canvas.current) return;
    const c = canvas.current;
    c.width = f.width;
    c.height = f.height;
    const pixels = new Uint8ClampedArray(f.pixels.buffer as ArrayBuffer, f.pixels.byteOffset, f.pixels.length);
    c.getContext('2d')?.putImageData(new ImageData(pixels, f.width, f.height), 0, 0);
  }), [engine]);
  return <canvas ref={canvas} />;
}

// Arrastre dentro del escenario, en coordenadas 0 a 1.
function useDrag(end: () => void) {
  const svg = useRef<SVGSVGElement>(null);
  const start = useCallback((e: ReactPointerEvent, onMove: (dx: number, dy: number, x: number, y: number, ctrl: boolean, m: PointerEvent) => void, onTap?: () => void) => {
    e.preventDefault();
    e.stopPropagation();
    const box = svg.current!.getBoundingClientRect();
    const x0 = e.clientX, y0 = e.clientY;
    let moved = false;
    const move = (m: PointerEvent) => {
      moved = true;
      onMove((m.clientX - x0) / box.width, (m.clientY - y0) / box.height, clamp01((m.clientX - box.left) / box.width), clamp01((m.clientY - box.top) / box.height), m.ctrlKey, m);
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      if (moved) end();
      else onTap?.();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  }, [end]);
  return { svg, start };
}

function Guide({ path, opacity }: { path: string; opacity: number }) {
  const [url, setUrl] = useState('');
  useEffect(() => {
    let live = true;
    if (path) void window.vjHost.readImage(path).then((u) => { if (live) setUrl(u); });
    else setUrl('');
    return () => { live = false; };
  }, [path]);
  return url ? <img className="stage-guide" src={url} alt="" style={{ opacity }} /> : null;
}

// Rejilla de ayuda; en modo LED, bloques de 8 × 8 píxeles de la salida.
function Grid({ size }: { size?: [number, number] }) {
  const lines = (n: number) => Array.from({ length: Math.max(0, n - 1) }, (_, i) => (i + 1) / n);
  const xs = lines(size ? Math.min(256, Math.floor(size[0] / 8)) : 8), ys = lines(size ? Math.min(256, Math.floor(size[1] / 8)) : 8);
  return (
    <g className="stage-grid">
      {xs.map((v) => <line key={`x${v}`} x1={v} x2={v} y1={0} y2={1} />)}
      {ys.map((v) => <line key={`y${v}`} y1={v} y2={v} x1={0} x2={1} />)}
    </g>
  );
}

function Help() {
  const [text, setText] = useState('');
  useEffect(() => {
    const over = (e: MouseEvent) => {
      const el = (e.target as Element).closest?.('[title], [data-help]');
      const help = el?.getAttribute('title') ?? el?.getAttribute('data-help');
      if (help) setText(help);
    };
    document.addEventListener('mouseover', over);
    return () => document.removeEventListener('mouseover', over);
  }, []);
  return <section className="adv-help"><h3>Ayuda</h3><p>{text || 'Pasa el ratón por un control para saber qué hace.'}</p></section>;
}

// Admite cálculos como 1920/3: se aplican al pulsar Intro o al salir del campo.
function Calc({ label, value, testId, onChange }: { label: string; value: number; testId?: string; onChange: (v: number) => void }) {
  const [text, setText] = useState<string | null>(null);
  const apply = () => {
    if (text === null) return;
    const v = parseExpression(text);
    if (v !== null) onChange(v);
    setText(null);
  };
  return (
    <label>{label} <input type="text" data-rect={testId} title="Admite cálculos, por ejemplo 1920/3" value={text ?? String(Math.round(value * 100) / 100)}
      onChange={(e) => setText(e.target.value)} onBlur={apply} onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') setText(null); }} /></label>
  );
}

function PixelRect({ rect, onChange }: { rect: Rect; onChange: (r: Rect) => void }) {
  const field = (label: string, key: keyof Rect) => <Calc key={key} label={label} testId={key} value={rect[key]} onChange={(v) => onChange({ ...rect, [key]: v })} />;
  return <div className="pixel-rect">{field('Izq.', 'left')}{field('Arriba', 'top')}{field('Ancho', 'width')}{field('Alto', 'height')}</div>;
}

function Section({ title, open, children }: { title: string; open?: boolean; children: React.ReactNode }) {
  return <details className="adv-section" open={open}><summary>{title}</summary>{children}</details>;
}

interface MenuAt { x: number; y: number; id: number }

const toPath = (pts: Pt[]) => pts.map((p) => p.join(',')).join(' ');

const lead = (screen: Screen, id: number) => screen.slices.find((s) => s.id === id) ?? screen.slices[0];

function InputStage({ screen, active, selected, patchMany, commit, pick, setSelection, aspect, zoom, grid, snapOn, change, onMenu, drawing, setDrawing, finishDrawing, maskIndex, setMask, editMask, ledSize, duplicateSelected }: {
  screen: Screen; active: number; selected: number[]; patchMany: (edits: Record<number, Partial<Slice>>) => void; commit: () => void; pick: (id: number, additive: boolean) => void;
  setSelection: (ids: number[]) => void; aspect: number; zoom: number; grid: boolean; snapOn: boolean; change: (id: number, p: Partial<Slice>) => void; onMenu: (m: MenuAt) => void;
  drawing: Pt[] | null; setDrawing: (p: Pt[] | null) => void; finishDrawing: (p: Pt[]) => void;
  maskIndex: number; setMask: (i: number) => void; editMask: (i: number, points: Pt[], history: boolean) => void;
  ledSize?: [number, number]; duplicateSelected: () => void;
}) {
  const { svg, start } = useDrag(commit);
  const [lasso, setLasso] = useState<Rect | null>(null);
  const near = (v: number, ctrl: boolean) => (snapOn && !ctrl ? snap(v) : v);
  const unit = (e: { clientX: number; clientY: number }): Pt => {
    const box = svg.current!.getBoundingClientRect();
    return [clamp01((e.clientX - box.left) / box.width), clamp01((e.clientY - box.top) / box.height)];
  };

  const moveGroup = (e: ReactPointerEvent, onTap: () => void) => {
    const group = screen.slices.filter((l) => selected.includes(l.id));
    const targets = snapTargets(screen.slices, selected);
    if (e.altKey) duplicateSelected();  // la copia se queda atrás y sigue el arrastre el original
    start(e, (rawX, rawY, _x, _y, ctrl, m) => {
      // Mayús limita el movimiento al eje dominante.
      const dx = m.shiftKey && Math.abs(rawX) * aspect < Math.abs(rawY) ? 0 : rawX;
      const dy = m.shiftKey && Math.abs(rawX) * aspect >= Math.abs(rawY) ? 0 : rawY;
      const lead = group[0];
      const ox = snapOn && !ctrl && dx ? snapOffset(lead.inX + dx, lead.inX + lead.inW + dx, targets.x) : 0;
      const oy = snapOn && !ctrl && dy ? snapOffset(lead.inY + dy, lead.inY + lead.inH + dy, targets.y) : 0;
      patchMany(Object.fromEntries(group.map((l) => [l.id, moveInput(l, dx + ox, dy + oy)])));
    }, onTap);
  };

  // Escala desde la esquina contraria; Alt, desde el centro; Mayús conserva la proporción.
  const scaleFrom = (e: ReactPointerEvent, s: Slice, cornerX: 0 | 1, cornerY: 0 | 1) => {
    const fixedX = cornerX ? s.inX : s.inX + s.inW, fixedY = cornerY ? s.inY : s.inY + s.inH;
    const cx = s.inX + s.inW / 2, cy = s.inY + s.inH / 2;
    start(e, (_, __, x, y, ctrl, m) => {
      const px = near(x, ctrl), py = near(y, ctrl);
      const r = scaledRect(s, { x: px, y: py }, { x: m.altKey ? cx : fixedX, y: m.altKey ? cy : fixedY }, m.altKey, m.shiftKey);
      patchMany({ [s.id]: r });
    });
  };

  const rotate = (e: ReactPointerEvent, s: Slice) => {
    const cx = s.inX + s.inW / 2, cy = s.inY + s.inH / 2;
    start(e, (_, __, x, y, ___, m) => {
      const angle = (Math.atan2((y - cy) / aspect, x - cx) * 180) / Math.PI + 90;
      const degrees = Math.round(((angle + 540) % 360) - 180);
      patchMany({ [s.id]: { inRotation: m.shiftKey ? Math.round(degrees / 45) * 45 : degrees } });
    });
  };

  const beginEmpty = (e: ReactPointerEvent) => {
    if (e.button !== 0) return;
    const from = unit(e);
    if (drawing) {
      if (drawing.length >= 3 && Math.hypot(from[0] - drawing[0][0], from[1] - drawing[0][1]) < 0.015) finishDrawing(drawing);
      // El segundo clic de un doble clic cae en el mismo sitio: no es un punto nuevo.
      else if (!drawing.length || Math.hypot(from[0] - drawing[drawing.length - 1][0], from[1] - drawing[drawing.length - 1][1]) > 0.004) setDrawing([...drawing, from]);
      return;
    }
    const box = svg.current!.getBoundingClientRect();
    const rectAt = (m: PointerEvent): Rect => {
      const x = clamp01((m.clientX - box.left) / box.width), y = clamp01((m.clientY - box.top) / box.height);
      return { left: Math.min(from[0], x), top: Math.min(from[1], y), width: Math.abs(x - from[0]), height: Math.abs(y - from[1]) };
    };
    const move = (m: PointerEvent) => setLasso(rectAt(m));
    const up = (m: PointerEvent) => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      setLasso(null);
      const r = rectAt(m);
      if (r.width < 0.003 && r.height < 0.003) return;
      const hit = screen.slices.filter((l) => intersects(l, r)).map((l) => l.id);
      if (hit.length) setSelection(hit);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  return (
    <div className="stage-inner" style={{ aspectRatio: String(aspect), width: `${zoom}%` }}>
      <Monitor />
      <Guide path={screen.inputGuide} opacity={screen.guideOpacity} />
      <svg ref={svg} viewBox="0 0 1 1" preserveAspectRatio="none" data-testid="input-stage" onPointerDown={beginEmpty}
        onDoubleClick={() => { if (drawing && drawing.length >= 3) finishDrawing(drawing); }}>
        {screen.slices.map((s) => {
          const on = selected.includes(s.id), lead = s.id === active;
          const points: Pt[] = s.polygon.length >= 3
            ? s.polygon.map(([x, y]) => [s.inX + x * s.inW, s.inY + y * s.inH])
            : [[s.inX, s.inY], [s.inX + s.inW, s.inY], [s.inX + s.inW, s.inY + s.inH], [s.inX, s.inY + s.inH]];
          return (
            <g key={s.id}>
              <polygon className={on ? 'slice-shape' : 'slice-shape other'} points={toPath(points)} data-slice={s.id}
                data-help="Arrastra para mover; Ctrl desactiva el imán. Mayús + clic añade a la selección. En un polígono, doble clic añade un punto."
                onContextMenu={(e) => { e.preventDefault(); if (!on) pick(s.id, false); onMenu({ x: e.clientX, y: e.clientY, id: s.id }); }}
                onDoubleClick={(e) => {
                  if (s.polygon.length < 3) return;
                  e.stopPropagation();
                  const [x, y] = unit(e);
                  change(s.id, { polygon: insertPoint(s.polygon, [(x - s.inX) / s.inW, (y - s.inY) / s.inH]) });
                }}
                onPointerDown={(e) => {
                  if (e.button !== 0 || drawing) return;
                  e.stopPropagation();
                  if (e.shiftKey && !on) { pick(s.id, true); return; }
                  if (e.shiftKey) { moveGroup(e, () => pick(s.id, true)); return; }
                  if (!on) { pick(s.id, false); return; }
                  moveGroup(e, () => pick(s.id, false));  // clic sin arrastrar: se queda solo este
                }} />
              {lead && s.polygon.length >= 3 && s.polygon.map((q, i) => (
                <circle key={i} className="handle" data-point={i} cx={s.inX + q[0] * s.inW} cy={s.inY + q[1] * s.inH} r={0.008} data-help="Arrastra el punto. Doble clic o clic derecho lo quita."
                  onContextMenu={(e) => { e.preventDefault(); e.stopPropagation(); change(s.id, { polygon: removePoint(s.polygon, i) }); }}
                  onDoubleClick={(e) => { e.stopPropagation(); change(s.id, { polygon: removePoint(s.polygon, i) }); }}
                  onPointerDown={(e) => { if (e.button === 0) start(e, (_, __, x, y, ctrl) => patchMany({ [s.id]: { polygon: s.polygon.map((p, k) => (k === i ? [clamp01((near(x, ctrl) - s.inX) / s.inW), clamp01((near(y, ctrl) - s.inY) / s.inH)] : p)) as Pt[] } })); }} />
              ))}
              {lead && s.polygon.length < 3 && (
                <>
                  {([[0, 0], [1, 0], [1, 1], [0, 1]] as const).map(([cx, cy]) => (
                    <rect key={`${cx}${cy}`} className="handle" x={s.inX + cx * s.inW - 0.007} y={s.inY + cy * s.inH - 0.011} width={0.014} height={0.022} data-handle="resize" data-help="Arrastra para escalar desde la esquina opuesta."
                      onPointerDown={(e) => { if (e.button === 0) scaleFrom(e, s, cx, cy); }} />
                  ))}
                  <circle className="handle rotate" cx={s.inX + s.inW / 2} cy={Math.max(0.02, s.inY - 0.03)} r={0.008} data-handle="rotate" data-help="Arrastra para rotar el slice, por ejemplo en pantallas LED giradas."
                    onPointerDown={(e) => { if (e.button === 0) rotate(e, s); }} />
                </>
              )}
            </g>
          );
        })}
        {lead(screen, active).masks.map((m, i) => {
          const s = lead(screen, active);
          const toStage = ([x, y]: Pt): Pt => [s.inX + x * s.inW, s.inY + y * s.inH];
          const fromStage = ([x, y]: Pt): Pt => [clamp01((x - s.inX) / s.inW), clamp01((y - s.inY) / s.inH)];
          return (
            <g key={i}>
              <polygon className={i === maskIndex ? 'mask-shape on' : 'mask-shape'} points={toPath(maskOutline(m).map(toStage))} data-slice-mask={i} data-help="Arrastra para mover la máscara. Doble clic añade un punto."
                onDoubleClick={(e) => { e.stopPropagation(); editMask(i, insertPoint(m.points, fromStage(unit(e))), true); }}
                onPointerDown={(e) => {
                  if (e.button !== 0 || drawing) return;
                  e.stopPropagation();
                  setMask(i);
                  const from = m.points;
                  start(e, (dx, dy) => editMask(i, from.map(([x, y]) => [x + dx / s.inW, y + dy / s.inH] as Pt), false));
                }} />
              {i === maskIndex && m.points.map((q, k) => {
                const at = toStage(q);
                return (
                  <circle key={k} className="handle mask" cx={at[0]} cy={at[1]} r={0.007} data-mask-point={k} data-help="Arrastra el punto. Doble clic lo quita."
                    onDoubleClick={(e) => { e.stopPropagation(); editMask(i, removePoint(m.points, k), true); }}
                    onPointerDown={(e) => { e.stopPropagation(); start(e, (_, __, x, y, ctrl) => editMask(i, m.points.map((p, n) => (n === k ? fromStage([near(x, ctrl), near(y, ctrl)]) : p)), false)); }} />
                );
              })}
            </g>
          );
        })}
        {drawing && <polyline className="slice-draw" points={toPath(drawing)} />}
        {drawing && drawing.map((q, i) => <circle key={i} className="handle" cx={q[0]} cy={q[1]} r={0.006} />)}
        {lasso && <rect className="stage-lasso" x={lasso.left} y={lasso.top} width={lasso.width} height={lasso.height} />}
        {grid && <Grid size={ledSize} />}
      </svg>
    </div>
  );
}

function OutputStage({ screen, active, patch, commit, select, zoom, grid, led, magnet, maskIndex, setMask, editMask, onMenu }: { screen: Screen; active: number; patch: (id: number, p: Partial<Slice>) => void; commit: () => void; select: (id: number) => void; zoom: number; grid: boolean; led: boolean; magnet: (v: number) => number; maskIndex: number; setMask: (i: number) => void; editMask: (i: number, points: Pt[], history: boolean) => void; onMenu: (m: MenuAt) => void }) {
  const { svg, start } = useDrag(commit);
  const bilerp = (c: Pt[], u: number, v: number): Pt => {
    const a: Pt = [c[0][0] + (c[1][0] - c[0][0]) * u, c[0][1] + (c[1][1] - c[0][1]) * u];
    const b: Pt = [c[3][0] + (c[2][0] - c[3][0]) * u, c[3][1] + (c[2][1] - c[3][1]) * u];
    return [a[0] + (b[0] - a[0]) * v, a[1] + (b[1] - a[1]) * v];
  };
  return (
    <div className="stage-inner" style={{ aspectRatio: String(screen.width / screen.height), width: `${zoom}%` }}>
      <Guide path={screen.outputGuide} opacity={screen.guideOpacity} />
      <svg ref={svg} viewBox="0 0 1 1" preserveAspectRatio="none" data-testid="output-stage">
        {screen.slices.map((s) => {
          const on = s.id === active;
          return (
            <g key={s.id}>
              <polygon className={on ? 'slice-shape' : 'slice-shape other'} points={toPath(s.corners)} data-help="Arrastra para colocarlo en la pantalla. Clic derecho: más opciones."
                onContextMenu={(e) => { e.preventDefault(); select(s.id); onMenu({ x: e.clientX, y: e.clientY, id: s.id }); }}
                onPointerDown={(e) => {
                  if (e.button !== 0) return;
                  select(s.id);
                  const from = s.corners;
                  start(e, (dx, dy) => patch(s.id, { corners: from.map(([x, y]) => [x + dx, y + dy] as Pt) }));
                }} />
              {on && s.corners.map((c, i) => (
                <rect key={i} className="handle" x={c[0] - 0.008} y={c[1] - 0.012} width={0.016} height={0.024}
                  onPointerDown={(e) => start(e, (_, __, x, y) => patch(s.id, { corners: s.corners.map((q, k) => (k === i ? [magnet(x), magnet(y)] : q)) as Pt[] }))} />
              ))}
              {on && s.warp.map((p, i) => {
                const at = bilerp(s.corners, p[0], p[1]);
                return (
                  <circle key={i} className="handle" cx={at[0]} cy={at[1]} r={0.007}
                    onPointerDown={(e) => {
                      const from = s.warp;
                      const w = Math.hypot(s.corners[1][0] - s.corners[0][0], s.corners[1][1] - s.corners[0][1]) || 1;
                      const h = Math.hypot(s.corners[3][0] - s.corners[0][0], s.corners[3][1] - s.corners[0][1]) || 1;
                      start(e, (dx, dy) => patch(s.id, { warp: from.map((q, k) => (k === i ? [q[0] + dx / w, q[1] + dy / h] : q)) as Pt[] }));
                    }} />
                );
              })}
            </g>
          );
        })}
        {screen.masks.map((m, i) => (
          <g key={i}>
            <polygon className={i === maskIndex ? 'mask-shape on' : 'mask-shape'} points={toPath(maskOutline(m))} data-mask={i} data-help="Arrastra para mover. Doble clic añade un punto."
              onDoubleClick={(e) => {
                const box = svg.current!.getBoundingClientRect();
                editMask(i, insertPoint(m.points, [clamp01((e.clientX - box.left) / box.width), clamp01((e.clientY - box.top) / box.height)]), true);
              }}
              onPointerDown={(e) => {
                setMask(i);
                const from = m.points;
                start(e, (dx, dy) => editMask(i, from.map(([x, y]) => [x + dx, y + dy] as Pt), false));
              }} />
            {i === maskIndex && m.points.map((q, k) => (
              <circle key={k} className="handle mask" cx={q[0]} cy={q[1]} r={0.007} data-help="Arrastra el punto. Doble clic lo quita."
                onDoubleClick={(e) => { e.stopPropagation(); editMask(i, removePoint(m.points, k), true); }}
                onPointerDown={(e) => start(e, (_, __, x, y, ctrl) => editMask(i, m.points.map((p, n) => (n === k ? [ctrl ? x : magnet(x), ctrl ? y : magnet(y)] : p)) as Pt[], false))} />
            ))}
          </g>
        ))}
        {grid && <Grid size={led ? [screen.width, screen.height] : undefined} />}
      </svg>
    </div>
  );
}

export function AdvancedOutput() {
  const engine = useEngine();
  const connection = useConnection();
  const [setup, setSetup] = useState<Setup | null>(null);
  const [screenId, setScreenId] = useState(0);
  const [sliceId, setSliceId] = useState(0);
  const [extra, setExtra] = useState<number[]>([]);
  const [hand, setHand] = useState(false);
  const [menu, setMenu] = useState<MenuAt | null>(null);
  const [drawing, setDrawing] = useState<Pt[] | null>(null);
  const [maskIndex, setMaskIndex] = useState(0);
  const [sliceMask, setSliceMask] = useState(0);
  const [drawingMask, setDrawingMask] = useState(false);
  const [tab, setTab] = useState<'input' | 'output'>('input');
  const [presets, setPresets] = useState<string[]>([]);
  const [message, setMessage] = useState('');
  const [name, setName] = useState('');
  const [zoom, setZoom] = useState(100);
  const [grid, setGrid] = useState(false);
  const [led, setLed] = useState(false);
  const held = useRef(false);
  const [magnetOn, setMagnetOn] = useState(true);
  const [folded, setFolded] = useState<number[]>([]);
  const [copied, setCopied] = useState<Slice[]>([]);
  const magnet = (v: number) => (magnetOn ? snap(v) : v);
  const [displaysText] = useParam('/output/displays', '[]');
  const [testCard, setTestCard] = useParam('/output/testcard', false);
  const [identify, setIdentify] = useParam('/output/identify', false);
  const [disabled, setDisabled] = useParam('/output/disabled', false);
  const [active] = useParam('/output/setup', '');
  const [layers] = useParam('/composition/structure/layers', 0);
  const [groups] = useParam('/composition/structure/groups', 0);
  const [compW] = useParam('/composition/width', 1920);
  const [compH] = useParam('/composition/height', 1080);
  const displays = parseDisplays(displaysText);
  const fail = (e: Error) => setMessage(e.message);

  const edits = useRef(0);
  // Último escenario enviado: al soltar un arrastre se confirma este, no el de cuando empezó.
  const latest = useRef<Setup | null>(null);
  const load = useCallback(async () => {
    const before = edits.current;
    const s = await engine.run<Setup>('screens.get');
    // Una lectura que llega tarde no pisa lo que el usuario acaba de cambiar.
    if (edits.current === before) {
      latest.current = s;
      setSetup(s);
      setScreenId((id) => (s.screens.some((x) => x.id === id) ? id : s.screens[0].id));
    }
    setPresets(await engine.run<string[]>('screens.preset.list'));
  }, [engine]);
  useEffect(() => { if (connection === 'connected') void load().catch(fail); }, [connection, load]);

  // Durante un arrastre se envía sin historial; al soltar queda un solo paso de deshacer.
  const push = useCallback((next: Setup, history: boolean) => {
    edits.current += 1;
    latest.current = next;
    setSetup(next);
    void engine.run('screens.set', { setup: next, history }).catch((e: Error) => {
      // El motor rechazó el cambio: se vuelve a lo que de verdad hay.
      fail(e);
      void load().catch(fail);
    });
  }, [engine, load]);

  useEffect(() => window.vjHost.onMenu((id) => {
    if (id === 'undo') void engine.run('screens.undo').then(load, fail);
    if (id === 'redo') void engine.run('screens.redo').then(load, fail);
  }), [engine, load]);

  const keyRef = useRef<(e: KeyboardEvent) => void>(() => {});
  useEffect(() => {
    const down = (e: KeyboardEvent) => keyRef.current(e);
    // Espacio mueve el lienzo solo mientras se mantiene pulsado.
    const up = (e: KeyboardEvent) => { if (e.key === ' ' && held.current) { held.current = false; setHand(false); } };
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
    };
  }, []);

  if (!setup) return <p className="hint" style={{ padding: 16 }}>Conectando con el motor…</p>;
  const screen = setup.screens.find((s) => s.id === screenId) ?? setup.screens[0];
  const slice = screen.slices.find((s) => s.id === sliceId) ?? screen.slices[0];

  const change = (fn: (s: Screen) => Screen, history = true) => {
    const now = latest.current ?? setup;
    push({ ...now, screens: now.screens.map((s) => (s.id === screen.id ? fn(s) : s)) }, history);
  };
  const patchSlice = (id: number, p: Partial<Slice>, history = false) =>
    change((s) => ({ ...s, slices: s.slices.map((l) => (l.id === id ? { ...l, ...p } : l)) }), history);
  const commit = () => void engine.run('screens.set', { setup: latest.current, history: true }).catch(fail);
  const patchActive = (p: Partial<Slice>) => patchSlice(slice.id, p, true);
  // Selección múltiple: el activo y los añadidos con Cmd o Ctrl.
  const selected = [slice.id, ...extra.filter((id) => id !== slice.id && screen.slices.some((s) => s.id === id))];
  const patchSelected = (p: Partial<Slice>) => change((s) => ({ ...s, slices: s.slices.map((l) => (selected.includes(l.id) ? { ...l, ...p } : l)) }));
  const clickSlice = (id: number, additive: boolean) => {
    const next = toggleSelection(selected, id, additive);
    setSliceId(next[next.length - 1]);
    setExtra(next);
  };
  const duplicateSelected = () => {
    const r = pasteSlices(screen, screen.slices.filter((l) => selected.includes(l.id)));
    change(() => r.screen);
  };
  const copySelected = () => setCopied(screen.slices.filter((l) => selected.includes(l.id)).map((l) => structuredClone(l)));
  const removeSelected = () => change((s) => ({ ...s, slices: s.slices.filter((l) => !selected.includes(l.id)) }));
  const paste = () => {
    if (!copied.length) return;
    const r = pasteSlices(screen, copied);
    change(() => r.screen);
    setSliceId(r.ids[r.ids.length - 1]);
    setExtra(r.ids);
  };
  const patchMany = (edits: Record<number, Partial<Slice>>) => change((s) => ({ ...s, slices: s.slices.map((l) => (edits[l.id] ? { ...l, ...edits[l.id] } : l)) }), false);
  const editSliceMask = (i: number, patch: Partial<Mask>, history: boolean) =>
    patchSlice(slice.id, { masks: slice.masks.map((m, k) => (k === i ? { ...m, ...patch } : m)) }, history);
  const addSliceMask = (points: Pt[], smooth = false) => {
    patchSlice(slice.id, { masks: [...slice.masks, { points, invert: false, smooth }] }, true);
    setSliceMask(slice.masks.length);
    setTab('input');
  };
  const finishDrawing = (points: Pt[]) => {
    setDrawing(null);
    if (selfCrosses(points)) { setMessage('Ese polígono se cruza consigo mismo: dibújalo otra vez.'); return; }
    if (drawingMask) {
      setDrawingMask(false);
      addSliceMask(points.map(([x, y]) => [clamp01((x - slice.inX) / slice.inW), clamp01((y - slice.inY) / slice.inH)]));
      return;
    }
    const id = nextId(screen.slices);
    change((s) => ({ ...s, slices: [...s.slices, polygonSlice(id, points)] }));
    setSliceId(id);
    setExtra([id]);
  };
  const editMask = (i: number, points: Pt[], history: boolean) => change((s) => ({ ...s, masks: s.masks.map((m, k) => (k === i ? { ...m, points } : m)) }), history);
  const addMask = (points: Pt[]) => {
    change((s) => ({ ...s, masks: [...s.masks, { points, invert: true, smooth: false }] }));
    setMaskIndex(screen.masks.length);
    setTab('output');
  };
  // Flechas: 1 píxel; con Mayús, 10.
  keyRef.current = (e) => {
    const steps: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
    const step = steps[e.key];
    const el = e.target as HTMLElement;
    if (el.tagName === 'INPUT' || el.tagName === 'SELECT' || el.tagName === 'TEXTAREA') return;
    if ((e.metaKey || e.ctrlKey) && !window.getSelection()?.toString()) {
      const key = e.key.toLowerCase();
      if (key === 'c') copySelected();
      if (key === 'x' && screen.slices.length > selected.length) { copySelected(); removeSelected(); }
      if (key === 'v') paste();
      if (key === 'd') { e.preventDefault(); duplicateSelected(); }
      if (key === '+' || key === '=') { e.preventDefault(); setZoom((z) => Math.min(800, Math.round(z * 1.25))); }
      if (key === '-') { e.preventDefault(); setZoom((z) => Math.max(25, Math.round(z / 1.25))); }
      return;
    }
    if (e.key === ' ' && !held.current) { e.preventDefault(); held.current = true; setHand(true); return; }
    if ((e.key === 'Backspace' || e.key === 'Delete') && screen.slices.length > selected.length) { removeSelected(); return; }
    if (!step) return;
    e.preventDefault();
    const amount = e.shiftKey ? 10 : 1;
    const group = screen.slices.filter((l) => selected.includes(l.id));
    if (tab === 'input') {
      change((s) => ({ ...s, slices: s.slices.map((l) => (selected.includes(l.id) ? { ...l, ...moveInput(l, (step[0] * amount) / compW, (step[1] * amount) / compH) } : l)) }));
    } else if (group.length) {
      const dx = (step[0] * amount) / screen.width, dy = (step[1] * amount) / screen.height;
      change((s) => ({ ...s, slices: s.slices.map((l) => (selected.includes(l.id) ? { ...l, corners: l.corners.map(([x, y]) => [x + dx, y + dy] as Pt) } : l)) }));
    }
  };
  const menuSlice = menu ? screen.slices.find((l) => l.id === menu.id) : undefined;
  const closeMenu = (fn: () => void) => () => { setMenu(null); fn(); };
  const panStage = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (!hand) return;
    e.preventDefault();
    e.stopPropagation();
    const el = e.currentTarget, x0 = e.clientX, y0 = e.clientY, left = el.scrollLeft, top = el.scrollTop;
    const move = (m: PointerEvent) => { el.scrollLeft = left - (m.clientX - x0); el.scrollTop = top - (m.clientY - y0); };
    const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };

  const used = new Set(setup.screens.map((s) => s.device));
  const comp: [number, number] = [compW, compH];
  const out: [number, number] = [screen.width, screen.height];
  const outRect = cornersRect(slice.corners);
  const scaleRect = (r: Rect, sx: number, sy: number): Rect => ({ left: r.left * sx, top: r.top * sy, width: r.width * sx, height: r.height * sy });
  const pickGuide = async (key: 'inputGuide' | 'outputGuide') => {
    const path = await window.vjHost.pickImage();
    if (path) change((s) => ({ ...s, [key]: path }));
  };
  const guideKey = tab === 'input' ? 'inputGuide' : 'outputGuide';
  return (
    <div className="adv" data-testid="advanced-output">
      <header className="adv-top">
        <b>Advanced Output</b>
        <label>Escenario&nbsp;
          <select data-testid="scenario-select" value={presets.includes(active) ? active : ''} onChange={(e) => void engine.run('screens.preset.load', { name: e.target.value }).then(load, fail)}>
            <option value="" disabled>Sin guardar</option>
            {presets.map((p) => <option key={p}>{p}</option>)}
          </select>
        </label>
        <input type="text" data-testid="scenario-name" placeholder="Nombre del escenario" value={name} onChange={(e) => setName(e.target.value)} />
        <button type="button" data-testid="scenario-save" disabled={!name} onClick={() => void engine.run('screens.preset.save', { name }).then(() => { setName(''); return load(); }, fail)}>Guardar</button>
        <button type="button" disabled={!presets.includes(active)} onClick={async () => {
          if (await window.vjHost.confirm(`¿Borrar el escenario ${active}?`, 'Se elimina el archivo. La configuración actual sigue activa.')) void engine.run('screens.preset.delete', { name: active }).then(load, fail);
        }}>Borrar</button>
        <span className="grow" />
        <Check label="Carta de prueba" on={testCard} onChange={setTestCard} />
        <Check label="Identificar" on={identify} onChange={setIdentify} />
        <Check label="Salida apagada" on={disabled} onChange={setDisabled} />
      </header>

      <aside className="adv-col">
        <h3>Pantallas</h3>
        {setup.screens.map((s) => (
          <div key={s.id}>
            <button type="button" className={s.id === screen.id ? 'adv-item on' : 'adv-item'} data-screen={s.id} onClick={() => setScreenId(s.id)}
              onDragOver={(e) => e.preventDefault()}
              onDrop={(e) => {
                const raw = e.dataTransfer.getData('application/x-vj-slice');
                if (!raw) return;
                const from = JSON.parse(raw) as { screen: number; slice: number };
                const next = moveSliceToScreen(setup, from.screen, s.id, from.slice);
                if (next === setup) setMessage('Una pantalla debe conservar al menos un slice.');
                else push(next, true);
              }}>
              <span className="adv-fold" title="Plegar slices; la salida sigue activa" onClick={(e) => { e.stopPropagation(); setFolded(folded.includes(s.id) ? folded.filter((f) => f !== s.id) : [...folded, s.id]); }}>{folded.includes(s.id) ? '▸' : '▾'}</span>
              <span className={s.enabled ? 'adv-enable on' : 'adv-enable'} data-enable={s.id} title="Activar o desactivar esta pantalla" onClick={(e) => {
                e.stopPropagation();
                push({ ...setup, screens: setup.screens.map((x) => (x.id === s.id ? { ...x, enabled: !x.enabled } : x)) }, true);
              }} />
              {s.name} <span className="hint">{s.device === 'virtual' ? 'virtual' : s.device === 'window' ? 'ventana' : displays[Number(s.device.split(':')[1])]?.name ?? 'desconectada'}</span>
            </button>
            {s.id === screen.id && !folded.includes(s.id) && s.slices.map((l) => (
              <button key={l.id} type="button" className={selected.includes(l.id) ? 'adv-item sub on' : 'adv-item sub'} data-slice-item={l.id} title="Mayús, Cmd o Ctrl + clic para seleccionar varios. Arrastra a otra pantalla para moverlo."
                draggable onDragStart={(e) => e.dataTransfer.setData('application/x-vj-slice', JSON.stringify({ screen: screen.id, slice: l.id }))}
                onContextMenu={(e) => { e.preventDefault(); if (!selected.includes(l.id)) clickSlice(l.id, false); setMenu({ x: e.clientX, y: e.clientY, id: l.id }); }}
                onClick={(e) => clickSlice(l.id, e.metaKey || e.ctrlKey || e.shiftKey)}>{l.name}</button>
            ))}
          </div>
        ))}
        <div className="row">
          <button type="button" data-testid="add-screen" onClick={() => {
            const id = nextId(setup.screens);
            push({ ...setup, screens: [...setup.screens, newScreen(id, 'virtual')] }, true);
            setScreenId(id);
          }}>＋ Pantalla</button>
          <button type="button" onClick={() => {
            const id = nextId(screen.slices);
            change((s) => ({ ...s, slices: [...s.slices, newSlice(id)] }));
            setSliceId(id);
          }}>＋ Slice</button>
          <button type="button" data-testid="draw-polygon" className={drawing ? 'toggle on' : 'toggle'} title="Clic para poner cada punto; cierra en el primero o con doble clic" onClick={() => { setTab('input'); setDrawing(drawing ? null : []); }}>Dibujar polígono</button>
        </div>
        <div className="row">
          <button type="button" data-testid="duplicate-screen" title="Copia la pantalla como virtual" onClick={() => {
            const r = duplicateScreen(setup, screen);
            push(r.setup, true);
            setScreenId(r.id);
          }}>Duplicar pantalla</button>
          <button type="button" data-testid="duplicate-slice" title="Copia el slice seleccionado" onClick={() => {
            const r = duplicateSlice(screen, slice);
            change(() => r.screen);
            setSliceId(r.id);
          }}>Duplicar slice</button>
        </div>
        <div className="row">
          <button type="button" disabled={setup.screens.length < 2} onClick={() => push({ ...setup, screens: setup.screens.filter((s) => s.id !== screen.id) }, true)}>Quitar pantalla</button>
          <button type="button" disabled={screen.slices.length <= selected.length} onClick={() => { change((s) => ({ ...s, slices: s.slices.filter((l) => !selected.includes(l.id)) })); setExtra([]); }}>Quitar slice{selected.length > 1 ? `s (${selected.length})` : ''}</button>
        </div>
        <h3>Reparto rápido</h3>
        <div className="row">
          <button type="button" data-testid="split" onClick={() => push(splitHorizontally(setup), true)}>Partir composición</button>
          <button type="button" onClick={() => push(overlapForBlend(setup, 0.15), true)}>Solape 15 %</button>
        </div>
        <h3>Pantallas conectadas</h3>
        {displays.map((d) => <p key={d.index} className="hint">{d.index + 1}. {d.name}: {d.width}×{d.height} a {d.refresh} Hz</p>)}
        {message && <p className="warn" onClick={() => setMessage('')}>{message}</p>}
        <span className="grow" />
        <Help />
      </aside>

      <main className="adv-center">
        <nav className="tabs">
          <button type="button" className={tab === 'input' ? 'on' : ''} onClick={() => setTab('input')}>Input Selection</button>
          <button type="button" className={tab === 'output' ? 'on' : ''} data-testid="tab-output" onClick={() => setTab('output')}>Output Transformation</button>
        </nav>
        <div className="adv-tools">
          <select aria-label="Zoom" title="Zoom del lienzo; no afecta a la salida" data-testid="stage-zoom" value={zoom} onChange={(e) => setZoom(Number(e.target.value))}>
            {[...new Set([50, 75, 100, 150, 200, 300, zoom])].sort((a, b) => a - b).map((z) => <option key={z} value={z}>{z} %</option>)}
          </select>
          <button type="button" title="Deshacer" onClick={() => void engine.run('screens.undo').then(load, fail)}>↶</button>
          <button type="button" title="Rehacer" onClick={() => void engine.run('screens.redo').then(load, fail)}>↷</button>
          <button type="button" data-testid="stage-hand" title="Mover el lienzo arrastrando; no afecta a la salida" className={hand ? 'toggle on' : 'toggle'} onClick={() => setHand(!hand)}>Mover</button>
          <button type="button" data-testid="stage-snap" title="Imán a bordes y centro" className={magnetOn ? 'toggle on' : 'toggle'} onClick={() => setMagnetOn(!magnetOn)}>Imán</button>
          <button type="button" data-testid="stage-grid" title="Rejilla de ayuda de 8 × 8" className={grid ? 'toggle on' : 'toggle'} onClick={() => setGrid(!grid)}>Rejilla</button>
          <button type="button" data-testid="stage-led" title="La rejilla marca bloques de 8 × 8 píxeles, para pantallas LED" className={led ? 'toggle on' : 'toggle'} onClick={() => { setLed(!led); setGrid(true); }}>LED 8×8</button>
          <span className="grow" />
          <span className="hint">{tab === 'input' ? `Composición ${compW} × ${compH}` : `${screen.name} ${screen.width} × ${screen.height}`}</span>
        </div>
        <div className={hand ? 'stage panning' : 'stage'} onWheel={(e) => setZoom((z) => Math.min(800, Math.max(25, Math.round(z * (e.deltaY < 0 ? 1.1 : 1 / 1.1)))))} onPointerDownCapture={panStage} onPointerDown={(e) => { if (e.target === e.currentTarget) setExtra([slice.id]); }}>
          {tab === 'input'
            ? <InputStage screen={screen} active={slice.id} selected={selected} patchMany={patchMany} commit={commit} pick={clickSlice} setSelection={(ids) => { setSliceId(ids[0]); setExtra(ids); }} aspect={compW / compH} zoom={zoom} grid={grid} snapOn={magnetOn} change={(id, p) => patchSlice(id, p, true)} onMenu={setMenu} drawing={drawing} setDrawing={setDrawing} finishDrawing={finishDrawing} maskIndex={sliceMask} setMask={setSliceMask} editMask={(i, points, history) => editSliceMask(i, { points }, history)} ledSize={led ? comp : undefined} duplicateSelected={duplicateSelected} />
            : <OutputStage screen={screen} active={slice.id} patch={(id, p) => patchSlice(id, p)} commit={commit} select={setSliceId} zoom={zoom} grid={grid} led={led} magnet={magnet} maskIndex={maskIndex} setMask={setMaskIndex} editMask={editMask} onMenu={setMenu} />}
        </div>
      </main>

      <aside className="adv-col">
        <Section title="Pantalla" open>
        <label className="field-enum"><span>Nombre</span><input type="text" value={screen.name} onChange={(e) => change((s) => ({ ...s, name: e.target.value }), false)} onBlur={commit} /></label>
        <label className="field-enum">
          <span>Salida</span>
          <select data-testid="screen-device" value={screen.device} onChange={(e) => change((s) => ({ ...s, device: e.target.value }))}>
            <option value="virtual">Virtual</option>
            <option value="window" disabled={used.has('window') && screen.device !== 'window'}>Ventana</option>
            {displays.map((d) => <option key={d.index} value={`display:${d.index}`} disabled={used.has(`display:${d.index}`) && screen.device !== `display:${d.index}`}>{d.name}: pantalla completa</option>)}
          </select>
        </label>
        <label className="field-enum">
          <span>Tamaño</span>
          <select data-testid="screen-size" value={`${screen.width}x${screen.height}`} onChange={(e) => {
            const [width, height] = e.target.value.split('x').map(Number);
            if (width && height) change((s) => ({ ...s, width, height }));
          }}>
            <option value={`${screen.width}x${screen.height}`}>{screen.width} × {screen.height}</option>
            {RESOLUTIONS.map(([n, w, h]) => <option key={n} value={`${w}x${h}`}>{n}</option>)}
            <option value={`${compW}x${compH}`}>Igual que la composición</option>
            {displays.map((d) => <option key={d.index} value={`${d.width}x${d.height}`}>{d.name}</option>)}
          </select>
        </label>
        <div className="row">
          <label>Ancho <input type="number" min={16} max={16384} value={screen.width} onChange={(e) => change((s) => ({ ...s, width: Number(e.target.value) }), false)} onBlur={commit} /></label>
          <label>Alto <input type="number" min={16} max={16384} value={screen.height} onChange={(e) => change((s) => ({ ...s, height: Number(e.target.value) }), false)} onBlur={commit} /></label>
        </div>
        <Num label="Opacidad" value={screen.opacity} min={0} max={1} onChange={(v) => change((s) => ({ ...s, opacity: v }))} />
        <Num label="Brillo" value={screen.brightness} min={0} max={2} onChange={(v) => change((s) => ({ ...s, brightness: v }))} />
        <Num label="Contraste" value={screen.contrast} min={0} max={2} onChange={(v) => change((s) => ({ ...s, contrast: v }))} />
        <Num label="Rojo" value={screen.red} min={0} max={1} onChange={(v) => change((s) => ({ ...s, red: v }))} />
        <Num label="Verde" value={screen.green} min={0} max={1} onChange={(v) => change((s) => ({ ...s, green: v }))} />
        <Num label="Azul" value={screen.blue} min={0} max={1} onChange={(v) => change((s) => ({ ...s, blue: v }))} />
        <Num label="Retardo ms" value={screen.delay} min={0} max={100} step={1} onChange={(v) => change((s) => ({ ...s, delay: v }))} />

        </Section>
        <Section title={`Guía de ${tab === 'input' ? 'entrada' : 'salida'}`}>
        <div className="row">
          <button type="button" data-testid="guide-load" title="Plantilla de imagen que solo se ve aquí, nunca en la salida" onClick={() => void pickGuide(guideKey)}>Cargar imagen</button>
          <button type="button" disabled={!screen[guideKey]} onClick={() => change((s) => ({ ...s, [guideKey]: '' }))}>Quitar</button>
        </div>
        <Num label="Opacidad guía" value={screen.guideOpacity} min={0} max={1} onChange={(v) => change((s) => ({ ...s, guideOpacity: v }))} />

        </Section>
        <Section title={selected.length > 1 ? `Slices (${selected.length})` : 'Slice'} open>
        <label className="field-enum"><span>Nombre</span><input type="text" data-testid="slice-name" value={slice.name} onChange={(e) => patchSlice(slice.id, { name: e.target.value })} onBlur={commit} /></label>
        {tab === 'input'
          ? <PixelRect rect={scaleRect({ left: slice.inX, top: slice.inY, width: slice.inW, height: slice.inH }, compW, compH)} onChange={(r) => patchActive({ inX: r.left / compW, inY: r.top / compH, inW: Math.max(1, r.width) / compW, inH: Math.max(1, r.height) / compH })} />
          : <PixelRect rect={scaleRect(outRect, screen.width, screen.height)} onChange={(r) => patchActive({ corners: fitCorners(slice.corners, scaleRect({ ...r, width: Math.max(1, r.width), height: Math.max(1, r.height) }, 1 / screen.width, 1 / screen.height)) })} />}
        <div className="row">
          <button type="button" data-testid="match-input" title="La zona de entrada toma la forma de la salida" onClick={() => patchActive(matchInputToOutput(slice, comp, out))}>Igualar entrada a salida</button>
          <button type="button" data-testid="match-output" title="La salida toma la forma de la entrada, en píxeles" onClick={() => patchActive(matchOutputToInput(slice, comp, out))}>Igualar salida a entrada</button>
        </div>
        <label className="field-enum">
          <span>Fuente</span>
          <select data-testid="slice-source" value={`${slice.sourceType}:${slice.sourceIndex}`} onChange={(e) => {
            const [sourceType, sourceIndex] = e.target.value.split(':');
            patchSelected({ sourceType, sourceIndex: Number(sourceIndex) });
          }}>
            <option value="composition:0">Composición</option>
            <option value="preview:0">Preview</option>
            <option value="bus:1">Crossfader A</option>
            <option value="bus:2">Crossfader B</option>
            {Array.from({ length: layers }, (_, i) => <option key={i} value={`layer:${i + 1}`}>Capa {i + 1}</option>)}
            {Array.from({ length: groups }, (_, i) => <option key={i} value={`group:${i + 1}`}>Grupo {i + 1}</option>)}
            {setup.screens.filter((s) => s.id !== screen.id).map((s) => <option key={s.id} value={`screen:${s.id}`}>{s.name}</option>)}
          </select>
        </label>
        {(slice.sourceType === 'layer' || slice.sourceType === 'group') && <Check label="Ignorar opacidad y bypass" on={slice.ignoreBypass} onChange={(v) => patchActive({ ignoreBypass: v })} />}
        <div className="row">
          <Check label="Bypass" on={slice.bypass} onChange={(v) => patchSelected({ bypass: v })} />
          <Check label="Flip H" on={slice.flipH} onChange={(v) => patchActive({ flipH: v })} />
          <Check label="Flip V" on={slice.flipV} onChange={(v) => patchActive({ flipV: v })} />
          <Check label="Clave" on={slice.isKey} onChange={(v) => patchActive({ isKey: v })} />
          <Check label="Fondo negro" on={slice.blackBg} onChange={(v) => patchActive({ blackBg: v })} />
        </div>
        <Num label="Rotación entrada" value={slice.inRotation} min={-180} max={180} step={1} onChange={(v) => patchActive({ inRotation: v })} />
        <Num label="Brillo" value={slice.brightness} min={0} max={2} onChange={(v) => patchActive({ brightness: v })} />
        <Num label="Contraste" value={slice.contrast} min={0} max={2} onChange={(v) => patchActive({ contrast: v })} />
        <div className="row">
          <span className="hint">Forma</span>
          <button type="button" onClick={() => patchActive({ polygon: [] })}>Rectángulo</button>
          <button type="button" onClick={() => patchActive({ polygon: regularPolygon(3) })}>Triángulo</button>
          <button type="button" data-testid="polygon-hex" onClick={() => patchActive({ polygon: regularPolygon(6) })}>Hexágono</button>
        </div>
        </Section>
        <Section title="Máscara del slice">
          <div className="row" data-help="Recorta este slice. La deformación sigue a las esquinas del slice, no a la máscara.">
            <button type="button" data-testid="slice-mask-rect" title="Rectángulo" onClick={() => addSliceMask([[0.2, 0.2], [0.8, 0.2], [0.8, 0.8], [0.2, 0.8]])}>▭</button>
            <button type="button" title="Círculo" onClick={() => addSliceMask(regularPolygon(4), true)}>◯</button>
            <button type="button" title="Triángulo" onClick={() => addSliceMask(regularPolygon(3))}>△</button>
            <button type="button" title="Hexágono" onClick={() => addSliceMask(regularPolygon(6))}>⬡</button>
            <button type="button" title="Corazón" onClick={() => addSliceMask(HEART, true)}>♡</button>
            <button type="button" data-testid="slice-mask-draw" title="Dibujar: clic en cada punto y doble clic para cerrar" onClick={() => { setTab('input'); setDrawingMask(true); setDrawing([]); }}>✎</button>
          </div>
          {slice.masks.map((m, i) => (
            <div key={i} className="row">
              <button type="button" className={i === sliceMask ? 'toggle on' : 'toggle'} onClick={() => { setSliceMask(i); setTab('input'); }}>Máscara {i + 1}</button>
              <Check label="Invertir" on={m.invert} onChange={(v) => editSliceMask(i, { invert: v }, true)} />
              <Check label="Curva" on={m.smooth} onChange={(v) => editSliceMask(i, { smooth: v }, true)} />
              <button type="button" title="Quitar máscara" onClick={() => patchActive({ masks: slice.masks.filter((_, k) => k !== i) })}>✕</button>
            </div>
          ))}
        </Section>
        <Section title="Contenido">
        <Num label="Posición X" value={slice.contentX} min={-1} max={1} onChange={(v) => patchSelected({ contentX: v })} />
        <Num label="Posición Y" value={slice.contentY} min={-1} max={1} onChange={(v) => patchSelected({ contentY: v })} />
        <Num label="Escala" value={slice.contentScale} min={0.1} max={5} onChange={(v) => patchSelected({ contentScale: v })} />
        <Num label="Rotación" value={slice.contentRotation} min={-180} max={180} step={1} onChange={(v) => patchSelected({ contentRotation: v })} />
        </Section>
        <Section title="Contorno">
        <div className="row">
          <Check label="Contorno" on={slice.outline} onChange={(v) => patchSelected({ outline: v })} />
          <Check label="Solo contorno" on={slice.outlineOnly} onChange={(v) => patchSelected({ outlineOnly: v })} />
        </div>
        {(slice.outline || slice.outlineOnly) && (
          <>
            <Num label="Grosor" value={slice.outlineWidth} min={0.0005} max={0.1} step={0.0005} onChange={(v) => patchSelected({ outlineWidth: v })} />
            <Num label="Rojo" value={slice.outlineRed} min={0} max={1} onChange={(v) => patchSelected({ outlineRed: v })} />
            <Num label="Verde" value={slice.outlineGreen} min={0} max={1} onChange={(v) => patchSelected({ outlineGreen: v })} />
            <Num label="Azul" value={slice.outlineBlue} min={0} max={1} onChange={(v) => patchSelected({ outlineBlue: v })} />
          </>
        )}
        </Section>
        <Section title="Deformación" open={tab === 'output'}>
        <div className="row">
          <button type="button" onClick={() => patchActive({ corners: [[0, 0], [1, 0], [1, 1], [0, 1]], warp: [] })}>Restablecer</button>
          <label>Columnas <select data-testid="warp-columns" value={slice.warp.length ? slice.warpColumns : 2} onChange={(e) => { const c = Number(e.target.value); patchActive({ warpColumns: c, warp: warpGrid(c, slice.warpRows) }); }}>{[2, 3, 4, 5, 6, 8, 10, 12, 16].map((n) => <option key={n}>{n}</option>)}</select></label>
          <label>Filas <select value={slice.warp.length ? slice.warpRows : 2} onChange={(e) => { const r = Number(e.target.value); patchActive({ warpRows: r, warp: warpGrid(slice.warpColumns, r) }); }}>{[2, 3, 4, 5, 6, 8, 10, 12, 16].map((n) => <option key={n}>{n}</option>)}</select></label>
          <button type="button" data-testid="warp-grid" onClick={() => patchActive({ warpColumns: 4, warpRows: 3, warp: warpGrid(4, 3) })}>Rejilla 4×3</button>
          <Check label="Bezier" on={slice.warpMode === 'bezier'} onChange={(v) => patchActive({ warpMode: v ? 'bezier' : 'linear' })} />
        </div>
        </Section>
        <Section title="Edge blending">
        <Check label="Activar" on={slice.blend} onChange={(v) => patchActive({ blend: v })} />
        {slice.blend && (
          <>
            <Num label="Izquierda" value={slice.blendLeft} min={0} max={0.5} onChange={(v) => patchActive({ blendLeft: v })} />
            <Num label="Derecha" value={slice.blendRight} min={0} max={0.5} onChange={(v) => patchActive({ blendRight: v })} />
            <Num label="Arriba" value={slice.blendTop} min={0} max={0.5} onChange={(v) => patchActive({ blendTop: v })} />
            <Num label="Abajo" value={slice.blendBottom} min={0} max={0.5} onChange={(v) => patchActive({ blendBottom: v })} />
            <Num label="Potencia" value={slice.blendPower} min={1} max={6} onChange={(v) => patchActive({ blendPower: v })} />
            <Num label="Luminancia" value={slice.blendLuminance} min={0.1} max={0.9} onChange={(v) => patchActive({ blendLuminance: v })} />
            <Num label="Gamma" value={slice.blendGamma} min={1} max={3} onChange={(v) => patchActive({ blendGamma: v })} />
            <Num label="Nivel de negro" value={slice.blackLevel} min={0} max={0.2} onChange={(v) => patchActive({ blackLevel: v })} />
            <Num label="Gamma rojo" value={slice.blendGammaRed} min={0.2} max={3} onChange={(v) => patchActive({ blendGammaRed: v })} />
            <Num label="Gamma verde" value={slice.blendGammaGreen} min={0.2} max={3} onChange={(v) => patchActive({ blendGammaGreen: v })} />
            <Num label="Gamma azul" value={slice.blendGammaBlue} min={0.2} max={3} onChange={(v) => patchActive({ blendGammaBlue: v })} />
          </>
        )}
        </Section>
        <Section title="Máscaras de pantalla">
          <div className="row">
            <button type="button" data-testid="mask-rect" onClick={() => addMask([[0.35, 0.35], [0.65, 0.35], [0.65, 0.65], [0.35, 0.65]])}>＋ Rectángulo</button>
            <button type="button" onClick={() => addMask(regularPolygon(3).map(([x, y]) => [0.35 + x * 0.3, 0.35 + y * 0.3] as Pt))}>＋ Triángulo</button>
            <button type="button" onClick={() => addMask(regularPolygon(24).map(([x, y]) => [0.35 + x * 0.3, 0.35 + y * 0.3] as Pt))}>＋ Círculo</button>
          </div>
          {screen.masks.map((m, i) => (
            <div key={i} className="row">
              <button type="button" className={i === maskIndex ? 'toggle on' : 'toggle'} onClick={() => { setMaskIndex(i); setTab('output'); }}>Máscara {i + 1}</button>
              <Check label="Invertir" on={m.invert} onChange={(v) => change((s) => ({ ...s, masks: s.masks.map((q, k) => (k === i ? { ...q, invert: v } : q)) }))} />
              <Check label="Curva" on={m.smooth} onChange={(v) => change((s) => ({ ...s, masks: s.masks.map((q, k) => (k === i ? { ...q, smooth: v } : q)) }))} />
              <button type="button" title="Quitar máscara" onClick={() => change((s) => ({ ...s, masks: s.masks.filter((_, k) => k !== i) }))}>✕</button>
            </div>
          ))}
        </Section>
      </aside>
      {menu && menuSlice && (
        <>
          <div className="menu-veil" onClick={() => setMenu(null)} onContextMenu={(e) => { e.preventDefault(); setMenu(null); }} />
          <div className="menu" role="menu" style={{ left: menu.x, top: Math.max(8, Math.min(menu.y, window.innerHeight - 660)), maxHeight: 'calc(100vh - 16px)', overflowY: 'auto' }}>
            <button type="button" data-testid="menu-center-x" onClick={closeMenu(() => patchSlice(menuSlice.id, centerSlice(menuSlice, tab, 'x'), true))}>Centrar X</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, centerSlice(menuSlice, tab, 'y'), true))}>Centrar Y</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, mirrorSlice(menuSlice, tab, 'x'), true))}>Reflejar X</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, mirrorSlice(menuSlice, tab, 'y'), true))}>Reflejar Y</button>
            <hr />
            {([['left', 'Mitad izquierda'], ['top', 'Mitad superior'], ['right', 'Mitad derecha'], ['bottom', 'Mitad inferior'], ['whole', 'Toda el área']] as [SliceArea, string][]).map(([area, label]) => (
              <button key={area} type="button" data-area={area} onClick={closeMenu(() => patchSlice(menuSlice.id, placeSlice(menuSlice, tab, AREAS[area]), true))}>{label}</button>
            ))}
            <hr />
<button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, { ...placeSlice(menuSlice, tab, AREAS.whole), polygon: regularPolygon(3) }, true))}>Hacer triángulo</button>
            <button type="button" data-testid="menu-circle" onClick={closeMenu(() => patchSlice(menuSlice.id, makeCircle(menuSlice, tab, tab === 'input' ? comp : out), true))}>Hacer círculo</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, matchInputToOutput(menuSlice, comp, out), true))}>Igualar entrada a salida</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, matchOutputToInput(menuSlice, comp, out), true))}>Igualar salida a entrada</button>
            <button type="button" data-testid="menu-swap" onClick={closeMenu(() => patchSlice(menuSlice.id, swapShapes(menuSlice, comp, out), true))}>Intercambiar entrada y salida</button>
            <hr />
            <button type="button" disabled={screen.slices[screen.slices.length - 1].id === menuSlice.id} onClick={closeMenu(() => change((s) => reorderSlice(s, menuSlice.id, 1)))}>Traer adelante</button>
            <button type="button" disabled={screen.slices[0].id === menuSlice.id} onClick={closeMenu(() => change((s) => reorderSlice(s, menuSlice.id, -1)))}>Enviar atrás</button>
            <hr />
            <button type="button" onClick={closeMenu(() => { const r = duplicateSlice(screen, menuSlice); change(() => r.screen); setSliceId(r.id); setExtra([r.id]); })}>Duplicar</button>
            <button type="button" data-testid="menu-copy" onClick={closeMenu(copySelected)}>Copiar</button>
            <button type="button" disabled={screen.slices.length <= selected.length} onClick={closeMenu(() => { copySelected(); removeSelected(); })}>Cortar</button>
            <button type="button" data-testid="menu-paste" disabled={!copied.length} onClick={closeMenu(paste)}>Pegar</button>
            <hr />
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, { inX: 0, inY: 0, inW: 1, inH: 1, inRotation: 0 }, true))}>Ajustar a la composición</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, { corners: [[0, 0], [1, 0], [1, 1], [0, 1]], warp: [] }, true))}>Ajustar a la pantalla</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, { flipH: !menuSlice.flipH }, true))}>Voltear horizontal</button>
            <button type="button" onClick={closeMenu(() => patchSlice(menuSlice.id, { flipV: !menuSlice.flipV }, true))}>Voltear vertical</button>
            {[2, 3, 4].map((n) => <button key={n} type="button" data-split={n} onClick={closeMenu(() => change((s) => splitSlice(s, menuSlice, n, false)))}>Dividir en {n} columnas</button>)}
            {[2, 3].map((n) => <button key={n} type="button" onClick={closeMenu(() => change((s) => splitSlice(s, menuSlice, n, true)))}>Dividir en {n} filas</button>)}
            <button type="button" disabled={screen.slices.length < 2} onClick={closeMenu(() => change((s) => ({ ...s, slices: s.slices.filter((l) => l.id !== menuSlice.id) })))}>Quitar</button>
          </div>
        </>
      )}
    </div>
  );
}
