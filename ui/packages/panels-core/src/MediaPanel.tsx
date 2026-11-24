import { useCallback, useEffect, useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import './host';

interface Item {
  path: string;
  exists: boolean;
  uses: number;
}

export function MediaPanel() {
  const engine = useEngine();
  const [items, setItems] = useState<Item[]>([]);
  const [message, setMessage] = useState('');
  const [decks] = useParam('/project/decks', '[]');
  const refresh = useCallback(async () => setItems(await engine.run<Item[]>('media.list')), [engine]);
  useEffect(() => { void refresh().catch(() => {}); }, [refresh, decks]);

  const relocate = async (path: string) => {
    const [to] = await window.vjHost.pickMedia();
    if (!to) return;
    const r = await engine.run<{ fixed: number }>('media.relink', { from: path, to, all: true });
    setMessage(`${r.fixed} ${r.fixed === 1 ? 'archivo reenlazado' : 'archivos reenlazados'}. Guarda la composición para conservarlo.`);
    await refresh();
  };
  const replace = async (path: string) => {
    const [to] = await window.vjHost.pickMedia();
    if (!to) return;
    await engine.run('media.relink', { from: path, to, all: false });
    await refresh();
  };
  const collect = async () => {
    const dir = await window.vjHost.pickFolder();
    if (!dir) return;
    const r = await engine.run<{ copied: number }>('media.collect', { dir });
    setMessage(`${r.copied} archivos copiados a Media. Los originales siguen en su sitio.`);
  };

  const missing = items.filter((i) => !i.exists).length;
  return (
    <div className="media">
      <div className="row">
        <button type="button" onClick={() => void refresh()}>Actualizar</button>
        <button type="button" onClick={() => void collect()}>Recopilar medios…</button>
        <span className={missing ? 'hint bad-text' : 'hint'}>{items.length} archivos, {missing} sin encontrar</span>
      </div>
      {message && <p className="hint">{message}</p>}
      <ul>
        {items.map((i) => (
          <li key={i.path} className={i.exists ? '' : 'missing'}>
            <span title={i.path}>{i.path.split('/').pop()}</span>
            <small>{i.uses > 1 ? `×${i.uses}` : ''}</small>
            {i.exists
              ? <button type="button" onClick={() => void replace(i.path)}>Reemplazar</button>
              : <button type="button" onClick={() => void relocate(i.path)}>Localizar</button>}
          </li>
        ))}
      </ul>
    </div>
  );
}
