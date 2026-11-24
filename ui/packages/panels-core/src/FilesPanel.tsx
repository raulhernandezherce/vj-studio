import { useCallback, useEffect, useState } from 'react';
import { useEngine } from '@vj/api-client';
import type { DirEntry } from './host';
import './host';

const FAVORITES = 'vj.files.favorites';
const MEDIA = /\.(mp4|mov|m4v|webm|avi|mkv|mpe?g|png|jpe?g|tiff?|bmp|gif|webp|heic|wav|mp3|aiff?|m4a|flac|aac)$/i;

function readFavorites(): string[] {
  try {
    return JSON.parse(localStorage.getItem(FAVORITES) ?? '[]');
  } catch {
    return [];
  }
}

export function FilesPanel() {
  const engine = useEngine();
  const [dir, setDir] = useState('');
  const [entries, setEntries] = useState<DirEntry[]>([]);
  const [favorites, setFavorites] = useState(readFavorites);

  const open = useCallback(async (path: string) => {
    setDir(path);
    setEntries(await window.vjHost.listDir(path));
  }, []);
  useEffect(() => { void window.vjHost.homeDir().then(open); }, [open]);

  const toggleFavorite = () => {
    const next = favorites.includes(dir) ? favorites.filter((f) => f !== dir) : [...favorites, dir];
    setFavorites(next);
    try { localStorage.setItem(FAVORITES, JSON.stringify(next)); } catch { /* sin almacenamiento: dura la sesión */ }
  };

  const shown = entries.filter((e) => e.directory || MEDIA.test(e.name));
  const parent = dir.slice(0, dir.lastIndexOf('/')) || '/';
  return (
    <div className="files">
      <div className="row">
        <button type="button" title="Carpeta superior" onClick={() => void open(parent)}>↑</button>
        <button type="button" title="Favorito" className={favorites.includes(dir) ? 'toggle on' : 'toggle'} onClick={toggleFavorite}>★</button>
        <span className="files-dir" title={dir}>{dir}</span>
      </div>
      {favorites.length > 0 && (
        <div className="row">{favorites.map((f) => <button key={f} type="button" title={f} onClick={() => void open(f)}>{f.split('/').pop() || '/'}</button>)}</div>
      )}
      <ul>
        {shown.map((e) => (
          <li key={e.path}>
            <button type="button" draggable={!e.directory} className={e.directory ? 'file dir' : 'file'}
              onDragStart={(ev) => ev.dataTransfer.setData('application/x-vj-files', JSON.stringify([e.path]))}
              onClick={() => { if (e.directory) void open(e.path); }}
              onDoubleClick={() => { if (!e.directory) void engine.run('preview.file', { path: e.path }); }}>
              {e.directory ? '▸ ' : ''}{e.name}
            </button>
          </li>
        ))}
        {shown.length === 0 && <li className="hint">Sin medios en esta carpeta</li>}
      </ul>
    </div>
  );
}
