import { useEngine, useParam } from '@vj/api-client';
import { Slider, Toggle } from './controls';
import './host';

export function CompositionBar() {
  const engine = useEngine();
  const [master, setMaster] = useParam('/composition/master', 1);
  const [speed, setSpeed] = useParam('/composition/speed', 1);
  const [bpm, setBpm] = useParam('/composition/tempo/bpm', 120);
  const [beat] = useParam('/composition/tempo/beat', 0);
  const [paused, setPaused] = useParam('/composition/tempo/pause', false);
  const [disabled, setDisabled] = useParam('/output/disabled', false);
  const [metronome, setMetronome] = useParam('/composition/tempo/metronome', false);
  const [left] = useParam('/audio/level/left', 0);
  const [right] = useParam('/audio/level/right', 0);
  const [recording, setRecording] = useParam('/record/active', false);
  const [recordStatus] = useParam('/record/status', 'parado');
  const reset = (p: string) => () => void engine.reset(p);
  const nudge = (path: string) => ({
    onPointerDown: () => void engine.set(path, true),
    onPointerUp: () => void engine.set(path, false),
    onPointerLeave: () => void engine.set(path, false),
  });
  return (
    <div className="bar">
      <div className="bar-tempo">
        <label>BPM <input type="number" min={20} max={500} step={0.1} value={bpm} data-testid="bpm" onChange={(e) => setBpm(Number(e.target.value))} /></label>
        <button type="button" title="Bajar 1 BPM" onClick={() => setBpm(bpm - 1)}>−</button>
        <button type="button" title="Subir 1 BPM" onClick={() => setBpm(bpm + 1)}>+</button>
        <button type="button" title="Nudge: frena mientras se pulsa" {...nudge('/composition/tempo/nudgedown')}>⇤</button>
        <button type="button" title="Nudge: acelera mientras se pulsa" {...nudge('/composition/tempo/nudgeup')}>⇥</button>
        <button type="button" title="Mitad de tempo" onClick={() => void engine.trigger('/composition/tempo/half')}>÷2</button>
        <button type="button" title="Doble de tempo" onClick={() => void engine.trigger('/composition/tempo/double')}>×2</button>
        <button type="button" data-testid="tap" title="Pulsa al ritmo para marcar el tempo" onClick={() => void engine.trigger('/composition/tempo/tap')}>TAP</button>
        <button type="button" title="Vuelve al primer tiempo del compás" onClick={() => void engine.trigger('/composition/tempo/resync')}>RESYNC</button>
        <Toggle label="PAUSA" title="Pausa el tiempo" on={paused} onChange={setPaused} />
        <Toggle label="♪" title="Metrónomo" on={metronome} onChange={setMetronome} />
        <button type="button" title="Deshacer" onClick={() => void engine.run('history.undo').catch(() => {})}>↶</button>
        <button type="button" title="Rehacer" onClick={() => void engine.run('history.redo').catch(() => {})}>↷</button>
        <span className="beats">{[0, 1, 2, 3].map((i) => <i key={i} className={Math.floor(beat) % 4 === i ? 'on' : ''} />)}</span>
      </div>
      <Slider label="Maestro" testId="master" value={master} min={0} max={1} onChange={setMaster} onReset={reset('/composition/master')} />
      <Slider label="Velocidad" testId="speed" value={speed} min={0} max={10} onChange={setSpeed} onReset={reset('/composition/speed')} format={(v) => `${v.toFixed(2)}×`} />
      <div className="meter" title="Nivel de audio" data-testid="meter"><i style={{ transform: `scaleX(${Math.min(1, left)})` }} /><i style={{ transform: `scaleX(${Math.min(1, right)})` }} /></div>
      <div className="bar-actions">
        <button type="button" data-testid="open-advanced" title="Configurar pantallas y escenarios" onClick={() => void window.vjHost.openAdvanced()}>Pantallas</button>
        <button type="button" data-testid="open-nodes" title="Editor de nodos" onClick={() => void window.vjHost.openNodes()}>Nodos</button>
        <Toggle label="Salida" title="Cmd+Mayús+D la apaga en cualquier momento" on={!disabled} onChange={(v) => setDisabled(!v)} testId="output-toggle" />
        <button type="button" data-testid="record" className={recording ? 'rec on' : 'rec'} title={`Grabación: ${recordStatus}`} onClick={() => setRecording(!recording)}>{recording ? '■ PARAR' : '● GRABAR'}</button>
      </div>
    </div>
  );
}
