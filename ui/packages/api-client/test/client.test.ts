import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { EngineClient } from '../src/client';
import { PARAMETERS } from '../src/generated';

class FakeSocket {
  static all: FakeSocket[] = [];
  onopen: ((e: unknown) => void) | null = null;
  onclose: ((e: unknown) => void) | null = null;
  onerror: ((e: unknown) => void) | null = null;
  onmessage: ((e: { data: unknown }) => void) | null = null;
  sent: Record<string, unknown>[] = [];
  list = [{ path: '/output/opacity', type: 'float', value: 1 }];
  constructor() {
    FakeSocket.all.push(this);
  }
  send(data: string) {
    const m = JSON.parse(data);
    this.sent.push(m);
    if (m.action === 'list') this.reply({ id: m.id, ok: true, parameters: this.list });
    else if (m.action === 'set' && m.parameter === '/bad') this.reply({ id: m.id, ok: false, error: 'no' });
    else if (m.action === 'set') this.reply({ id: m.id, ok: true, value: m.value });
    else this.reply({ id: m.id, ok: true });
  }
  reply(m: object) {
    queueMicrotask(() => this.onmessage?.({ data: JSON.stringify({ type: 'response', ...m }) }));
  }
  push(m: object) {
    this.onmessage?.({ data: JSON.stringify(m) });
  }
  close() {
    this.onclose?.({});
  }
}

const flush = async () => {
  await vi.advanceTimersByTimeAsync(0);
};

async function connected() {
  const c = new EngineClient({ url: () => 'ws://x', socket: () => new FakeSocket() as never, reconnectMs: 50 });
  await c.start();
  await flush();
  const s = FakeSocket.all.at(-1)!;
  s.onopen?.({});
  await flush();
  return { c, s };
}

describe('EngineClient', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    FakeSocket.all = [];
  });
  afterEach(() => vi.useRealTimers());

  it('sincroniza la lista y se suscribe a todo al conectar', async () => {
    const { c, s } = await connected();
    expect(c.state).toBe('connected');
    expect(c.value('/output/opacity')).toBe(1);
    expect(s.sent.map((m) => m.action)).toEqual(['list', 'subscribe']);
    expect(s.sent[1].parameter).toBe('*');
  });

  it('aplica actualizaciones del motor y avisa a los oyentes', async () => {
    const { c, s } = await connected();
    const seen: unknown[] = [];
    c.onParam((p, v) => seen.push([p, v]));
    s.push({ type: 'update', parameter: '/output/opacity', value: 0.2 });
    expect(c.value('/output/opacity')).toBe(0.2);
    expect(seen).toEqual([['/output/opacity', 0.2]]);
  });

  it('set guarda el valor que confirma el motor', async () => {
    const { c } = await connected();
    const p = c.set('/output/opacity', 0.4);
    await flush();
    await p;
    expect(c.value('/output/opacity')).toBe(0.4);
  });

  it('un error del motor rechaza la promesa', async () => {
    const { c } = await connected();
    const p = c.set('/bad', 1);
    const check = expect(p).rejects.toThrow('no');
    await flush();
    await check;
  });

  it('se reconecta y resincroniza si se cae el motor', async () => {
    const { c, s } = await connected();
    s.close();
    expect(c.state).toBe('disconnected');
    await vi.advanceTimersByTimeAsync(60);
    expect(c.state).toBe('connecting');
    const again = FakeSocket.all.at(-1)!;
    expect(again).not.toBe(s);
    again.onopen?.({});
    await flush();
    expect(c.state).toBe('connected');
  });

  it('entrega la imagen binaria de los monitores y pide el flujo', async () => {
    const { c, s } = await connected();
    const seen: number[] = [];
    const off = c.onMonitor((f) => seen.push(f.monitor, f.width, f.height, f.pixels[0]));
    expect(s.sent.at(-1)).toMatchObject({ action: 'monitor', value: true });
    const bytes = new Uint8Array([1, 0, 2, 0, 1, 9, 0, 0, 0, 0, 0, 0, 0]);
    s.onmessage?.({ data: bytes.buffer });
    expect(seen).toEqual([1, 2, 1, 9]);
    off();
    expect(s.sent.at(-1)).toMatchObject({ action: 'monitor', value: false });
  });

  it('marca los cambios deshacibles', async () => {
    const { c, s } = await connected();
    void c.set('/output/opacity', 0.1, true);
    expect(s.sent.at(-1)).toMatchObject({ history: true });
    await flush();
  });

  it('stop no vuelve a conectar', async () => {
    const { c } = await connected();
    c.stop();
    await vi.advanceTimersByTimeAsync(200);
    expect(FakeSocket.all).toHaveLength(1);
  });

  it('las peticiones en curso fallan al perder la conexión', async () => {
    const { c, s } = await connected();
    s.send = () => {};
    const p = c.trigger('/media/eject');
    const check = expect(p).rejects.toThrow('conexión perdida');
    s.close();
    await check;
  });
});

describe('contrato generado', () => {
  it('rutas únicas y con hito', () => {
    const paths = PARAMETERS.map((p) => p.path);
    expect(new Set(paths).size).toBe(paths.length);
    expect(PARAMETERS.every((p) => /^M\d+$/.test(p.milestone))).toBe(true);
  });
});
