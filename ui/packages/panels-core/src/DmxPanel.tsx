import { useCallback, useEffect, useState } from 'react';
import { useConnection, useEngine, useParam } from '@vj/api-client';

interface Fixture { id: number; name: string; type: string; universe: number; channel: number; columns: number; rows: number; x: number; y: number; w: number; h: number; flipX: boolean; flipY: boolean; brightness: number; contrast: number }
interface Setup { format: string; version: number; fixtures: Fixture[]; warnings: string[]; channels: Record<string, number> }

export function DmxPanel() {
  const engine = useEngine();
  const connection = useConnection();
  const [setup, setSetup] = useState<Setup | null>(null);
  const [types, setTypes] = useState<{ name: string }[]>([]);
  const [message, setMessage] = useState('');
  const [enabled, setEnabled] = useParam('/dmx/enabled', false);
  const [target, setTarget] = useParam('/dmx/target', '255.255.255.255');
  const [status] = useParam('/dmx/status', 'apagado');
  const [newType, setNewType] = useState('');
  const [channels, setChannels] = useState('r g b');

  const refreshTypes = useCallback(() => void engine.run<{ name: string }[]>('dmx.types').then(setTypes).catch(() => {}), [engine]);
  useEffect(() => {
    if (connection !== 'connected') return;
    void engine.run<Setup>('dmx.get').then(setSetup).catch((e: Error) => setMessage(e.message));
    refreshTypes();
  }, [engine, connection, refreshTypes]);

  const put = (fixtures: Fixture[], auto = false) => void engine.run<Setup>('dmx.set', { setup: { format: 'vjstudio-dmx', version: 1, fixtures }, auto }).then(setSetup).catch((e: Error) => setMessage(e.message));
  if (!setup) return <p className="dim">Sin conexión con el motor.</p>;
  const edit = (id: number, patch: Partial<Fixture>) => put(setup.fixtures.map((f) => f.id === id ? { ...f, ...patch } : f));
  const add = () => {
    const id = setup.fixtures.reduce((m, f) => Math.max(m, f.id), 0) + 1;
    put([...setup.fixtures, { id, name: `Fixture ${id}`, type: 'RGB', universe: 1, channel: 1, columns: 16, rows: 1, x: 0, y: 0, w: 1, h: 1, flipX: false, flipY: false, brightness: 1, contrast: 1 }], true);
  };
  const num = (f: Fixture, key: 'universe' | 'channel' | 'columns' | 'rows' | 'x' | 'y' | 'w' | 'h' | 'brightness' | 'contrast', label: string, step = 1) => (
    <label key={key}>{label}<input type="number" step={step} value={f[key]} onChange={(e) => edit(f.id, { [key]: Number(e.target.value) })} /></label>
  );
  return (
    <div className="dmx" data-testid="dmx-panel">
      <button type="button" className={enabled ? 'toggle on' : 'toggle'} onClick={() => setEnabled(!enabled)}>{enabled ? 'Art-Net activo' : 'Activar Art-Net'}</button>
      <label>Destino (IP o difusión)<input value={target} onChange={(e) => setTarget(e.target.value)} /></label>
      <p className="dim" role="status">{status}{message && ` · ${message}`}</p>
      {setup.warnings.map((w) => <p key={w} className="warn">{w}</p>)}
      <button type="button" data-testid="dmx-add" onClick={add}>Añadir fixture</button>
      <button type="button" onClick={() => put(setup.fixtures, true)}>Reasignar canales</button>
      {setup.fixtures.map((f) => (
        <details key={f.id} data-fixture={f.id}>
          <summary>{f.name} · U{f.universe}.{f.channel} · {setup.channels[f.id]} canales</summary>
          <label>Nombre<input value={f.name} onChange={(e) => edit(f.id, { name: e.target.value })} /></label>
          <label>Tipo<select value={f.type} onChange={(e) => edit(f.id, { type: e.target.value })}>{types.map((t) => <option key={t.name}>{t.name}</option>)}</select></label>
          {num(f, 'universe', 'Universo')}{num(f, 'channel', 'Canal')}{num(f, 'columns', 'Píxeles por fila')}{num(f, 'rows', 'Filas')}
          {num(f, 'x', 'Zona X', 0.01)}{num(f, 'y', 'Zona Y', 0.01)}{num(f, 'w', 'Zona ancho', 0.01)}{num(f, 'h', 'Zona alto', 0.01)}
          {num(f, 'brightness', 'Brillo', 0.05)}{num(f, 'contrast', 'Contraste', 0.05)}
          <label><input type="checkbox" checked={f.flipX} onChange={(e) => edit(f.id, { flipX: e.target.checked })} /> Voltear horizontal</label>
          <button type="button" onClick={() => put(setup.fixtures.filter((q) => q.id !== f.id))}>Quitar</button>
        </details>
      ))}
      <h4>Tipo de fixture nuevo</h4>
      <label>Nombre<input value={newType} onChange={(e) => setNewType(e.target.value)} /></label>
      <label>Canales (r g b w l o un valor 0-255)<input value={channels} onChange={(e) => setChannels(e.target.value)} /></label>
      <button type="button" disabled={!newType.trim()} onClick={() => void engine.run('dmx.type.save', { name: newType.trim(), channels: channels.trim().split(/\s+/) }).then(() => { setNewType(''); refreshTypes(); }).catch((e: Error) => setMessage(e.message))}>Guardar tipo</button>
    </div>
  );
}
