import { useCallback, useEffect, useRef, useState } from 'react';
import { useEngine } from '@vj/api-client';
import { Row, Slider, Toggle } from './controls';
import { envelopeAdd, envelopeDrag, envelopeRemove, type EnvelopePoint } from './paths.extra';

interface Config {
  mode: string;
  in?: number;
  out?: number;
  duration?: number;
  beats?: number;
  speed?: number;
  playmode?: string;
  direction?: string;
  phaseLock?: boolean;
  expr?: string;
  envelope?: EnvelopePoint[];
  start?: { load?: boolean; clip?: boolean; column?: boolean; manual?: boolean };
  fft?: { source?: string; band?: string; gain?: number; fall?: number; drive?: string };
}

let target: { path: string; label: string } | null = null;
const listeners = new Set<() => void>();

// Lo abre el botón ∿ de cualquier parámetro.
export function openAnimation(path: string, label: string) {
  target = { path, label };
  listeners.forEach((fn) => fn());
  document.querySelector<HTMLButtonElement>('[data-tab=animation]')?.click();
}

const MODES: [string, string][] = [['timeline', 'Timeline'], ['bpm', 'BPM Sync'], ['clip', 'Posición de clip'], ['fft', 'Audio (FFT)'], ['crossfader', 'Crossfader'], ['expression', 'Expresión']];

function Envelope({ points, onChange }: { points: EnvelopePoint[]; onChange: (p: EnvelopePoint[]) => void }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const drag = useRef<number | null>(null);
  useEffect(() => {
    const c = canvas.current;
    const g = c?.getContext('2d');
    if (!c || !g) return;
    c.width = c.clientWidth;
    c.height = c.clientHeight;
    g.clearRect(0, 0, c.width, c.height);
    g.strokeStyle = '#ff9f1c';
    g.fillStyle = '#ff9f1c';
    g.beginPath();
    points.forEach(([x, y], i) => g[i ? 'lineTo' : 'moveTo'](x * c.width, (1 - y) * c.height));
    g.stroke();
    for (const [x, y] of points) g.fillRect(x * c.width - 3, (1 - y) * c.height - 3, 6, 6);
  }, [points]);
  const at = (e: React.PointerEvent | React.MouseEvent): EnvelopePoint => {
    const r = canvas.current!.getBoundingClientRect();
    return [Math.min(1, Math.max(0, (e.clientX - r.left) / r.width)), Math.min(1, Math.max(0, 1 - (e.clientY - r.top) / r.height))];
  };
  const near = (p: EnvelopePoint) => points.findIndex(([x, y]) => Math.hypot(x - p[0], y - p[1]) < 0.06);
  return (
    <canvas ref={canvas} className="envelope" data-testid="envelope"
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId);
        const p = at(e);
        const hit = near(p);
        if (hit >= 0) drag.current = hit;
        else {
          const next = envelopeAdd(points, p);
          drag.current = next.findIndex((q) => q === p);
          onChange(next);
        }
      }}
      onPointerMove={(e) => { if (drag.current !== null) onChange(envelopeDrag(points, drag.current, at(e))); }}
      onPointerUp={() => { drag.current = null; }}
      onContextMenu={(e) => {
        e.preventDefault();
        const hit = near(at(e));
        if (hit >= 0) onChange(envelopeRemove(points, hit));
      }} />
  );
}

