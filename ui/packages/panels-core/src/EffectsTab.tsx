import { useCallback, useEffect, useState } from 'react';
import { type ParamInfo, useEngine, useParam } from '@vj/api-client';
import { Toggle } from './controls';
import './host';
import { Field } from './ParamForm';
import { clipPath, groupPath, layerPath } from './paths';
import type { CatalogEntry } from './paths.extra';
import { useSelection } from './selection';

interface Instance {
  id: number;
  type: string;
  title: string;
  prefix: string;
  parameters: ParamInfo[];
}

function EffectCard({ entity, effect, index, count }: { entity: string; effect: Instance; index: number; count: number }) {
  const engine = useEngine();
  const [bypass, setBypass] = useParam(`${effect.prefix}/bypass`, false);
  const [name] = useParam(`${effect.prefix}/name`, '');
  const [presets, setPresets] = useState<string[]>([]);
  const [saving, setSaving] = useState(false);
  const run = (cmd: string, args: Record<string, unknown> = {}) => engine.run(cmd, { entity, id: effect.id, ...args });
  const refreshPresets = useCallback(() => engine.run<string[]>('preset.list', { kind: effect.type }).then(setPresets), [engine, effect.type]);
  useEffect(() => { void refreshPresets(); }, [refreshPresets]);

  const name_ = (p: ParamInfo) => p.path.slice(effect.prefix.length + 1);
  const common = effect.parameters.filter((p) => ['opacity', 'blend'].includes(name_(p)));
  const own = effect.parameters.filter((p) => !['bypass', 'opacity', 'blend', 'name'].includes(name_(p)));
  return (
    <details className="effect" open data-effect={effect.type}>
      <summary>
        <b>{name || effect.title}</b>
        <span className="effect-tools" onClick={(e) => e.preventDefault()}>
          <Toggle label="B" title="Bypass" on={bypass} onChange={setBypass} />
          <button type="button" title="Subir" disabled={index === 0} data-effect-up onClick={() => void run('effect.move', { index: index - 1 })}>↑</button>
          <button type="button" title="Bajar" disabled={index === count - 1} onClick={() => void run('effect.move', { index: index + 1 })}>↓</button>
          <button type="button" title="Quitar" onClick={() => void run('effect.remove')}>✕</button>
        </span>
      </summary>
      {own.map((p) => <Field key={p.path} info={p} />)}
      {effect.type === 'lut' && (
        <button type="button" onClick={async () => {
          const file = await window.vjHost.pickFile('LUT .cube', ['cube']);
          if (file) void engine.set(`${effect.prefix}/file`, file, true);
        }}>Cargar LUT…</button>
      )}
      {common.map((p) => <Field key={p.path} info={p} />)}
      <div className="row">
        {saving ? (
          <input type="text" autoFocus placeholder="Nombre del preset" onBlur={() => setSaving(false)} onKeyDown={async (e) => {
            if (e.key === 'Escape') setSaving(false);
            if (e.key === 'Enter' && e.currentTarget.value.trim()) {
              await run('preset.save', { name: e.currentTarget.value.trim() });
              setSaving(false);
              void refreshPresets();
            }
          }} />
        ) : <button type="button" onClick={() => setSaving(true)}>Guardar preset</button>}
        {presets.length > 0 && (
          <select aria-label="Aplicar preset" value="" onChange={(e) => void run('preset.apply', { kind: effect.type, name: e.target.value })}>
            <option value="" disabled>Preset…</option>
            {presets.map((n) => <option key={n} value={n}>{n}</option>)}
          </select>
        )}
      </div>
    </details>
  );
}

export function EffectsTab() {
  const engine = useEngine();
  const { layer, column } = useSelection();
  const [group] = useParam(`${layerPath(layer)}/group`, 0);
  const [scope, setScope] = useState<'clip' | 'layer' | 'group' | 'composition'>('layer');
  const [catalog, setCatalog] = useState<CatalogEntry[]>([]);
  const [chain, setChain] = useState<Instance[]>([]);
  const [message, setMessage] = useState('');
  const entity = scope === 'clip' ? clipPath(layer, column)
    : scope === 'layer' ? layerPath(layer)
    : scope === 'group' && group > 0 ? groupPath(group)
    : '/composition';

  const refresh = useCallback(async () => {
    const list = await engine.run<Instance[]>('effects.list', { entity });
    for (const e of list) engine.mergeParams(e.parameters);
    setChain(list);
  }, [engine, entity]);
  useEffect(() => {
    void refresh().catch(() => {});
    return engine.onEvent((topic, data) => {
      if (topic === 'effects.changed' && (data as { entity: string }).entity === entity) void refresh().catch(() => {});
      if (topic === 'effects.error') setMessage((data as { message: string }).message);
    });
  }, [engine, entity, refresh]);
  useEffect(() => { void engine.run<CatalogEntry[]>('effects.catalog').then(setCatalog).catch(() => {}); }, [engine]);

  const categories = [...new Set(catalog.map((c) => c.category))];
  const safe = (fn: () => Promise<unknown>) => void fn().catch((e: Error) => setMessage(e.message));
  return (
    <div className="effects">
      <label className="field-enum">
        <span>Aplicar a</span>
        <select value={scope} data-testid="effect-scope" onChange={(e) => setScope(e.target.value as typeof scope)}>
          <option value="clip">Clip seleccionado</option>
          <option value="layer">Capa {layer}</option>
          <option value="group" disabled={group === 0}>{group > 0 ? `Grupo ${group}` : 'Grupo (ninguno)'}</option>
          <option value="composition">Composición</option>
        </select>
      </label>
      <label className="field-enum">
        <span>Añadir</span>
        <select value="" data-testid="effect-add" onChange={(e) => safe(() => engine.run('effect.add', { entity, type: e.target.value }))}>
          <option value="" disabled>Efecto…</option>
          {categories.map((cat) => (
            <optgroup key={cat} label={cat}>
              {catalog.filter((c) => c.category === cat).map((c) => <option key={c.id} value={c.id}>{c.name}</option>)}
            </optgroup>
          ))}
        </select>
      </label>
      <div className="row">
        <button type="button" disabled={!chain.length} onClick={() => safe(() => engine.run('effects.copy', { entity }))}>Copiar efectos</button>
        <button type="button" onClick={() => safe(() => engine.run('effects.paste', { entity }))}>Pegar efectos</button>
        <button type="button" onClick={() => safe(async () => {
          const errors = await engine.run<{ file: string; error: string }[]>('effects.rescan');
          setCatalog(await engine.run<CatalogEntry[]>('effects.catalog'));
          setMessage(errors.length ? errors.map((x) => `${x.file}: ${x.error}`).join(' · ') : 'Carpeta Extra Effects leída');
        })}>Releer ISF</button>
      </div>
      {message && <p className="hint" onClick={() => setMessage('')}>{message}</p>}
      {chain.length === 0 && <p className="hint">Sin efectos. Se aplican de arriba abajo, cada uno sobre el resultado del anterior.</p>}
      {chain.map((e, i) => <EffectCard key={e.id} entity={entity} effect={e} index={i} count={chain.length} />)}
    </div>
  );
}
