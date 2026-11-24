import { useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import { Slider } from './controls';
import { parseLinks } from './paths.extra';

function Knob({ entity, n }: { entity: string; n: number }) {
  const engine = useEngine();
  const path = `${entity}/dashboard/k${n}`;
  const [value, setValue] = useParam(path, 0);
  const [text] = useParam(`${path}/links`, '[]');
  const [over, setOver] = useState(false);
  const links = parseLinks(text);
  const label = links[0] ? (engine.params.get(links[0].param.startsWith('/') ? links[0].param : `${entity}/${links[0].param}`)?.label ?? links[0].param) : `Perilla ${n}`;
  return (
    <div className={over ? 'knob drop' : 'knob'}
      onDragOver={(e) => { e.preventDefault(); setOver(true); }}
      onDragLeave={() => setOver(false)}
      onDrop={(e) => {
        e.preventDefault();
        setOver(false);
        const param = e.dataTransfer.getData('application/x-vj-param');
        if (param) void engine.run('dashboard.assign', { knob: path, param });
      }}>
      <Slider label={label} value={value} min={0} max={1} onChange={setValue} />
      {links.map((l) => (
        <button key={l.param} type="button" className="link" title="Quitar enlace"
          onClick={() => void engine.run('dashboard.unassign', { knob: path, param: l.param })}>{l.param} ✕</button>
      ))}
    </div>
  );
}

export function DashboardTab({ entity }: { entity: string }) {
  return (
    <div className="dashboard">
      <p className="hint">Arrastra el nombre de un parámetro a una perilla. Varios parámetros pueden compartir perilla.</p>
      {Array.from({ length: 8 }, (_, i) => <Knob key={i} entity={entity} n={i + 1} />)}
    </div>
  );
}
