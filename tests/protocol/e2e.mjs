// Prueba extremo a extremo: arranca vjengine de verdad y habla con él por WebSocket.
import { spawn } from 'node:child_process';
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const [, , engine, makeFixture, ffglDir] = process.argv;
const root = join(dirname(fileURLToPath(import.meta.url)), '../..');
const docs = mkdtempSync(join(tmpdir(), 'vj-docs-'));
const schema = JSON.parse(readFileSync(join(root, 'schemas/parameters.json'), 'utf8'));

function start(args) {
  const child = spawn(engine, [...args, '--port', '0', '--documents', docs, '--no-audio'], { stdio: ['ignore', 'pipe', 'inherit'] });
  return new Promise((resolve, reject) => {
    let out = '';
    child.stdout.on('data', (d) => {
      out += d;
      const m = out.match(/VJENGINE_READY port=(\d+)/);
      if (m) resolve({ child, port: Number(m[1]) });
    });
    child.on('exit', (c) => reject(new Error(`motor terminó con código ${c}`)));
  });
}

async function connect(port) {
  const ws = new WebSocket(`ws://127.0.0.1:${port}/api/v1`);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let id = 0;
  const pending = new Map();
  const updates = [];
  const frames = [];
  ws.onmessage = (e) => {
    if (typeof e.data !== 'string') { frames.push(e.data); return; }
    const m = JSON.parse(e.data);
    if (m.type === 'response') { pending.get(m.id)?.(m); pending.delete(m.id); } else updates.push(m);
  };
  const call = (body) => new Promise((res) => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, ...body })); });
  return { ws, call, updates, frames };
}

const exited = (child) => new Promise((res) => child.on('exit', (code, sig) => res({ code, sig })));

async function headlessScenario() {
  const { child, port } = await start(['--headless']);
  const { call, updates, ws } = await connect(port);

  const health = await (await fetch(`http://127.0.0.1:${port}/api/v1/health`)).json();
  assert.equal(health.app, 'vjstudio');
  const uiVersion = JSON.parse(readFileSync(join(root, 'ui/app/package.json'), 'utf8')).version;
  assert.equal(health.version, uiVersion, 'motor e interfaz con la misma versión');

  const list = await call({ action: 'list' });
  const paths = new Set(list.parameters.map((p) => p.path));
  for (const p of schema.parameters) assert.ok(paths.has(p.path), p.path);
  // 4 capas × 8 columnas por defecto: una copia de cada plantilla.
  for (const [key, l, c, g] of [['layer', 4, 1, 1], ['clip', 4, 8, 1], ['group', 1, 1, 4], ['column', 1, 8, 1], ['groupcolumn', 1, 8, 4]]) {
    const t = schema.templates[key];
    const prefix = t.prefix.replace('{l}', l).replace('{c}', c).replace('{g}', g);
    for (const p of t.parameters) assert.ok(paths.has(`${prefix}/${p.path}`), `${prefix}/${p.path}`);
  }

  assert.equal((await call({ action: 'set', parameter: '/output/opacity', value: 0.25 })).value, 0.25);
  assert.equal((await call({ action: 'get', parameter: '/output/opacity' })).value, 0.25);
  assert.equal((await call({ action: 'reset', parameter: '/output/opacity' })).value, 1);
  assert.equal((await call({ action: 'set', parameter: '/engine/fps', value: 1 })).ok, false);

  await call({ action: 'subscribe', parameter: '/engine/frames' });
  await new Promise((r) => setTimeout(r, 1200));
  assert.ok(updates.some((u) => u.parameter === '/engine/frames'), 'el motor publica su avance');
  await call({ action: 'unsubscribe', parameter: '/engine/frames' });

  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0);
  ws.close();
}

async function sigtermScenario() {
  const { child } = await start(['--headless']);
  const gone = exited(child);
  child.kill('SIGTERM');
  assert.equal((await gone).code, 0, 'SIGTERM cierra limpio');
}

async function gpuScenario() {
  const { child, port } = await start(['--hidden']);
  const { call, frames, ws } = await connect(port);
  ws.binaryType = 'arraybuffer';
  await call({ action: 'set', parameter: '/composition/layers/1/clips/1/path', value: 'vj://solid/FF0000' });
  await new Promise((r) => setTimeout(r, 200));
  await call({ action: 'trigger', parameter: '/composition/layers/1/clips/1/connect' });
  await call({ action: 'monitor', value: true });
  await new Promise((r) => setTimeout(r, 500));
  assert.ok(frames.length > 3, `llegan fotogramas de monitor: ${frames.length}`);
  const frame = new Uint8Array(frames.at(-1));
  assert.equal(frame[0], 0);
  assert.equal(frame.length, 5 + 480 * 270 * 4);
  assert.ok(frame[5 + (135 * 480 + 240) * 4] > 200, 'monitor Output muestra la salida');
  await call({ action: 'monitor', value: false });
  assert.equal((await call({ action: 'get', parameter: '/engine/backend' })).value, 'OpenGL 4.1');
  await new Promise((r) => setTimeout(r, 700));
  const fps = (await call({ action: 'get', parameter: '/engine/fps' })).value;
  assert.ok(fps > 10, `fps razonable: ${fps}`);
  assert.ok((await call({ action: 'get', parameter: '/output/width' })).value > 0);
  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0);
}

