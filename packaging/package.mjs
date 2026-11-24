// Prepara una carpeta limpia con interfaz y motor, y genera el instalador en dist/.
// Se hace fuera del repositorio para que electron-builder no dependa de su ruta ni del workspace.
import { execFileSync } from 'node:child_process';
import { cpSync, existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const app = join(root, 'ui/app');
const exe = process.platform === 'win32' ? 'vjengine.exe' : 'vjengine';
// Visual Studio deja el ejecutable dentro de una carpeta por configuración.
const engine = [join(root, 'build/engine/app', exe), join(root, 'build/engine/app/Release', exe)].find(existsSync);
if (!engine) throw new Error('falta el motor: compila antes con CMake');

const stage = mkdtempSync(join(tmpdir(), 'vjstudio-stage-'));
try {
  for (const d of ['dist', 'dist-electron']) cpSync(join(app, d), join(stage, d), { recursive: true });
  cpSync(engine, join(stage, 'engine-bin', exe));
  const pkg = JSON.parse(readFileSync(join(app, 'package.json'), 'utf8'));
  delete pkg.devDependencies;
  delete pkg.scripts;
  writeFileSync(join(stage, 'package.json'), JSON.stringify(pkg, null, 2));

  const electron = JSON.parse(readFileSync(join(root, 'ui/node_modules/electron/package.json'), 'utf8'));
  execFileSync(process.execPath, [
    join(root, 'ui/node_modules/electron-builder/cli.js'),
    '--config', join(here, 'electron-builder.yml'),
    '--projectDir', stage,
    `-c.electronVersion=${electron.version}`,
    `-c.electronDist=${join(root, 'ui/node_modules/electron/dist')}`,
  ], { stdio: 'inherit', cwd: stage });

  rmSync(join(root, 'dist'), { recursive: true, force: true });
  cpSync(join(stage, 'out'), join(root, 'dist'), { recursive: true, verbatimSymlinks: true });
} finally {
  rmSync(stage, { recursive: true, force: true });
}
