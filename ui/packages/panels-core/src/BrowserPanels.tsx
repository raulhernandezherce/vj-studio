import { useEffect, useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import type { DirEntry } from './host';
import './host';
import { clipPath, layerPath } from './paths';
import type { CatalogEntry } from './paths.extra';
import { useSelection } from './selection';

// Lista de efectos o fuentes: un clic lo aplica a la selección actual.
export function CatalogPanel({ kind }: { kind: 'effect' | 'source' }) {
  const engine = useEngine();
  const { layer, column } = useSelection();
  const [file] = useParam(`${clipPath(layer, column)}/path`, '');
  const [catalog, setCatalog] = useState<CatalogEntry[]>([]);
  const [search, setSearch] = useState('');
  const [message, setMessage] = useState('');
  useEffect(() => { void engine.run<CatalogEntry[]>('effects.catalog').then(setCatalog).catch(() => {}); }, [engine]);

  const needle = search.toLowerCase();
  const shown = catalog.filter((c) => Boolean(c.source) === (kind === 'source') && c.name.toLowerCase().includes(needle));
  const entity = file ? clipPath(layer, column) : layerPath(layer);
  const add = (c: CatalogEntry) => {
    const done = kind === 'source'
      ? engine.run('source.add', { layer, column, type: c.id })
      : engine.run('effect.add', { entity, type: c.id });
    void done.then(() => setMessage(`${c.name} añadido`), (e: Error) => setMessage(e.message));
  };
  return (
    <div className="media">
      <input type="search" placeholder="Buscar" aria-label="Buscar" value={search} onChange={(e) => setSearch(e.target.value)} />
      {message && <p className="hint" onClick={() => setMessage('')}>{message}</p>}
      <ul>
        {[...new Set(shown.map((c) => c.category))].map((category) => (
          <li key={category} className="catalog-group">
            <b>{category}</b>
            {shown.filter((c) => c.category === category).map((c) => (
              <button key={c.id} type="button" className="file" data-catalog={c.id} draggable
                onDragStart={(e) => e.dataTransfer.setData('application/x-vj-catalog', JSON.stringify({ kind, id: c.id }))}
                title={kind === 'source' ? `Arrástrala a una celda, o clic para cargarla en la seleccionada` : `Arrástralo a un clip, o clic para añadirlo al clip seleccionado`}
                onClick={() => add(c)}>{c.name}</button>
            ))}
          </li>
        ))}
      </ul>
    </div>
  );
}

// Composiciones guardadas en la carpeta del proyecto.
export function CompositionsPanel() {
  const engine = useEngine();
  const [documents] = useParam('/project/folder', '');
  const [entries, setEntries] = useState<DirEntry[]>([]);
  const folder = `${documents}/Compositions`;
  const refresh = () => { if (documents) void window.vjHost.listDir(folder).then(setEntries).catch(() => setEntries([])); };
  useEffect(refresh, [documents]);
  const shown = entries.filter((e) => e.name.endsWith('.vjcomp'));
  return (
    <div className="files">
      <div className="row">
        <button type="button" title="Volver a leer la carpeta" onClick={refresh}>Actualizar</button>
        <span className="files-dir" title={folder}>{folder}</span>
      </div>
      <ul>
        {shown.map((e) => (
          <li key={e.path}>
            <button type="button" className="file" title="Doble clic para abrir" onDoubleClick={() => void engine.run('project.open', { path: e.path })}>{e.name.replace(/\.vjcomp$/, '')}</button>
          </li>
        ))}
        {shown.length === 0 && <li className="hint">Sin composiciones en esta carpeta</li>}
      </ul>
    </div>
  );
}
