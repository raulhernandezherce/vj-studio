import { useEffect, useRef } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import { ParamForm } from './ParamForm';

interface Device {
  id: string;
  name: string;
  inputs: number;
  outputs: number;
}

function parse(text: string): Device[] {
  try {
    const list = JSON.parse(text);
    return Array.isArray(list) ? list : [];
  } catch {
    return [];
  }
}

function Spectrum() {
  const engine = useEngine();
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    let alive = true;
    const draw = async () => {
      try {
        const s = await engine.run<number[]>('audio.spectrum', { source: 'composition' });
        const c = canvas.current;
        const g = c?.getContext('2d');
        if (alive && c && g) {
          c.width = c.clientWidth;
          c.height = c.clientHeight;
          g.clearRect(0, 0, c.width, c.height);
          g.fillStyle = '#2ec4b6';
          const w = c.width / s.length;
          s.forEach((v, i) => { const h = Math.min(1, v * 2) * c.height; g.fillRect(i * w, c.height - h, Math.max(1, w - 1), h); });
        }
      } catch { /* sin motor: se reintenta */ }
      if (alive) setTimeout(() => void draw(), 80);
    };
    void draw();
    return () => { alive = false; };
  }, [engine]);
  return <canvas ref={canvas} className="spectrum" data-testid="spectrum" />;
}

export function AudioPanel() {
  const [devices] = useParam('/audio/devices', '[]');
  const [status] = useParam('/audio/status', 'parado');
  const [output, setOutput] = useParam('/preferences/audio/output', '');
  const [input, setInput] = useParam('/preferences/audio/input', '');
  const list = parse(devices);
  const hidden = ['audio/output', 'audio/input', 'bpm/nudgerange'];
  return (
    <div>
      <p className="hint">Estado: <span data-testid="audio-status">{status}</span></p>
      <Spectrum />
      <label className="field-enum">
        <span>Salida</span>
        <select value={output} onChange={(e) => setOutput(e.target.value)}>
          <option value="">Por defecto del sistema</option>
          {list.filter((d) => d.outputs > 0).map((d) => <option key={d.id} value={d.id}>{d.name}</option>)}
        </select>
      </label>
      <label className="field-enum">
        <span>Entrada</span>
        <select value={input} onChange={(e) => setInput(e.target.value)}>
          <option value="">Ninguna</option>
          {list.filter((d) => d.inputs > 0).map((d) => <option key={d.id} value={d.id}>{d.name}</option>)}
        </select>
      </label>
      <ParamForm prefix="/preferences/audio" hidden={['output', 'input', ...hidden]} />
    </div>
  );
}
