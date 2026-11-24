import { useCallback, useEffect, useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import { ParamForm } from './ParamForm';
import './host';

interface Shortcut {
  id: number;
  key: string;
  target: string;
  mode?: string;
}

const MODES = ['absolute', 'toggle', 'piano', 'range', 'value', 'velocity', 'relative', 'trigger', 'next', 'previous', 'random'];

export function ControlPanel() {
  const engine = useEngine();
  const [list, setList] = useState<Shortcut[]>([]);
  const [presets, setPresets] = useState<string[]>([]);
  const [log, setLog] = useState<string[]>([]);
  const [preset] = useParam('/shortcuts/preset', 'Por defecto');
  const [learning] = useParam('/shortcuts/learning', false);
  const [name, setName] = useState('');
  const refresh = useCallback(() => {
    void engine.run<Shortcut[]>('shortcuts.list').then(setList).catch(() => {});
    void engine.run<string[]>('shortcuts.preset.list').then(setPresets).catch(() => {});
  }, [engine]);
  useEffect(refresh, [refresh, learning, preset]);
  useEffect(() => {
    const t = setInterval(() => {
      void Promise.all([engine.run<string[]>('midi.monitor'), engine.run<string[]>('osc.log')]).then(([m, o]) => setLog([...m, ...o].slice(-12))).catch(() => {});
    }, 700);
    return () => clearInterval(t);
  }, [engine]);
  return (
    <div className="control">
      <p className="hint" data-testid="learn-state">{learning ? 'Aprendiendo: mueve o pulsa un control…' : 'Pulsa ⌁ junto a un parámetro y mueve el control físico o pulsa una tecla.'}</p>
      {learning && <button type="button" onClick={() => void engine.run('shortcuts.cancel')}>Cancelar</button>}
      <div className="row">
        <select aria-label="Preset de atajos" value={preset} onChange={(e) => void engine.run('shortcuts.preset.load', { name: e.target.value })}>
          {[...new Set([preset, ...presets])].map((p) => <option key={p}>{p}</option>)}
        </select>
        <input type="text" placeholder="Nuevo preset" value={name} onChange={(e) => setName(e.target.value)} />
        <button type="button" disabled={!name} onClick={() => { void engine.run('shortcuts.preset.new', { name }); setName(''); }}>Crear</button>
      </div>
      {list.map((s) => (
        <div className="shortcut" key={s.id} data-shortcut={s.key}>
          <code title={`${s.key} → ${s.target}`}>{s.key} → {s.target.split('/').slice(-2).join('/')}</code>
          <select aria-label="Modo" value={s.mode ?? 'absolute'} onChange={(e) => { void engine.run('shortcuts.update', { id: s.id, fields: { mode: e.target.value } }).then(refresh); }}>
            {MODES.map((m) => <option key={m}>{m}</option>)}
          </select>
          <button type="button" title="Quitar" onClick={() => { void engine.run('shortcuts.remove', { id: s.id }).then(refresh); }}>✕</button>
        </div>
      ))}
      <details open={false}>
        <summary>OSC</summary>
        <ParamForm prefix="/preferences/osc" />
      </details>
      <details>
        <summary>MIDI</summary>
        <ParamForm prefix="/preferences/midi" />
      </details>
      <details>
        <summary>Art-Net</summary>
        <ParamForm prefix="/preferences/dmx" />
      </details>
      <div className="log" data-testid="control-log">{log.join('\n')}</div>
    </div>
  );
}
