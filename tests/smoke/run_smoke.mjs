// Abre el programa real (en desarrollo o el .app empaquetado) y recorre interfaz → motor.
// Uso: node run_smoke.mjs <ejecutable> <argumentos...>
import { execFileSync, spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, copyFileSync, mkdirSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';

const root = join(dirname(fileURLToPath(import.meta.url)), '../..');
const [, , exe, ...args] = process.argv;
const engine = join(root, 'build/engine/app/vjengine');
const dir = mkdtempSync(join(tmpdir(), 'vj-smoke-'));
const video = join(dir, 'clip.mp4');
execFileSync(join(root, 'build/tests/vj_make_fixture'), [video]);

const PORT = 17777;
const portFree = await fetch(`http://127.0.0.1:${PORT}/`).then(() => false, () => true);
assert.ok(portFree, `el puerto ${PORT} está ocupado: quedó un motor de una prueba anterior`);

function runApp(env) {
  const child = spawn(exe, args, { cwd: join(root, 'ui/app'), env: { ...process.env, ...env, VJ_SMOKE_DIR: dir, VJ_DOCUMENTS: join(dir, 'docs'), VJ_ENGINE_PORT: String(PORT) }, stdio: ['ignore', 'ignore', 'inherit'] });
  const timer = setTimeout(() => child.kill('SIGKILL'), 60_000);
  return new Promise((res) => child.on('exit', (code) => { clearTimeout(timer); res(code); }));
}
const report = () => JSON.parse(readFileSync(join(dir, 'smoke.json'), 'utf8'));

// 1. El programa arranca el motor, reproduce un vídeo y se recupera de una caída.
assert.equal(await runApp({ VJ_SMOKE_VIDEO: video }), 0, JSON.stringify(report()));
const first = report();
assert.equal(first.ok, true);
assert.equal(first.backend, 'OpenGL 4.1');
assert.equal(first.clipFiredFromCell, true);
assert.equal(first.columnFired, true);
assert.equal(first.inspector, true);
assert.equal(first.cells, 32);
for (const key of ['thumbnail', 'outputMonitor', 'previewMonitor', 'decks', 'undoFromMenu', 'saveOpen', 'effects', 'tap', 'animation', 'audioDevice', 'screens', 'recording', 'nodes', 'perf', 'panels', 'structure']) assert.equal(first[key], true, key);
assert.equal(first.adopted, false);
assert.equal(first.recoveredFromCrash, true);
if (process.env.VJ_SMOKE_SHOT) {
  mkdirSync(dirname(process.env.VJ_SMOKE_SHOT), { recursive: true });
  copyFileSync(join(dir, 'ui.png'), process.env.VJ_SMOKE_SHOT);
  copyFileSync(join(dir, 'effects.png'), process.env.VJ_SMOKE_SHOT.replace('.png', '-effects.png'));
  copyFileSync(join(dir, 'animation.png'), process.env.VJ_SMOKE_SHOT.replace('.png', '-animation.png'));
  for (const n of ['nodes', 'dmx']) copyFileSync(join(dir, `${n}.png`), process.env.VJ_SMOKE_SHOT.replace('.png', `-${n}.png`));
  copyFileSync(join(dir, 'advanced.png'), process.env.VJ_SMOKE_SHOT.replace('.png', '-advanced.png'));
}

// 2. Si la interfaz murió y el motor sigue emitiendo, se reengancha sin lanzar otro.
const kept = spawn(engine, ['--hidden', '--port', String(PORT), '--documents', join(dir, 'docs')], { stdio: ['ignore', 'pipe', 'inherit'] });
await new Promise((res) => kept.stdout.once('data', res));
const keptGone = new Promise((res) => kept.on('exit', res));
assert.equal(await runApp({}), 0, JSON.stringify(report()));
assert.equal(report().adopted, true);
assert.equal(await keptGone, 0, 'al cerrar la interfaz se cierra el motor reenganchado');

// 3. No queda ningún motor vivo.
assert.ok(await fetch(`http://127.0.0.1:${PORT}/`).then(() => false, () => true), 'motor huérfano');
rmSync(dir, { recursive: true });
console.log('smoke ok:', JSON.stringify(first));