// Vídeo real: se abre por la API, se salta a la parte azul y se lee la imagen de salida.
async function videoScenario() {
  const dir = mkdtempSync(join(tmpdir(), 'vj-e2e-'));
  const video = join(dir, 'clip.mp4');
  const shot = join(dir, 'out.ppm');
  execFileSync(makeFixture, [video]);
  const { child, port } = await start(['--hidden', '--snapshot', shot]);
  const { call } = await connect(port);
  const clip = '/composition/layers/1/clips/1';
  await call({ action: 'set', parameter: `${clip}/path`, value: video });
  for (let i = 0; i < 100; ++i) {
    if ((await call({ action: 'get', parameter: `${clip}/status` })).value === 'ready') break;
    await new Promise((r) => setTimeout(r, 50));
  }
  assert.equal((await call({ action: 'get', parameter: `${clip}/status` })).value, 'ready');
  assert.equal((await call({ action: 'get', parameter: `${clip}/mediawidth` })).value, 320);
  await call({ action: 'trigger', parameter: `${clip}/connect` });
  await new Promise((r) => setTimeout(r, 100));
  assert.equal((await call({ action: 'get', parameter: `${clip}/connected` })).value, true);
  await call({ action: 'set', parameter: `${clip}/transport/direction`, value: 'Pause' });
  await call({ action: 'set', parameter: `${clip}/transport/position`, value: 0.75 });
  await new Promise((r) => setTimeout(r, 500));
  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0);

  const ppm = readFileSync(shot);
  let pos = 0;
  const line = () => { const e = ppm.indexOf(10, pos); const t = ppm.toString('latin1', pos, e); pos = e + 1; return t; };
  line();
  const [w, h] = line().split(' ').map(Number);
  line();
  const px = pos + (Math.floor(h / 2) * w + Math.floor(w / 2)) * 3;
  assert.ok(ppm[px + 2] > 200 && ppm[px] < 60, `centro azul, leído ${[...ppm.subarray(px, px + 3)]}`);
  rmSync(dir, { recursive: true });
}

// Guardar, cerrar y abrir deja la misma imagen, bit a bit.
async function roundTripScenario() {
  const { child, port } = await start(['--hidden']);
  const { call } = await connect(port);
  const set = (parameter, value) => call({ action: 'set', parameter, value });
  const fire = (l) => call({ action: 'trigger', parameter: `/composition/layers/${l}/clips/1/connect` });
  const settle = () => new Promise((r) => setTimeout(r, 300));
  await set('/composition/layers/1/clips/1/path', 'vj://pattern');
  await set('/composition/layers/2/clips/1/path', 'vj://circle');
  await set('/composition/layers/1/clips/1/transport/direction', 'Pause');
  await set('/composition/layers/1/clips/1/transform/rotation', 12);
  await set('/composition/layers/2/mask', 'One Below');
  await set('/composition/layers/1/blend', 'Alpha');
  await set('/composition/master', 0.8);
  const fx = await call({ action: 'command', name: 'effect.add', args: { entity: '/composition/layers/1', type: 'hue' } });
  await set(`/composition/layers/1/effects/e${fx.result.id}/amount`, 0.3);
  await settle();
  await fire(1);
  await fire(2);
  await settle();
  const a = join(docs, 'a.png');
  const b = join(docs, 'b.png');
  const comp = join(docs, 'Compositions/ida-vuelta.vjcomp');
  assert.equal((await call({ action: 'command', name: 'output.capture', args: { path: a } })).ok, true);
  await call({ action: 'command', name: 'project.save', args: { path: comp } });
  await call({ action: 'command', name: 'project.new' });
  await settle();
  await call({ action: 'command', name: 'project.open', args: { path: comp } });
  await settle();
  await fire(1);
  await fire(2);
  await settle();
  await call({ action: 'command', name: 'output.capture', args: { path: b } });
  assert.ok(readFileSync(a).equals(readFileSync(b)), 'la imagen tras abrir es idéntica');
  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0);
}

