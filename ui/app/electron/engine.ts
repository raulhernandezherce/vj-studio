import { type ChildProcess, spawn } from 'node:child_process';
import { EventEmitter } from 'node:events';
import { existsSync } from 'node:fs';
import { join } from 'node:path';

export type EngineState = 'starting' | 'running' | 'restarting' | 'failed';

// Las pruebas usan otro puerto para no chocar con un programa abierto.
const DEFAULT_PORT = Number(process.env.VJ_ENGINE_PORT) || 7777;
const MAX_RESTARTS = 5;
const RESTART_WINDOW_MS = 30_000;
const READY_TIMEOUT_MS = 15_000;

export function enginePath(isPackaged: boolean, appPath: string): string {
  if (process.env.VJ_ENGINE_PATH) return process.env.VJ_ENGINE_PATH;
  const exe = process.platform === 'win32' ? 'vjengine.exe' : 'vjengine';
  if (isPackaged) return join(process.resourcesPath, 'engine', exe);
  return join(appPath, '..', '..', 'build', 'engine', 'app', exe);
}

async function healthy(port: number, version: string): Promise<boolean> {
  try {
    const r = await fetch(`http://127.0.0.1:${port}/api/v1/health`, { signal: AbortSignal.timeout(500) });
    const body = (await r.json()) as { app?: string; version?: string };
    return body.app === 'vjstudio' && body.version === version;
  } catch {
    return false;
  }
}

// Arranca el motor y lo vigila. Si la interfaz muere, el motor sigue emitiendo;
// al abrirla de nuevo se reengancha al que ya corre en el puerto conocido.
export class EngineSupervisor extends EventEmitter {
  state: EngineState = 'starting';
  port = 0;
  adopted = false;
  private child: ChildProcess | null = null;
  private restarts: number[] = [];
  private quitting = false;

  constructor(private exe: string, private version: string) {
    super();
  }

  private setState(s: EngineState) {
    this.state = s;
    this.emit('state', s);
  }

  async start() {
    if (await healthy(DEFAULT_PORT, this.version)) {
      this.port = DEFAULT_PORT;
      this.adopted = true;
      this.setState('running');
      return;
    }
    await this.launch();
  }

  private async launch() {
    if (!existsSync(this.exe)) {
      console.error(`No se encuentra el motor: ${this.exe}`);
      this.setState('failed');
      return;
    }
    // Si el puerto conocido está ocupado por otra cosa, el sistema elige uno libre.
    const busy = await fetch(`http://127.0.0.1:${DEFAULT_PORT}/`, { signal: AbortSignal.timeout(300) }).then(() => true, (e) => e?.cause?.code !== 'ECONNREFUSED');
    const port = busy ? 0 : DEFAULT_PORT;
    const child = spawn(this.exe, ['--agent', '--port', String(port)], { windowsHide: true, stdio: ['ignore', 'pipe', 'inherit'] });
    this.child = child;
    let out = '';
    const timer = setTimeout(() => child.kill('SIGKILL'), READY_TIMEOUT_MS);
    child.stdout?.on('data', (d) => {
      out += d;
      const m = out.match(/VJENGINE_READY port=(\d+)/);
      if (m) {
        clearTimeout(timer);
        this.port = Number(m[1]);
        this.setState('running');
        out = '';
      }
    });
    child.on('exit', () => {
      clearTimeout(timer);
      if (this.child !== child) return;
      this.child = null;
      if (!this.quitting) this.onCrash();
    });
  }

  private onCrash() {
    const now = Date.now();
    this.restarts = this.restarts.filter((t) => now - t < RESTART_WINDOW_MS);
    if (this.restarts.length >= MAX_RESTARTS) {
      this.setState('failed');
      return;
    }
    this.restarts.push(now);
    this.setState('restarting');
    setTimeout(() => void this.launch(), 500);
  }

  // Solo para la prueba de recuperación.
  crashForTest() {
    this.child?.kill('SIGKILL');
  }

  async stop() {
    this.quitting = true;
    if (this.child) {
      const child = this.child;
      const gone = new Promise((res) => child.once('exit', res));
      child.kill('SIGTERM');
      await Promise.race([gone, new Promise((res) => setTimeout(res, 3000))]);
      if (child.exitCode === null) child.kill('SIGKILL');
      return;
    }
    if (this.port) {
      // Motor reenganchado: no es hijo nuestro, se le pide cerrar por la API.
      await new Promise<void>((res) => {
        const ws = new WebSocket(`ws://127.0.0.1:${this.port}/api/v1`);
        const done = () => { ws.close(); res(); };
        ws.onopen = () => { ws.send(JSON.stringify({ id: 1, action: 'trigger', parameter: '/engine/quit' })); setTimeout(done, 100); };
        ws.onerror = done;
      });
    }
  }
}
