import { useState } from 'react';
import { useParam } from '@vj/api-client';
import { DashboardTab } from './DashboardTab';
import { EffectsTab } from './EffectsTab';
import { ParamForm } from './ParamForm';
import { ParametersPanel } from './ParametersPanel';
import { clipPath, fileName, groupPath, layerPath } from './paths';
import { useSelection } from './selection';

const CLIP_HIDDEN = ['connect', 'release', 'path', 'connected', 'clear', 'thumbnail', 'thumbnailcustom', 'covered', 'status', 'hasaudio'];

function ClipTab() {
  const { layer, column } = useSelection();
  const path = clipPath(layer, column);
  const [file] = useParam(`${path}/path`, '');
  return (
    <>
      <h3 className="inspect-title">Capa {layer} · Clip {column}{file ? ` · ${fileName(file)}` : ''}</h3>
      {file ? <ParamForm key={path} prefix={path} hidden={CLIP_HIDDEN} /> : <p className="hint">Celda vacía. Haz clic en ella para cargar un vídeo o una imagen.</p>}
    </>
  );
}

function GroupTab() {
  const { layer } = useSelection();
  const [group, setGroup] = useParam(`${layerPath(layer)}/group`, 0);
  const [groups] = useParam('/composition/structure/groups', 0);
  return (
    <>
      <label className="field-enum">
        <span>Capa {layer} en</span>
        <select value={group} data-testid="layer-group" onChange={(e) => setGroup(Number(e.target.value))}>
          <option value={0}>Sin grupo</option>
          {Array.from({ length: groups }, (_, i) => <option key={i + 1} value={i + 1}>Grupo {i + 1}</option>)}
        </select>
      </label>
      {group > 0 ? <ParamForm key={group} prefix={groupPath(group)} /> : <p className="hint">Asigna capas contiguas al mismo grupo para mezclarlas como una sola.</p>}
    </>
  );
}

function Tabs<T extends string>({ tabs, tab, setTab }: { tabs: readonly T[]; tab: T; setTab: (t: T) => void }) {
  return (
    <nav className="tabs" role="tablist">
      {tabs.map((t) => (
        <button key={t} type="button" role="tab" aria-selected={tab === t} className={tab === t ? 'on' : ''} onClick={() => setTab(t)}>{t}</button>
      ))}
    </nav>
  );
}

const MAIN_TABS = ['Composición', 'Capa', 'Grupo', 'Dashboard', 'Salida', 'Grabación', 'Parámetros'] as const;

// Propiedades de lo general: composición, capa seleccionada y salida.
export function CompositionInspector() {
  const [tab, setTab] = useState<(typeof MAIN_TABS)[number]>('Composición');
  const { layer, column } = useSelection();
  const [scope, setScope] = useState<'clip' | 'layer' | 'composition'>('composition');
  const entity = scope === 'clip' ? clipPath(layer, column) : scope === 'layer' ? layerPath(layer) : '/composition';
  return (
    <div className="inspector">
      <Tabs tabs={MAIN_TABS} tab={tab} setTab={setTab} />
      <div className="tab-body">
        {tab === 'Capa' && <><h3 className="inspect-title">Capa {layer}</h3><ParamForm key={layer} prefix={layerPath(layer)} hidden={['group']} /></>}
        {tab === 'Grupo' && <GroupTab />}
        {tab === 'Composición' && <ParamForm prefix="/composition" hidden={['tempo/beat', 'notes', 'tempo/tap', 'tempo/half', 'tempo/double', 'tempo/nudgeup', 'tempo/nudgedown', 'activelayer', 'selectedclip', 'deck', 'eject', 'structure/layers', 'structure/columns', 'structure/groups']} />}
        {tab === 'Dashboard' && (
          <>
            <label className="field-enum">
              <span>Perillas de</span>
              <select value={scope} onChange={(e) => setScope(e.target.value as typeof scope)}>
                <option value="clip">Clip seleccionado</option>
                <option value="layer">Capa seleccionada</option>
                <option value="composition">Composición</option>
              </select>
            </label>
            <DashboardTab key={entity} entity={entity} />
          </>
        )}
        {tab === 'Salida' && <ParamForm prefix="/output" />}
        {tab === 'Grabación' && <ParamForm prefix="/record" />}
        {tab === 'Parámetros' && <ParametersPanel />}
      </div>
    </div>
  );
}

const CLIP_TABS = ['Clip', 'Efectos'] as const;

export function ClipInspector() {
  const [tab, setTab] = useState<(typeof CLIP_TABS)[number]>('Clip');
  return (
    <div className="inspector">
      <Tabs tabs={CLIP_TABS} tab={tab} setTab={setTab} />
      <div className="tab-body">{tab === 'Clip' ? <ClipTab /> : <EffectsTab />}</div>
    </div>
  );
}