export function AnimationPanel() {
  const engine = useEngine();
  const [, bump] = useState(0);
  const [config, setConfig] = useState<Config | null>(null);
  const [presets, setPresets] = useState('');
  const [message, setMessage] = useState('');
  const edited = useRef(false);
  useEffect(() => {
    const fn = () => bump((n) => n + 1);
    listeners.add(fn);
    return () => void listeners.delete(fn);
  }, []);
  const path = target?.path;
  useEffect(() => {
    if (!path) return;
    edited.current = false;
    // Una respuesta que llega tarde no pisa lo que el usuario ya ha editado.
    let stale = false;
    void engine.run<Config | null>('animation.get', { path }).then(
      (c) => { if (!stale && !edited.current) setConfig(c); },
      () => { if (!stale && !edited.current) setConfig(null); },
    );
    return () => { stale = true; };
  }, [engine, path]);

  const send = useCallback((next: Config) => {
    edited.current = true;
    setConfig(next);
    engine.run('animation.set', { path, config: next }).then(() => setMessage(''), (e: Error) => setMessage(e.message));
  }, [engine, path]);
  const patch = (p: Partial<Config>) => { if (config) send({ ...config, ...p }); };

  if (!target) return <p className="hint">Pasa el ratón por un parámetro del inspector y pulsa ∿ para animarlo.</p>;
  if (!config) {
    return (
      <div>
        <h3 className="inspect-title">{target.label}</h3>
        <button type="button" data-testid="anim-add" onClick={() => send({ mode: 'timeline', duration: 2, start: { load: true } })}>Animar</button>
      </div>
    );
  }
  const fft = config.fft ?? {};
  const start = config.start ?? {};
  const points = config.envelope ?? [[0, 0], [1, 1]];
  const select = (label: string, value: string, options: string[], on: (v: string) => void) => (
    <label className="field-enum"><span>{label}</span><select value={value} onChange={(e) => on(e.target.value)}>{options.map((o) => <option key={o}>{o}</option>)}</select></label>
  );
  return (
    <div className="animation">
      <h3 className="inspect-title" title={path}>{target.label}</h3>
      <label className="field-enum">
        <span>Modo</span>
        <select value={config.mode} data-testid="anim-mode" onChange={(e) => patch({ mode: e.target.value })}>
          {MODES.map(([id, name]) => <option key={id} value={id}>{name}</option>)}
        </select>
      </label>
      {config.mode === 'timeline' && <Slider label="Duración" value={config.duration ?? 2} min={0.05} max={60} onChange={(v) => patch({ duration: v })} format={(v) => `${v.toFixed(2)} s`} />}
      {config.mode === 'bpm' && (
        <>
          <Slider label="Beats" value={config.beats ?? 4} min={0.25} max={64} step={0.25} onChange={(v) => patch({ beats: v })} format={(v) => String(v)} />
          <Toggle label="Fase con el BPM maestro" on={config.phaseLock ?? true} onChange={(v) => patch({ phaseLock: v })} />
        </>
      )}
      {(config.mode === 'timeline' || config.mode === 'bpm') && (
        <>
          {select('Playmode', config.playmode ?? 'Loop', ['Loop', 'Ping Pong', 'Random', 'Play Once and Hold'], (v) => patch({ playmode: v }))}
          {select('Dirección', config.direction ?? 'Forward', ['Forward', 'Backward', 'Pause'], (v) => patch({ direction: v }))}
          <Slider label="Velocidad" value={config.speed ?? 1} min={0} max={10} onChange={(v) => patch({ speed: v })} />
        </>
      )}
      {config.mode === 'fft' && (
        <>
          {select('Fuente', fft.source ?? 'composition', ['composition', 'external', 'clip', 'layer', 'group'], (v) => patch({ fft: { ...fft, source: v } }))}
          {select('Banda', fft.band ?? 'L', ['L', 'M', 'H'], (v) => patch({ fft: { ...fft, band: v } }))}
          {select('Dirección', fft.drive ?? '>', ['>', '<', '+', '-'], (v) => patch({ fft: { ...fft, drive: v } }))}
          <Slider label="Ganancia" value={fft.gain ?? 1} min={0} max={16} onChange={(v) => patch({ fft: { ...fft, gain: v } })} />
          <Slider label="Caída" value={fft.fall ?? 2} min={0} max={20} onChange={(v) => patch({ fft: { ...fft, fall: v } })} />
        </>
      )}
      {config.mode === 'expression' && (
        <label className="field-enum">
          <span>Fórmula</span>
          <input type="text" data-testid="anim-expr" defaultValue={config.expr ?? ''} placeholder="fract(beat / 4)" onBlur={(e) => patch({ expr: e.target.value })} />
        </label>
      )}
      <Slider label="Entrada" value={config.in ?? 0} min={0} max={1} onChange={(v) => patch({ in: v })} />
      <Slider label="Salida" value={config.out ?? 1} min={0} max={1} onChange={(v) => patch({ out: v })} />
      <p className="hint">Envolvente: clic añade, arrastrar mueve, clic derecho quita.</p>
      <Envelope points={points} onChange={(p) => patch({ envelope: p })} />
      <Row>
        <span className="hint">Arranca con</span>
        <Toggle label="Composición" on={start.load ?? false} onChange={(v) => patch({ start: { ...start, load: v } })} />
        <Toggle label="Clip" on={start.clip ?? false} onChange={(v) => patch({ start: { ...start, clip: v } })} />
        <Toggle label="Columna" on={start.column ?? false} onChange={(v) => patch({ start: { ...start, column: v } })} />
      </Row>
      <div className="anim-actions">
        <button type="button" onClick={() => void engine.run('animation.start', { path })}>Arrancar</button>
        <button type="button" data-testid="anim-remove" onClick={() => { void engine.run('animation.remove', { path }); setConfig(null); }}>Quitar</button>
        <input type="text" placeholder="Nombre de preset" value={presets} onChange={(e) => setPresets(e.target.value)} />
        <button type="button" disabled={!presets} onClick={() => void engine.run('animation.preset.save', { path, name: presets }).then(() => setMessage('Preset guardado'))}>Guardar</button>
        <button type="button" disabled={!presets} onClick={() => void engine.run('animation.preset.apply', { path, name: presets }).then(() => engine.run<Config>('animation.get', { path })).then(setConfig, (e: Error) => setMessage(e.message))}>Aplicar</button>
      </div>
      {message && <p className="hint">{message}</p>}
    </div>
  );
}