// Plugins FFGL reales en Extra Effects, junto a uno que se cae al cargarlo.
async function ffglScenario() {
  mkdirSync(join(docs, 'Extra Effects'), { recursive: true });
  for (const p of ['Gradients', 'AddSubtract', 'Crash']) cpSync(join(ffglDir, `${p}.bundle`), join(docs, 'Extra Effects', `${p}.bundle`), { recursive: true });
  const { child, port } = await start(['--hidden']);
  const { call, frames, ws } = await connect(port);
  ws.binaryType = 'arraybuffer';
  const catalog = (await call({ action: 'command', name: 'effects.catalog' })).result;
  const gradient = catalog.find((e) => e.name === 'Gradient Example');
  assert.ok(gradient, 'el plugin aparece en el catálogo');
  assert.equal(gradient.source, true);
  assert.ok(catalog.some((e) => e.name === 'AddSub Example' && !e.source));
  const scan = (await call({ action: 'command', name: 'effects.rescan' })).result;
  assert.ok(scan.some((e) => e.file === 'Crash.bundle'), 'el plugin roto se informa');

  const added = await call({ action: 'command', name: 'source.add', args: { layer: 1, column: 1, type: gradient.id } });
  assert.equal(added.ok, true, added.error);
  await new Promise((r) => setTimeout(r, 200));
  await call({ action: 'trigger', parameter: '/composition/layers/1/clips/1/connect' });
  await call({ action: 'monitor', value: true });
  await new Promise((r) => setTimeout(r, 500));
  const frame = new Uint8Array(frames.at(-1));
  let lit = 0;
  for (let i = 5; i < frame.length; i += 400) lit += frame[i] + frame[i + 1] + frame[i + 2] > 30 ? 1 : 0;
  assert.ok(lit > 100, `el plugin se ve en el monitor: ${lit}`);

  // Se guarda en el proyecto como cualquier efecto.
  const comp = join(docs, 'Compositions/ffgl');
  const saved = await call({ action: 'command', name: 'project.save', args: { path: comp } });
  await call({ action: 'command', name: 'project.new' });
  assert.equal((await call({ action: 'command', name: 'project.open', args: { path: saved.result.path } })).ok, true);
  const list = await call({ action: 'command', name: 'effects.list', args: { entity: '/composition/layers/1/clips/1' } });
  assert.equal(list.result[0].type, gradient.id);

  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0, 'cierra limpio con plugins cargados');
  rmSync(join(docs, 'Extra Effects'), { recursive: true });
}

// OSC por UDP real y API REST por HTTP real.
async function controlScenario() {
  const dgram = await import('node:dgram');
  const { child, port } = await start(['--headless']);
  const { call } = await connect(port);
  await call({ action: 'set', parameter: '/preferences/osc/inport', value: 17000 });
  await call({ action: 'set', parameter: '/preferences/osc/input', value: true });
  await new Promise((r) => setTimeout(r, 200));
  const pad = (b) => Buffer.concat([b, Buffer.alloc(4 - (b.length % 4))]);
  const f = Buffer.alloc(4);
  f.writeFloatBE(0.25);
  const msg = Buffer.concat([pad(Buffer.from('/composition/layers/1/video/opacity')), pad(Buffer.from(',f')), f]);
  const sock = dgram.createSocket('udp4');
  await new Promise((r) => sock.send(msg, 17000, '127.0.0.1', r));
  await new Promise((r) => setTimeout(r, 200));
  assert.equal((await call({ action: 'get', parameter: '/composition/layers/1/opacity' })).value, 0.25);
  sock.close();

  const base = `http://127.0.0.1:${port}/api/v1`;
  const put = await fetch(`${base}/parameter/composition/master`, { method: 'PUT', body: JSON.stringify({ value: 0.5 }) });
  assert.equal(put.status, 200);
  assert.equal((await call({ action: 'get', parameter: '/composition/master' })).value, 0.5);
  const routes = await (await fetch(`${base}/routes`)).json();
  assert.ok(routes.length > 10);
  const evil = await fetch(`${base}/product`, { headers: { Origin: 'https://evil.example' } });
  assert.equal(evil.status, 403, 'una web ajena no puede usar la API');
  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0);
}

// Mover o quitar capas no debe cambiar qué capa recibe cada slice.
async function routingScenario() {
  const { child, port } = await start(['--hidden']);
  const { call } = await connect(port);
  const screens = async () => (await call({ action: 'command', name: 'screens.get' })).result.screens[0].slices;
  const run = (name, args = {}) => call({ action: 'command', name, args });
  const original = (await run('screens.get')).result;  // el escenario persiste entre arranques
  const slice = { id: 1, name: 'S', sourceType: 'layer', sourceIndex: 1 };
  await run('screens.set', { setup: { format: 'vjstudio-screens', version: 1, name: 'R', screens: [{ id: 1, name: 'P', device: 'virtual', width: 320, height: 180, slices: [slice] }] } });
  await run('structure.moveLayer', { layer: 1, to: 3 });
  assert.equal((await screens())[0].sourceIndex, 3, 'el slice sigue a su capa');
  await run('history.undo');
  assert.equal((await screens())[0].sourceIndex, 1, 'deshacer devuelve la ruta');
  await run('structure.removeLayer', { layer: 1 });
  assert.equal((await screens())[0].sourceType, 'composition', 'sin su capa vuelve a la composición');
  await run('history.undo');
  assert.equal((await screens())[0].sourceType, 'layer');
  await run('screens.set', { setup: original });
  const gone = exited(child);
  await call({ action: 'trigger', parameter: '/engine/quit' });
  assert.equal((await gone).code, 0);
}

await headlessScenario();
await routingScenario();
await sigtermScenario();
await gpuScenario();
if (makeFixture) await videoScenario();
await roundTripScenario();
await controlScenario();
if (ffglDir) await ffglScenario();
rmSync(docs, { recursive: true });
console.log('e2e ok');
