import { useEffect, useRef, useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import { clipPath } from './paths';

function Monitor({ id, testId, blank }: { id: number; testId: string; blank?: boolean }) {
  const engine = useEngine();
  const canvas = useRef<HTMLCanvasElement>(null);
  // Sin contenido no llegan fotogramas: hay que borrar el último.
  useEffect(() => {
    const c = canvas.current;
    if (blank && c) c.getContext('2d')?.clearRect(0, 0, c.width, c.height);
  }, [blank]);
  useEffect(() => engine.onMonitor((f) => {
    if (f.monitor !== id || !canvas.current) return;
    const c = canvas.current;
    if (c.width !== f.width) c.width = f.width;
    if (c.height !== f.height) c.height = f.height;
    const pixels = new Uint8ClampedArray(f.pixels.buffer as ArrayBuffer, f.pixels.byteOffset, f.pixels.length);
    c.getContext('2d')?.putImageData(new ImageData(pixels, f.width, f.height), 0, 0);
  }), [engine, id]);
  return <canvas ref={canvas} width={480} height={270} data-testid={testId} />;
}

export function MonitorsPanel() {
  const engine = useEngine();
  const [layer] = useParam('/preview/layer', 0);
  const [column] = useParam('/preview/column', 0);
  const [file] = useParam('/preview/file', '');
  const [fps] = useParam('/engine/fps', 0);
  const [message, setMessage] = useState('');
  const [width] = useParam('/composition/width', 1920);
  const [height] = useParam('/composition/height', 1080);
  const has = layer > 0 || Boolean(file);

  const snapshot = async () => {
    try {
      const r = await engine.run<{ path: string; column: number }>('output.snapshot');
      setMessage(r.column ? `Guardada e importada en Capa 1 · Clip ${r.column}` : 'Guardada en Recorded; no quedan celdas libres en la capa 1');
    } catch (e) {
      setMessage(String((e as Error).message));
    }
  };
  const copy = () => {
    const c = document.querySelector<HTMLCanvasElement>('[data-testid=monitor-output]');
    c?.toBlob((blob) => {
      if (blob) void navigator.clipboard.write([new ClipboardItem({ 'image/png': blob })]).then(() => setMessage('Imagen copiada'));
    });
  };

  return (
    <div className="monitors">
      <figure>
        <figcaption><span>Monitor de composición</span></figcaption>
        <div className="monitor-view">
          <Monitor id={0} testId="monitor-output" />
          {message && <p className="monitor-note" onClick={() => setMessage('')}>{message}</p>}
        </div>
        <footer>
          <span>{width} × {height} · {fps.toFixed(0)} fps</span>
          <span className="grow" />
          <button type="button" title="Copia la imagen de salida" onClick={copy}>Copiar</button>
          <button type="button" title="Guarda la imagen e impórtala como clip" onClick={() => void snapshot()}>Snapshot</button>
        </footer>
      </figure>
      <figure>
        <figcaption><span>Preview</span></figcaption>
        <div className="monitor-view checker">
          <Monitor id={1} testId="monitor-preview" blank={!has} />
          {!has && <p className="monitor-empty">Clic en el nombre de un clip</p>}
        </div>
        <footer>
          <span>{has ? `Capa ${layer} · Clip ${column}` : 'Previsualizar'}</span>
          <span className="grow" />
          {layer > 0 && <button type="button" data-testid="preview-send" title="Dispara este clip en la salida" onClick={() => void engine.trigger(`${clipPath(layer, column)}/connect`)}>▶ A salida</button>}
          <button type="button" disabled={!has} onClick={() => void engine.run('preview.stop')}>Cerrar</button>
        </footer>
      </figure>
    </div>
  );
}
