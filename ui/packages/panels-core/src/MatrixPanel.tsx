import { useEffect, useRef, useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import { Toggle } from './controls';
import { clipPath, columnPath, fillCells, layerPath } from './paths';
import { type CatalogEntry, clipLabel, PASTEL } from './paths.extra';
import { select, useSelection } from './selection';
import './host';

function Cell({ layer, column, columns }: { layer: number; column: number; columns: number }) {
  const engine = useEngine();
  const path = clipPath(layer, column);
  const [file] = useParam(`${path}/path`, '');
  const [status] = useParam(`${path}/status`, 'empty');
  const [live] = useParam(`${path}/connected`, false);
  const [position] = useParam(`${path}/transport/position`, 0);
  const [name] = useParam(`${path}/name`, '');
  const [thumbnail] = useParam(`${path}/thumbnail`, '');
  const [color] = useParam(`${path}/color`, 'None');
  const [persistent] = useParam(`${path}/persistent`, false);
  const [covered] = useParam(`${path}/covered`, '');
  const [custom] = useParam(`${path}/thumbnailcustom`, false);
  const [folded] = useParam(`${layerPath(layer)}/folded`, false);
  const selection = useSelection();
  const [over, setOver] = useState(false);
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [sources, setSources] = useState<CatalogEntry[]>([]);
  const [cameras, setCameras] = useState<{ id: string; name: string }[]>([]);
  const held = useRef(false);
  const selected = selection.layer === layer && selection.column === column;
  const failed = status.startsWith('error');

  const load = (paths: string[]) => {
    const cells = fillCells(paths, column, columns, (c) => Boolean(engine.value(`${clipPath(layer, c)}/path`)));
    for (const { column: c, path: p } of cells) void engine.set(`${clipPath(layer, c)}/path`, p);
  };

  const release = () => {
    if (!held.current) return;
    held.current = false;
    void engine.trigger(`${path}/release`);
  };

  const cell = { layer, column };
  const run = (name: string, args: Record<string, unknown> = {}) => void engine.run(name, { ...cell, ...args }).catch(() => {});
  const close = (fn: () => void) => () => { setMenu(null); fn(); };

  const classes = ['cell', folded && 'folded', file && 'filled', live && 'live', selected && 'selected', over && 'drop', failed && 'bad'];
  return (
    <div className="cell-wrap">
      <button
        type="button"
        className={classes.filter(Boolean).join(' ')}
        data-cell={`${layer}-${column}`}
        title={failed ? status : file}
        draggable={Boolean(file)}
        style={{
          backgroundImage: thumbnail ? `url(${thumbnail})` : undefined,
          borderColor: !live && !selected && PASTEL[color] ? PASTEL[color] : undefined,
        }}
        onPointerDown={(e) => {
          if (e.button !== 0) return;
          select({ layer, column });
          void engine.set('/composition/activelayer', layer);
          void engine.set('/composition/selectedclip', column);
          if (file && !failed && !e.altKey) {
            held.current = true;
            void engine.trigger(`${path}/connect`);
          }
        }}
        onPointerUp={release}
        onPointerLeave={release}
        onClick={async () => { if (!file) load(await window.vjHost.pickMedia()); }}
        onDoubleClick={() => { if (file) run('preview.clip'); }}
        onContextMenu={(e) => {
          e.preventDefault();
          select({ layer, column });
          setMenu({ x: e.clientX, y: e.clientY });
          void engine.run<CatalogEntry[]>('effects.catalog').then((all) => setSources(all.filter((t) => t.source)));
          void engine.run<{ id: string; name: string }[]>('media.cameras').then(setCameras).catch(() => {});
        }}
        onDragStart={(e) => e.dataTransfer.setData('application/x-vj-clip', JSON.stringify(cell))}
        onDragOver={(e) => { e.preventDefault(); setOver(true); }}
        onDragLeave={() => setOver(false)}
        onDrop={(e) => {
          e.preventDefault();
          setOver(false);
          const moved = e.dataTransfer.getData('application/x-vj-clip');
          const listed = e.dataTransfer.getData('application/x-vj-files');
          const catalog = e.dataTransfer.getData('application/x-vj-catalog');
          if (catalog) {
            const item = JSON.parse(catalog) as { kind: string; id: string };
            if (item.kind === 'source') run('source.add', { type: item.id });
            else if (file) void engine.run('effect.add', { entity: path, type: item.id }).catch(() => {});
          } else if (moved) {
            const from = JSON.parse(moved) as { layer: number; column: number };
            if (from.layer !== layer || from.column !== column)
              void engine.run('clip.move', { ...from, toLayer: layer, toColumn: column, copy: e.altKey }).catch(() => {});
          } else if (listed) {
            load(JSON.parse(listed));
          } else {
            load([...e.dataTransfer.files].map((f) => window.vjHost.pathForFile(f)));
          }
        }}
      >
        {file && (
          <span className="cell-name" title="Clic: ver en Preview sin mandarlo a la salida"
            onPointerDown={(e) => e.stopPropagation()}
            onClick={(e) => { e.stopPropagation(); select({ layer, column }); run('preview.clip'); }}>{clipLabel(name, file)}</span>
        )}
        {status === 'loading' && <span className="cell-note">cargando</span>}
        {failed && <span className="cell-note">no encontrado</span>}
        {persistent && <span className="cell-pin" title="Persistente">●</span>}
        {live && <i className="cell-progress" style={{ transform: `scaleX(${position})` }} />}
      </button>
      {covered && (
        <button type="button" className="cell-covered" title={`Liberar clip tapado: ${covered}`} onClick={() => run('clip.releaseCovered')}>⧉</button>
      )}
      {menu && (
        <>
          <div className="menu-veil" onClick={() => setMenu(null)} onContextMenu={(e) => { e.preventDefault(); setMenu(null); }} />
          <div className="menu" role="menu" style={{ left: menu.x, top: menu.y }}>
            {file && <button type="button" onClick={close(() => run('preview.clip'))}>Abrir en Preview</button>}
            {file && <button type="button" onClick={close(() => run('clip.render'))}>Renderizar a archivo</button>}
            {file && <button type="button" onClick={close(() => run('clip.copy'))}>Copiar</button>}
            {file && <button type="button" onClick={close(() => run('clip.cut'))}>Cortar</button>}
            <button type="button" onClick={close(() => run('clip.paste'))}>Pegar</button>
            {!file && <button type="button" onClick={close(() => void engine.set(`${path}/path`, 'vj://pattern', true))}>Carta de ajuste en la celda</button>}
            {!file && cameras.map((d) => (
              <button key={d.id} type="button" onClick={close(() => void engine.set(`${path}/path`, `vj://camera/${d.id}`, true))}>Cámara: {d.name}</button>
            ))}
            {!file && sources.map((t) => (
              <button key={t.id} type="button" data-source={t.id} onClick={close(() => run('source.add', { type: t.id }))}>Fuente: {t.name}</button>
            ))}
            {file && <button type="button" onClick={close(() => run('clip.clear'))}>Vaciar</button>}
            {file && <button type="button" onClick={close(() => void engine.set(`${path}/persistent`, !persistent, true))}>{persistent ? 'Quitar persistente' : 'Hacer persistente'}</button>}
            {file && !file.startsWith('vj://') && <button type="button" onClick={close(() => void window.vjHost.reveal(file))}>Mostrar en Finder</button>}
            {file && <button type="button" onClick={close(() => run('clip.thumbnail', { mode: 'current' }))}>Miniatura: fotograma actual</button>}
            {file && <button type="button" onClick={close(async () => {
              const image = await window.vjHost.pickImage();
              if (image) run('clip.thumbnail', { mode: 'file', path: image });
            })}>Miniatura: desde archivo…</button>}
            {custom && <button type="button" onClick={close(() => run('clip.thumbnail', { mode: 'revert' }))}>Miniatura: original</button>}
            {file && (
              <div className="menu-colors">
                {Object.keys(PASTEL).map((c) => (
                  <button key={c} type="button" title={c} aria-label={`Color ${c}`} style={{ background: PASTEL[c] || 'var(--panel2)' }}
                    onClick={close(() => void engine.set(`${path}/color`, c, true))} />
                ))}
              </div>
            )}
          </div>
        </>
      )}
    </div>
  );
}

function VFader({ label, title, value, min = 0, max = 1, onChange, onReset }: { label: string; title: string; value: number; min?: number; max?: number; onChange: (v: number) => void; onReset: () => void }) {
  return (
    <label className="vfader" title={title} onContextMenu={(e) => { e.preventDefault(); onReset(); }}>
      <span>{label}</span>
      <input type="range" min={min} max={max} step={(max - min) / 100} value={value} aria-label={title} onChange={(e) => onChange(Number(e.target.value))} />
    </label>
  );
}

function LayerHeader({ layer }: { layer: number }) {
  const engine = useEngine();
  const path = layerPath(layer);
  const selection = useSelection();
  const [master, setMaster] = useParam(`${path}/master`, 1);
  const [opacity, setOpacity] = useParam(`${path}/opacity`, 1);
  const [volume, setVolume] = useParam(`${path}/audio/volume`, 0);
  const [bypass, setBypass] = useParam(`${path}/bypass`, false);
  const [solo, setSolo] = useParam(`${path}/solo`, false);
  const [blend, setBlend] = useParam(`${path}/blend`, 'Add');
  const [bus, setBus] = useParam(`${path}/crossfaderbus`, 'None');
  const [group] = useParam(`${path}/group`, 0);
  const [name, setName] = useParam(`${path}/name`, '');
  const [lock, setLock] = useParam(`${path}/lock`, false);
  const [ignore, setIgnore] = useParam(`${path}/ignorecolumn`, false);
  const [mask, setMask] = useParam(`${path}/mask`, '');
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [renaming, setRenaming] = useState(false);
  const [folded, setFolded] = useParam(`${path}/folded`, false);
  const [, setGroup] = useParam(`${path}/group`, 0);
  const [groups] = useParam('/composition/structure/groups', 0);
  const masks = engine.params.get(`${path}/mask`)?.options ?? [];
  const close = (fn: () => void) => () => { setMenu(null); fn(); };
  const structure = (cmd: string, args: Record<string, unknown> = {}) => void engine.run(cmd, args).catch((e: Error) => void window.vjHost.confirm('No se puede', e.message));
  const [layers] = useParam('/composition/structure/layers', 0);
  const blends = engine.params.get(`${path}/blend`)?.options ?? [blend];
  const choose = () => { select({ ...selection, layer }); void engine.set('/composition/activelayer', layer); };
  return (
    <div className={['layer', selection.layer === layer && 'selected', folded && 'folded'].filter(Boolean).join(' ')} data-layer={layer} onClick={choose}
      onContextMenu={(e) => { e.preventDefault(); choose(); setMenu({ x: e.clientX, y: e.clientY }); }}>
      <div className="layer-main" onClick={(e) => e.stopPropagation()}>
        <button type="button" className="layer-eject" title="Expulsa el clip de la capa" data-layer-eject={layer} onClick={() => void engine.trigger(`${path}/eject`)}>✕</button>
        <Toggle label="B" title="Bypass: oculta esta capa" on={bypass} onChange={setBypass} />
        <Toggle label="S" title="Solo: muestra solo esta capa" on={solo} onChange={setSolo} />
        <select value={blend} aria-label="Modo de fusión" title="Modo de fusión de la capa" onChange={(e) => setBlend(e.target.value)}>
          {blends.map((o) => <option key={o} value={o}>{o}</option>)}
        </select>
      </div>
      <div className="layer-faders" onClick={(e) => e.stopPropagation()}>
        <VFader label="M" title="Máster: audio y vídeo de la capa" value={master} onChange={setMaster} onReset={() => void engine.reset(`${path}/master`)} />
        <VFader label="A" title="Audio: volumen de la capa" value={volume} min={-60} max={12} onChange={setVolume} onReset={() => void engine.reset(`${path}/audio/volume`)} />
        <VFader label="V" title="Vídeo: opacidad de la capa" value={opacity} onChange={setOpacity} onReset={() => void engine.reset(`${path}/opacity`)} />
      </div>
      <div className="layer-name">
        {renaming ? (
          <input type="text" autoFocus defaultValue={name} aria-label="Nombre de la capa"
            onBlur={(e) => { setName(e.target.value); setRenaming(false); }}
            onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') setRenaming(false); }} />
        ) : <b title="Doble clic: renombrar" onDoubleClick={() => setRenaming(true)}>{group > 0 ? `G${group} · ` : ''}{name || `Capa ${layer}`}{lock ? ' 🔒' : ''}</b>}
        <span onClick={(e) => e.stopPropagation()}>
          <button type="button" data-testid={`layer-fold-${layer}`} title={folded ? 'Desplegar' : 'Plegar'} onClick={() => setFolded(!folded)}>{folded ? '+' : '−'}</button>
          <Toggle label="A" title="Asigna la capa al lado A del crossfader" on={bus === 'A'} onChange={(on) => setBus(on ? 'A' : 'None')} />
          <Toggle label="B" title="Asigna la capa al lado B del crossfader" on={bus === 'B'} onChange={(on) => setBus(on ? 'B' : 'None')} />
        </span>
      </div>
      {menu && (
        <>
          <div className="menu-veil" onClick={(e) => { e.stopPropagation(); setMenu(null); }} onContextMenu={(e) => { e.preventDefault(); e.stopPropagation(); setMenu(null); }} />
          <div className="menu" role="menu" style={{ left: menu.x, top: menu.y }} onClick={(e) => e.stopPropagation()}>
            <button type="button" data-testid="layer-add" onClick={close(() => structure('structure.addLayer'))}>Nueva capa</button>
            <button type="button" data-testid="layer-insert" onClick={close(() => structure('structure.insertLayer', { layer: layer + 1 }))}>Insertar encima</button>
            <button type="button" onClick={close(() => structure('structure.insertLayer', { layer }))}>Insertar debajo</button>
            <button type="button" data-testid="layer-duplicate" onClick={close(() => structure('structure.duplicateLayer', { layer }))}>Duplicar</button>
            <button type="button" disabled={layer >= layers} data-testid="layer-up" onClick={close(() => structure('structure.moveLayer', { layer, to: layer + 1 }))}>Subir</button>
            <button type="button" disabled={layer <= 1} onClick={close(() => structure('structure.moveLayer', { layer, to: layer - 1 }))}>Bajar</button>
            <button type="button" onClick={close(async () => {
              if (await window.vjHost.confirm('¿Vaciar esta capa?', 'Se quitan sus clips del deck actual. Se puede deshacer.')) structure('structure.clearLayer', { layer });
            })}>Vaciar clips</button>
            <button type="button" disabled={layers <= 1} data-testid="layer-remove" onClick={close(async () => {
              if (await window.vjHost.confirm('¿Quitar esta capa?', 'Se elimina con sus clips en todos los decks. Se puede deshacer.')) structure('structure.removeLayer', { layer });
            })}>Quitar</button>
            <hr />
            <button type="button" onClick={close(() => setRenaming(true))}>Renombrar</button>
            <button type="button" onClick={close(() => void engine.trigger(`${path}/eject`))}>Expulsar clip</button>
            <button type="button" onClick={close(() => setLock(!lock))}>{lock ? 'Desbloquear contenido' : 'Bloquear contenido'}</button>
            <button type="button" onClick={close(() => setIgnore(!ignore))}>{ignore ? 'Atender columnas' : 'Ignorar columnas'}</button>
            {masks.map((m) => <button key={m} type="button" onClick={close(() => setMask(m))}>{m === mask ? '● ' : ''}Máscara: {m}</button>)}
            <hr />
            {Array.from({ length: groups + 1 }, (_, g) => <button key={g} type="button" onClick={close(() => setGroup(g))}>{g === group ? '● ' : ''}{g ? `Grupo ${g}` : 'Sin grupo'}</button>)}
          </div>
        </>
      )}
    </div>
  );
}

// Lo que suena ahora en la capa, aunque el clip esté en otra columna.
function LiveCell({ layer }: { layer: number }) {
  const [folded] = useParam(`${layerPath(layer)}/folded`, false);
  const [active] = useParam(`${layerPath(layer)}/activeclip`, 0);
  const path = clipPath(layer, active);
  const [thumbnail] = useParam(`${path}/thumbnail`, '');
  const [name] = useParam(`${path}/name`, '');
  const [file] = useParam(`${path}/path`, '');
  const [duration, setDuration] = useParam(`${layerPath(layer)}/transition/duration`, 0);
  return (
    <div className={folded ? 'live-cell folded' : 'live-cell'} title="Clip en reproducción en esta capa" style={{ backgroundImage: active > 0 && thumbnail ? `url(${thumbnail})` : undefined }}>
      <label className="transition" title={`Transición entre clips: ${duration.toFixed(1)} s`}>
        <input type="range" min={0} max={10} step={0.1} value={duration} aria-label="Duración de transición" onChange={(e) => setDuration(Number(e.target.value))} />
      </label>
      {active > 0 && file && <span className="cell-name">{clipLabel(name, file)}</span>}
    </div>
  );
}

function ColumnHeader({ column }: { column: number }) {
  const engine = useEngine();
  const [current] = useParam('/composition/columns/current', 0);
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [columns] = useParam('/composition/structure/columns', 0);
  const structure = (cmd: string, args: Record<string, unknown> = {}) => { setMenu(null); void engine.run(cmd, args).catch((e: Error) => void window.vjHost.confirm('No se puede', e.message)); };
  return (
    <>
    {menu && (
      <>
        <div className="menu-veil" onClick={() => setMenu(null)} onContextMenu={(e) => { e.preventDefault(); setMenu(null); }} />
        <div className="menu" role="menu" style={{ left: menu.x, top: menu.y }}>
          <button type="button" data-testid="column-add" onClick={() => structure('structure.addColumn')}>Nueva columna</button>
          <button type="button" onClick={() => structure('structure.insertColumn', { column })}>Insertar a la izquierda</button>
          <button type="button" onClick={() => structure('structure.insertColumn', { column: column + 1 })}>Insertar a la derecha</button>
          <button type="button" onClick={() => structure('structure.duplicateColumn', { column })}>Duplicar</button>
          <button type="button" disabled={column <= 1} onClick={() => structure('structure.moveColumn', { column, to: column - 1 })}>Mover a la izquierda</button>
          <button type="button" disabled={column >= columns} onClick={() => structure('structure.moveColumn', { column, to: column + 1 })}>Mover a la derecha</button>
          <button type="button" disabled={columns <= 1} onClick={async () => {
            setMenu(null);
            if (await window.vjHost.confirm('¿Quitar esta columna?', 'Se elimina con sus clips en todos los decks. Se puede deshacer.')) structure('structure.removeColumn', { column });
          }}>Quitar</button>
        </div>
      </>
    )}
    <button type="button" className={current === column ? 'column on' : 'column'} data-column={column}
      onContextMenu={(e) => { e.preventDefault(); setMenu({ x: e.clientX, y: e.clientY }); }}
      title="Dispara todos los clips de la columna" onClick={() => void engine.trigger(`${columnPath(column)}/connect`)}>Columna {column}</button>
    </>
  );
}

export function MatrixPanel() {
  const engine = useEngine();
  const [layers] = useParam('/composition/structure/layers', 0);
  const [columns] = useParam('/composition/structure/columns', 0);
  const selection = useSelection();
  const [master, setMaster] = useParam('/composition/master', 1);
  const [compBypass, setCompBypass] = useParam('/composition/bypass', false);
  const heldKeys = useRef(new Map<string, string>());

  // Las teclas 1 a 9 disparan en la capa activa, con el mismo efecto que un clic.
  useEffect(() => {
    const isField = (t: EventTarget | null) => t instanceof HTMLInputElement || t instanceof HTMLSelectElement;
    const down = (e: KeyboardEvent) => {
      if (e.repeat || isField(e.target) || e.metaKey || e.ctrlKey) return;
      // Los atajos asignados mandan sobre los de serie.
      void engine.run('shortcuts.key', { key: e.key, down: true }).catch(() => {});
      if (!/^[1-9]$/.test(e.key)) return;
      const path = clipPath(selection.layer, Number(e.key));
      if (!engine.params.has(`${path}/connect`)) return;
      heldKeys.current.set(e.key, path);
      void engine.trigger(`${path}/connect`);
    };
    const up = (e: KeyboardEvent) => {
      if (!isField(e.target)) void engine.run('shortcuts.key', { key: e.key, down: false }).catch(() => {});
      const path = heldKeys.current.get(e.key);
      if (!path) return;
      heldKeys.current.delete(e.key);
      void engine.trigger(`${path}/release`);
    };
    window.addEventListener('keydown', down);
    window.addEventListener('keyup', up);
    return () => {
      window.removeEventListener('keydown', down);
      window.removeEventListener('keyup', up);
    };
  }, [engine, selection.layer]);

  const columnList = Array.from({ length: columns }, (_, i) => i + 1);
  // La capa 1 es la de abajo: se pinta en la última fila.
  const layerList = Array.from({ length: layers }, (_, i) => layers - i);
  return (
    <div className="matrix" style={{ gridTemplateColumns: `232px 92px repeat(${columns}, 104px)` }}>
      <div className="matrix-corner">
        <b>Composición</b>
        <button type="button" data-testid="eject-all" title="Expulsa todos los clips" onClick={() => void engine.trigger('/composition/eject')}>✕</button>
        <Toggle label="B" title="Bypass de toda la salida" on={compBypass} onChange={setCompBypass} />
      </div>
      <label className="comp-master" title="Máster de la composición"><span>M</span><input type="range" min={0} max={1} step={0.01} value={master} aria-label="Máster de la composición" onChange={(e) => setMaster(Number(e.target.value))} /></label>
      {columnList.map((c) => <ColumnHeader key={c} column={c} />)}
      {layerList.flatMap((l) => [
        <LayerHeader key={`h${l}`} layer={l} />,
        <LiveCell key={`v${l}`} layer={l} />,
        ...columnList.map((c) => <Cell key={`${l}-${c}`} layer={l} column={c} columns={columns} />),
      ])}
    </div>
  );
}
