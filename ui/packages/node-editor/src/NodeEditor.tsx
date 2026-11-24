import { useCallback, useEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent as RPointerEvent } from 'react';
import { useConnection, useEngine } from '@vj/api-client';
import { addNode, connect, duplicate, emptyPatch, History, linkError, type NodeDef, type Patch, type PatchNode, removeNodes, search, setHidden } from './model';

const NODE_W = 170, HEAD = 26, ROW = 22;
const color: Record<string, string> = { float: '#2ec4b6', int: '#6fa8ff', bool: '#ffd166', trigger: '#ff6b6b', string: '#c792ea', color: '#ff9f1c', texture: '#f4f4f4' };

function visibleInputs(n: PatchNode, def?: NodeDef) { return (def?.inputs ?? []).filter((p) => !n.hidden.includes(p.name)); }
function outPos(n: PatchNode, def: NodeDef | undefined, port: string) {
  const i = (def?.outputs ?? []).findIndex((p) => p.name === port);
  return { x: n.x + NODE_W, y: n.y + HEAD + (i + 0.5) * ROW };
}
function inPos(n: PatchNode, def: NodeDef | undefined, port: string) {
  const i = visibleInputs(n, def).findIndex((p) => p.name === port);
  return { x: n.x, y: n.y + HEAD + (Math.max(i, 0) + 0.5) * ROW };
}
const wire = (a: { x: number; y: number }, b: { x: number; y: number }) => {
  const dx = Math.max(40, Math.abs(b.x - a.x) / 2);
  return `M${a.x} ${a.y} C${a.x + dx} ${a.y} ${b.x - dx} ${b.y} ${b.x} ${b.y}`;
};

