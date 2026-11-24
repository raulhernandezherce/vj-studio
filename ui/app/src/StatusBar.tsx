import { useEffect, useState } from 'react';
import { useConnection, useParam } from '@vj/api-client';
import { LayoutBar } from './LayoutBar';

const ENGINE_TEXT: Record<string, string> = {
  starting: 'Arrancando motor…',
  restarting: 'Motor caído: reiniciando…',
  failed: 'El motor no arranca',
};

export function StatusBar() {
  const connection = useConnection();
  const [hostState, setHostState] = useState('starting');
  const [fps] = useParam('/engine/fps', 0);
  const [cpu] = useParam('/engine/cpu', 0);
  const [gpu] = useParam('/engine/gpu', -1);
  const [memory] = useParam('/engine/memory', 0);
  const [recovered] = useParam('/project/recovered', false);
  const [backend] = useParam('/engine/backend', 'none');
  const [version] = useParam('/engine/version', '');
  const [clock, setClock] = useState('');
  useEffect(() => {
    const tick = () => setClock(new Date().toLocaleTimeString('es', { hour: '2-digit', minute: '2-digit' }));
    tick();
    const id = setInterval(tick, 10000);
    return () => clearInterval(id);
  }, []);
  useEffect(() => window.vjHost.onEngineState(setHostState), []);
  useEffect(() => { document.body.dataset.engine = connection; }, [connection]);

  const live = connection === 'connected';
  const text = live ? 'Motor conectado' : (ENGINE_TEXT[hostState] ?? 'Conectando con el motor…');
  return (
    <footer className="status">
      <span className="logo">VJ<b>STUDIO</b> {live && version}</span>
      <span className={live ? 'dot on' : hostState === 'failed' ? 'dot bad' : 'dot'} />
      <span data-testid="engine-state">{text}</span>
      {live && <span data-testid="perf">{fps.toFixed(1)} fps · CPU {cpu.toFixed(0)} % · GPU {gpu < 0 ? '—' : `${gpu.toFixed(0)} %`} · {memory.toFixed(0)} MB</span>}
      {live && recovered && <span role="status">Proyecto recuperado tras un fallo: guárdalo para conservarlo</span>}
      {live && <span>{backend}</span>}
      <span className="grow" />
      <LayoutBar />
      <span>{clock}</span>
    </footer>
  );
}
