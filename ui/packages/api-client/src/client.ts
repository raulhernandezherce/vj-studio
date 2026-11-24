import type { ParamType } from './generated';

export type ParamValue = boolean | number | string;

export interface ParamInfo {
  path: string;
  type: ParamType;
  readOnly: boolean;
  label: string;
  group: string;
  unit: string;
  min: number;
  max: number;
  milestone: string;
  options?: string[];
  value?: ParamValue;
}

export interface MonitorFrame {
  monitor: number;
  width: number;
  height: number;
  pixels: Uint8ClampedArray;
}

export type ConnectionState = 'connecting' | 'connected' | 'disconnected';

interface Response {
  type: 'response';
  id?: number;
  ok: boolean;
  error?: string;
  options?: string[];
  value?: ParamValue;
  parameters?: ParamInfo[];
  result?: unknown;
}

type Message =
  | Response
  | { type: 'update'; parameter: string; value: ParamValue }
  | { type: 'trigger'; parameter: string }
  | { type: 'event'; topic: string; data: unknown };

interface SocketLike {
  onopen: ((e: unknown) => void) | null;
  onclose: ((e: unknown) => void) | null;
  onerror: ((e: unknown) => void) | null;
  onmessage: ((e: { data: unknown }) => void) | null;
  binaryType?: string;
  send(data: string): void;
  close(): void;
}

export interface ClientOptions {
  url: () => Promise<string> | string;
  socket?: (url: string) => SocketLike;
  reconnectMs?: number;
}

// Conexión con el motor. Mantiene una copia local de los parámetros y se
// reconecta sola: si el motor se reinicia, la interfaz vuelve a sincronizarse.
export class EngineClient {
  private ws: SocketLike | null = null;
  private nextId = 1;
  private pending = new Map<number, { resolve: (r: Response) => void; reject: (e: Error) => void }>();
  private paramListeners = new Set<(path: string, value: ParamValue) => void>();
  private listListeners = new Set<() => void>();
  private stateListeners = new Set<(s: ConnectionState) => void>();
  private eventListeners = new Set<(topic: string, data: unknown) => void>();
  private monitorListeners = new Set<(frame: MonitorFrame) => void>();
  private retry: ReturnType<typeof setTimeout> | null = null;
  private stopped = false;

  state: ConnectionState = 'disconnected';
  params = new Map<string, ParamInfo>();

  constructor(private options: ClientOptions) {}

  async start() {
    this.stopped = false;
    this.setState('connecting');
    let url: string;
    try {
      url = await this.options.url();
    } catch {
      this.scheduleRetry();
      return;
    }
    if (this.stopped) return;
    const make = this.options.socket ?? ((u: string) => new WebSocket(u) as unknown as SocketLike);
    const ws = make(url);
    this.ws = ws;
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => void this.sync(ws);
    ws.onmessage = (e) => {
      if (typeof e.data === 'string') this.receive(e.data);
      else this.receiveFrame(e.data as ArrayBuffer);
    };
    ws.onerror = () => {};
    ws.onclose = () => {
      if (this.ws !== ws) return;
      this.ws = null;
      for (const p of this.pending.values()) p.reject(new Error('conexión perdida'));
      this.pending.clear();
      this.setState('disconnected');
      this.scheduleRetry();
    };
  }

  stop() {
    this.stopped = true;
    if (this.retry) clearTimeout(this.retry);
    this.retry = null;
    this.ws?.close();
    this.ws = null;
    this.setState('disconnected');
  }

  private scheduleRetry() {
    if (this.stopped || this.retry) return;
    this.retry = setTimeout(() => {
      this.retry = null;
      void this.start();
    }, this.options.reconnectMs ?? 1000);
  }

  private async sync(ws: SocketLike) {
    try {
      const list = await this.request({ action: 'list' });
      this.params = new Map((list.parameters ?? []).map((p) => [p.path, p]));
      await this.request({ action: 'subscribe', parameter: '*' });
      if (this.monitorListeners.size) await this.request({ action: 'monitor', value: true });
      if (this.ws !== ws) return;
      this.setState('connected');
      this.listListeners.forEach((fn) => fn());
    } catch {
      ws.close();
    }
  }

