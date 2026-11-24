import { useEffect, useState } from 'react';
import { type ParamInfo, useEngine, useParamList } from '@vj/api-client';

const LIMIT = 200;

function show(p: ParamInfo) {
  if (p.type === 'trigger') return 'disparador';
  const v = p.value;
  if (typeof v === 'number') {
    const n = p.type === 'int' ? String(v) : v.toFixed(v >= 100 ? 0 : 2);
    return p.unit ? `${n} ${p.unit}` : n;
  }
  if (typeof v === 'boolean') return v ? 'sí' : 'no';
  return v ? String(v) : '—';
}

// Lista en vivo con buscador: lee del motor, no de una copia en la interfaz.
export function ParametersPanel() {
  const engine = useEngine();
  const params = useParamList();
  const [query, setQuery] = useState('');
  const [, bump] = useState(0);
  useEffect(() => engine.onParam(() => bump((n) => n + 1)), [engine]);

  const q = query.trim().toLowerCase();
  const matches = [...params.values()].filter((p) => !q || p.path.toLowerCase().includes(q) || p.label.toLowerCase().includes(q));
  matches.sort((a, b) => a.path.localeCompare(b.path, undefined, { numeric: true }));
  return (
    <div className="params" data-testid="params">
      <input type="search" placeholder="Buscar parámetro" value={query} onChange={(e) => setQuery(e.target.value)} />
      <p className="hint">{matches.length} de {params.size}{matches.length > LIMIT ? `, se muestran ${LIMIT}` : ''}</p>
      {matches.slice(0, LIMIT).map((p) => (
        <div className="param" key={p.path} title={p.path} data-path={p.path}>
          <code>{p.path}</code>
          <span className={p.readOnly ? 'val ro' : 'val'}>{show(p)}</span>
        </div>
      ))}
    </div>
  );
}