export function NodeEditor() {
  const engine = useEngine();
  const connection = useConnection();
  const [defs, setDefs] = useState<NodeDef[]>([]);
  const [patch, setPatch] = useState<Patch>(emptyPatch);
  const [selected, setSelected] = useState<number[]>([]);
  const [message, setMessage] = useState('');
  const [errors, setErrors] = useState<Record<string, string>>({});
  const [saved, setSaved] = useState<string[]>([]);
  const [finder, setFinder] = useState<{ x: number; y: number; text: string; from?: { node: number; port: string; type: string } } | null>(null);
  const [drag, setDrag] = useState<{ node: number; port: string; type: string; x: number; y: number } | null>(null);
  const [box, setBox] = useState<{ x0: number; y0: number; x1: number; y1: number } | null>(null);
  const history = useRef(new History());
  const canvas = useRef<HTMLCanvasElement>(null);
  const area = useRef<HTMLDivElement>(null);
  const byType = useMemo(() => new Map(defs.map((d) => [d.id, d])), [defs]);

  const send = useCallback((p: Patch, preview = 0) => {
    void engine.run('patch.set', { patch: p, preview }).catch((e: Error) => setMessage(e.message));
  }, [engine]);
  // Todo cambio de datos pasa por aquí: guarda el deshacer y lo envía al motor.
  const change = useCallback((next: Patch) => {
    history.current.push(patch);
    setPatch(next);
    send(next);
  }, [patch, send]);

  const refreshList = useCallback(() => void engine.run<string[]>('patch.list').then(setSaved).catch(() => {}), [engine]);
  useEffect(() => {
    if (connection !== 'connected') return undefined;
    void engine.run<NodeDef[]>('patch.catalog').then(setDefs).catch((e: Error) => setMessage(e.message));
    void engine.run<Patch>('patch.get').then(setPatch).catch(() => {});
    refreshList();
    const t = setInterval(() => void engine.run<{ errors: Record<string, string> }>('patch.status').then((s) => setErrors(s.errors)).catch(() => {}), 1000);
    return () => clearInterval(t);
  }, [engine, connection, refreshList]);
  useEffect(() => engine.onMonitor((f) => {
    if (f.monitor !== 2 || !canvas.current) return;
    const pixels = new Uint8ClampedArray(f.pixels.buffer as ArrayBuffer, f.pixels.byteOffset, f.pixels.length);
    canvas.current.getContext('2d')?.putImageData(new ImageData(pixels, f.width, f.height), 0, 0);
  }), [engine]);

  const point = (e: { clientX: number; clientY: number }) => {
    const r = area.current!.getBoundingClientRect();
    return { x: e.clientX - r.left + area.current!.scrollLeft, y: e.clientY - r.top + area.current!.scrollTop };
  };
  const create = (def: NodeDef) => {
    if (!finder) return;
    let next = addNode(patch, def, finder.x, finder.y);
    const id = next.nodes[next.nodes.length - 1].id;
    // Parcheo rápido: soltar un cable en el vacío crea el nodo ya conectado.
    if (finder.from) {
      const from = finder.from;
      const port = def.inputs.find((p) => !linkError(from.type, p.type));
      if (port) next = connect(next, { from: from.node, fromPort: from.port, to: id, toPort: port.name });
    }
    change(next);
    setSelected([id]);
    setFinder(null);
  };
  const tryConnect = (to: PatchNode, port: { name: string; type: string }) => {
    if (!drag) return;
    const err = linkError(drag.type, port.type);
    if (err) setMessage(err);
    else change(connect(patch, { from: drag.node, fromPort: drag.port, to: to.id, toPort: port.name }));
    setDrag(null);
  };
  const startMove = (e: RPointerEvent, n: PatchNode) => {
    e.stopPropagation();
    const group = selected.includes(n.id) ? selected : [n.id];
    if (e.metaKey || e.ctrlKey) setSelected(selected.includes(n.id) ? selected.filter((i) => i !== n.id) : [...selected, n.id]);
    else if (!selected.includes(n.id)) setSelected([n.id]);
    const start = { x: e.clientX, y: e.clientY }, before = patch;
    let last = patch;
    const move = (m: PointerEvent) => {
      last = { ...before, nodes: before.nodes.map((q) => group.includes(q.id) ? { ...q, x: q.x + m.clientX - start.x, y: q.y + m.clientY - start.y } : q) };
      setPatch(last);
    };
    const up = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      if (last !== before) { history.current.push(before); send(last); }
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  const startBox = (e: RPointerEvent) => {
    if (e.target !== e.currentTarget && !(e.target as Element).classList.contains('wires')) return;
    setSelected([]);
    const a = point(e);
    setBox({ x0: a.x, y0: a.y, x1: a.x, y1: a.y });
    const move = (m: PointerEvent) => { const b = point(m); setBox({ x0: a.x, y0: a.y, x1: b.x, y1: b.y }); };
    const up = (m: PointerEvent) => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      const b = point(m), l = Math.min(a.x, b.x), r = Math.max(a.x, b.x), t = Math.min(a.y, b.y), d = Math.max(a.y, b.y);
      setSelected(patch.nodes.filter((n) => n.x + NODE_W > l && n.x < r && n.y + HEAD > t && n.y < d).map((n) => n.id));
      setBox(null);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
  };
  const keys = (e: KeyboardEvent) => {
    if ((e.target as HTMLElement).matches('input, textarea, select')) return;
    const cmd = e.metaKey || e.ctrlKey;
    if (e.key === 'Delete' || e.key === 'Backspace') { change(removeNodes(patch, selected)); setSelected([]); }
    else if (cmd && e.key === 'z') { e.preventDefault(); const p = e.shiftKey ? history.current.redo(patch) : history.current.undo(patch); setPatch(p); send(p); }
    else if (cmd && e.key === 'd') { e.preventDefault(); const d = duplicate(patch, selected); change(d.patch); setSelected(d.ids); }
    else if (e.key === 'h') change(setHidden(patch, byType, e.shiftKey ? patch.nodes.map((n) => n.id) : selected, true));
    else if (e.key === 'j') change(setHidden(patch, byType, e.shiftKey ? patch.nodes.map((n) => n.id) : selected, false));
    else if (e.key === 'Tab') { e.preventDefault(); setFinder({ x: 80 + area.current!.scrollLeft, y: 80 + area.current!.scrollTop, text: '' }); }
    else if (e.key === 'Escape') { setFinder(null); setDrag(null); }
  };
  const editNode = (id: number, fn: (n: PatchNode) => PatchNode) => change({ ...patch, nodes: patch.nodes.map((n) => n.id === id ? fn(n) : n) });
  const save = async () => {
    const r = await engine.run<{ file: string }>('patch.save', { name: patch.name, category: patch.category }).catch((e: Error) => { setMessage(e.message); return null; });
    if (r) { setMessage(`Guardado. ${patch.category === 'none' ? '' : 'Ya está en el catálogo de Efectos, en Parches.'}`); refreshList(); }
  };
  const load = async (name: string) => {
    const r = await engine.run<{ patch: Patch; warnings: string[] }>('patch.load', { name }).catch((e: Error) => { setMessage(e.message); return null; });
    if (!r) return;
    history.current.push(patch);
    setPatch(r.patch);
    setSelected([]);
    setMessage(r.warnings.join('. '));
  };
  const sel = patch.nodes.find((n) => n.id === selected[0]);
  const selDef = sel && byType.get(sel.type);
  const results = finder ? search(defs, finder.text) : [];
  const inputLink = (n: PatchNode, port: string) => patch.links.some((l) => l.to === n.id && l.toPort === port);

  return (
    <div className="nodes" tabIndex={0} onKeyDown={keys} data-testid="node-editor">
      <header className="nodes-bar">
        <input aria-label="Nombre del parche" data-testid="patch-name" value={patch.name} onChange={(e) => setPatch({ ...patch, name: e.target.value })} onBlur={() => send(patch)} />
        <select aria-label="Tipo" data-testid="patch-category" value={patch.category} onChange={(e) => change({ ...patch, category: e.target.value })}>
          <option value="none">Solo parche</option><option value="effect">Efecto</option><option value="source">Fuente</option>
        </select>
        <button type="button" data-testid="patch-save" onClick={() => void save()}>Guardar</button>
        <button type="button" onClick={() => void engine.run<{ folder: string }>('patch.consolidate', { name: patch.name }).then((r) => setMessage(`Consolidado en ${r.folder}`)).catch((e: Error) => setMessage(e.message))}>Consolidar</button>
        <select aria-label="Abrir parche" value="" onChange={(e) => e.target.value && void load(e.target.value)}>
          <option value="">Abrir…</option>{saved.map((s) => <option key={s}>{s}</option>)}
        </select>
        <button type="button" onClick={() => { change(emptyPatch()); setSelected([]); }}>Nuevo</button>
        <span className="nodes-msg" role="status">{message}</span>
      </header>
      <div className="nodes-body">
        <div ref={area} className="nodes-canvas" data-testid="node-canvas"
          onPointerDown={startBox}
          onPointerMove={(e) => { if (drag) { const p = point(e); setDrag({ ...drag, x: p.x, y: p.y }); } }}
          onPointerUp={(e) => { if (drag) { const p = point(e); setFinder({ ...p, text: '', from: { node: drag.node, port: drag.port, type: drag.type } }); setDrag(null); } }}
          onDoubleClick={(e) => { if (e.target === e.currentTarget || (e.target as Element).classList.contains('wires')) setFinder({ ...point(e), text: '' }); }}
          onContextMenu={(e) => { e.preventDefault(); setFinder({ ...point(e), text: '' }); }}>
          <svg className="wires">
            {patch.links.map((l) => {
              const a = patch.nodes.find((n) => n.id === l.from), b = patch.nodes.find((n) => n.id === l.to);
              if (!a || !b) return null;
              const type = byType.get(a.type)?.outputs.find((p) => p.name === l.fromPort)?.type ?? 'float';
              return <path key={`${l.to}-${l.toPort}`} d={wire(outPos(a, byType.get(a.type), l.fromPort), inPos(b, byType.get(b.type), l.toPort))} stroke={color[type]} fill="none" strokeWidth={2} />;
            })}
            {drag && (() => { const a = patch.nodes.find((n) => n.id === drag.node); return a ? <path d={wire(outPos(a, byType.get(a.type), drag.port), drag)} stroke={color[drag.type]} fill="none" strokeWidth={2} strokeDasharray="5 4" /> : null; })()}
            {box && <rect x={Math.min(box.x0, box.x1)} y={Math.min(box.y0, box.y1)} width={Math.abs(box.x1 - box.x0)} height={Math.abs(box.y1 - box.y0)} fill="none" stroke="var(--accent)" strokeDasharray="4 3" />}
          </svg>
          {patch.nodes.map((n) => {
            const def = byType.get(n.type);
            const inputs = visibleInputs(n, def);
            return (
              <div key={n.id} data-node={n.type} className={`node ${selected.includes(n.id) ? 'selected' : ''} ${errors[n.id] ? 'failed' : ''}`} title={errors[n.id] ?? def?.description}
                style={{ left: n.x, top: n.y, width: NODE_W, height: HEAD + Math.max(inputs.length, def?.outputs.length ?? 0) * ROW + 4 }}>
                <div className="node-head" onPointerDown={(e) => startMove(e, n)}>{n.name}</div>
                {inputs.map((p, i) => (
                  <div key={p.name} className="port in" style={{ top: HEAD + i * ROW }} onPointerUp={(e) => { e.stopPropagation(); tryConnect(n, p); }} title={`${p.name}: ${p.type}`}>
                    <i style={{ background: color[p.type] }} />{p.name}
                    {!inputLink(n, p.name) && (p.type === 'float' || p.type === 'int') && (
                      <input type="number" step="any" aria-label={p.name} value={String(n.values[p.name] ?? p.def)} onPointerDown={(e) => e.stopPropagation()}
                        onChange={(e) => editNode(n.id, (q) => ({ ...q, values: { ...q.values, [p.name]: Number(e.target.value) } }))} />
                    )}
                  </div>
                ))}
                {def?.outputs.map((p, i) => (
                  <div key={p.name} className="port out" style={{ top: HEAD + i * ROW }} title={`${p.name}: ${p.type}`}
                    onPointerDown={(e) => { e.stopPropagation(); const q = point(e); setDrag({ node: n.id, port: p.name, type: p.type, ...q }); }}>
                    {p.name}<i style={{ background: color[p.type] }} />
                  </div>
                ))}
              </div>
            );
          })}
          {finder && (
            <div className="node-finder" style={{ left: finder.x, top: finder.y }} onPointerDown={(e) => e.stopPropagation()} onPointerUp={(e) => e.stopPropagation()}>
              <input autoFocus aria-label="Buscar nodo" data-testid="node-search" value={finder.text} onChange={(e) => setFinder({ ...finder, text: e.target.value })}
                onKeyDown={(e) => { if (e.key === 'Enter' && results[0]) create(results[0]); if (e.key === 'Escape') setFinder(null); e.stopPropagation(); }} />
              <ul>{results.slice(0, 30).map((d) => <li key={d.id}><button type="button" title={d.description} onClick={() => create(d)}><b>{d.name}</b> <small>{d.category}</small></button></li>)}</ul>
            </div>
          )}
          {!patch.nodes.length && !finder && <p className="nodes-empty">Doble clic o Tab para crear un nodo.</p>}
        </div>
        <aside className="nodes-side">
          <canvas ref={canvas} width={480} height={270} data-testid="patch-monitor" />
          {sel && selDef ? (
            <section>
              <h3>{selDef.name}</h3>
              <p className="dim">{selDef.description}</p>
              {errors[sel.id] && <p className="node-error">{errors[sel.id]}</p>}
              <label>Nombre<input value={sel.name} onChange={(e) => editNode(sel.id, (q) => ({ ...q, name: e.target.value }))} /></label>
              {selDef.inputs.filter((p) => p.type === 'string' && !inputLink(sel, p.name)).map((p) => (
                <label key={p.name}>{p.name}<input value={String(sel.values[p.name] ?? p.text)} onChange={(e) => editNode(sel.id, (q) => ({ ...q, values: { ...q.values, [p.name]: e.target.value } }))} /></label>
              ))}
              {selDef.inputs.filter((p) => p.type === 'color' && !inputLink(sel, p.name)).map((p) => {
                const c = (sel.values[p.name] as number[] | undefined) ?? [1, 1, 1, 1];
                const hex = `#${c.slice(0, 3).map((v) => Math.round(v * 255).toString(16).padStart(2, '0')).join('')}`;
                return <label key={p.name}>{p.name}<input type="color" value={hex} onChange={(e) => {
                  const v = e.target.value;
                  editNode(sel.id, (q) => ({ ...q, values: { ...q.values, [p.name]: [1, 3, 5].map((i) => parseInt(v.slice(i, i + 2), 16) / 255).concat(c[3]) } }));
                }} /></label>;
              })}
              {selDef.attrs.map((a) => {
                const value = sel.attrs[a.name] ?? a.def;
                const set = (v: unknown) => editNode(sel.id, (q) => ({ ...q, attrs: { ...q.attrs, [a.name]: v } }));
                if (a.type === 'enum') return <label key={a.name}>{a.label}<select value={String(value)} onChange={(e) => set(e.target.value)}>{a.options.map((o) => <option key={o}>{o}</option>)}</select></label>;
                if (a.type === 'code') return <label key={a.name}>{a.label}<textarea rows={12} spellCheck={false} value={String(value)} onChange={(e) => set(e.target.value)} /></label>;
                if (a.type === 'file') return <label key={a.name}>{a.label}<button type="button" onClick={() => void window.vjHost.pickMedia().then((f) => f[0] && set(f[0]))}>{String(value) || 'Elegir…'}</button></label>;
                if (a.type === 'string') return <label key={a.name}>{a.label}<input value={String(value)} onChange={(e) => set(e.target.value)} /></label>;
                return <label key={a.name}>{a.label}<input type="number" value={Number(value)} onChange={(e) => set(Number(e.target.value))} /></label>;
              })}
              <button type="button" onClick={() => void engine.run('patch.set', { patch, preview: sel.id })}>Ver este nodo</button>
              <button type="button" onClick={() => send(patch, 0)}>Ver la salida</button>
            </section>
          ) : <p className="dim">Sin selección. Tab: buscar · Supr: borrar · Cmd+D: duplicar · H / J: ocultar o mostrar puertos (Mayús: todos).</p>}
        </aside>
      </div>
    </div>
  );
}