  private setState(s: ConnectionState) {
    if (s === this.state) return;
    this.state = s;
    this.stateListeners.forEach((fn) => fn(s));
  }

  private receive(text: string) {
    const m = JSON.parse(text) as Message;
    if (m.type === 'response') {
      const p = m.id !== undefined ? this.pending.get(m.id) : undefined;
      if (p && m.id !== undefined) {
        this.pending.delete(m.id);
        p.resolve(m);
      }
    } else if (m.type === 'update') {
      this.applyValue(m.parameter, m.value);
    } else if (m.type === 'event') {
      if (m.topic === 'structure.changed') void this.reloadParams().catch(() => {});
      this.eventListeners.forEach((fn) => fn(m.topic, m.data));
    }
  }

  private applyValue(path: string, value: ParamValue) {
    const p = this.params.get(path);
    if (p) p.value = value;
    this.paramListeners.forEach((fn) => fn(path, value));
  }

  private receiveFrame(data: ArrayBuffer) {
    const head = new DataView(data);
    const frame: MonitorFrame = {
      monitor: head.getUint8(0),
      width: head.getUint16(1),
      height: head.getUint16(3),
      pixels: new Uint8ClampedArray(data, 5),
    };
    this.monitorListeners.forEach((fn) => fn(frame));
  }

  private request(body: Record<string, unknown>): Promise<Response> {
    return new Promise((resolve, reject) => {
      if (!this.ws) {
        reject(new Error('sin conexión con el motor'));
        return;
      }
      const id = this.nextId++;
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ id, ...body }));
    });
  }

  private async send(body: Record<string, unknown>): Promise<Response> {
    const r = await this.request(body);
    if (!r.ok) throw new Error(r.error ?? 'error del motor');
    return r;
  }

  // history: el cambio lo hizo una persona y se puede deshacer.
  async set(parameter: string, value: ParamValue, history = false) {
    const r = await this.send({ action: 'set', parameter, value, ...(history ? { history: true } : {}) });
    if (r.value !== undefined) this.applyValue(parameter, r.value);
  }

  async reset(parameter: string) {
    const r = await this.send({ action: 'reset', parameter });
    if (r.value !== undefined) this.applyValue(parameter, r.value);
  }

  async trigger(parameter: string) {
    await this.send({ action: 'trigger', parameter });
  }

  async run<T = unknown>(name: string, args: Record<string, unknown> = {}): Promise<T> {
    const r = await this.send({ action: 'command', name, args });
    return r.result as T;
  }

  // Instancias nuevas (efectos): se añaden a la copia local sin pedir la lista entera.
  mergeParams(list: ParamInfo[]) {
    for (const p of list) this.params.set(p.path, p);
  }

  // La matriz cambió de tamaño: hay parámetros nuevos o retirados.
  async reloadParams() {
    const list = await this.request({ action: 'list' });
    this.params = new Map((list.parameters ?? []).map((p) => [p.path, p]));
    this.listListeners.forEach((fn) => fn());
  }

  value(path: string): ParamValue | undefined {
    return this.params.get(path)?.value;
  }

  onParam(fn: (path: string, value: ParamValue) => void) {
    this.paramListeners.add(fn);
    return () => void this.paramListeners.delete(fn);
  }
  onList(fn: () => void) {
    this.listListeners.add(fn);
    return () => void this.listListeners.delete(fn);
  }
  onState(fn: (s: ConnectionState) => void) {
    this.stateListeners.add(fn);
    return () => void this.stateListeners.delete(fn);
  }
  // Imagen de los monitores: el motor solo la envía mientras alguien escucha.
  onMonitor(fn: (frame: MonitorFrame) => void) {
    this.monitorListeners.add(fn);
    if (this.monitorListeners.size === 1 && this.ws) void this.request({ action: 'monitor', value: true }).catch(() => {});
    return () => {
      this.monitorListeners.delete(fn);
      if (this.monitorListeners.size === 0 && this.ws) void this.request({ action: 'monitor', value: false }).catch(() => {});
    };
  }
  onEvent(fn: (topic: string, data: unknown) => void) {
    this.eventListeners.add(fn);
    return () => void this.eventListeners.delete(fn);
  }
}
