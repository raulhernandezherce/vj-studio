#!/usr/bin/env bash
# Regresión acumulativa: compila y pasa todas las pruebas de M0 hasta el hito actual.
#   tools/regress.sh          motor, interfaz y programa en desarrollo
#   tools/regress.sh --app    además genera el instalador y prueba el programa empaquetado
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
tools/bootstrap.sh >/dev/null
source tools/env.sh
step() { printf '\n== %s\n' "$1"; }

step "Contrato schemas/ alineado"
python3 tools/gen_schema.py --check

step "Compilación C++"
[ -f build/CMakeCache.txt ] || cmake -S . -B build -G "Unix Makefiles"
# Un fallo de compilación detiene todo: seguir probaría el binario anterior.
LOG="$(mktemp)"
if ! cmake --build build -j"$(sysctl -n hw.ncpu)" >"$LOG" 2>&1; then
  grep -E " error: |\*\*\*" "$LOG" || tail -20 "$LOG"
  rm -f "$LOG"
  exit 1
fi
grep -E "engine/.*warning: " "$LOG" || true
rm -f "$LOG"
test -x build/engine/app/vjengine

step "Pruebas del motor"
ctest --test-dir build --output-on-failure --timeout 180

step "Pruebas de la interfaz"
(cd ui && { [ -d node_modules ] || npm install --no-audit --no-fund; } && npx vitest run && npm run build)

# Devuelve 77 si el motor no puede crear un contexto OpenGL (máquinas sin GPU, como los runners de GitHub).
hay_opengl() {
  node -e '
    const c = require("child_process").spawn(process.argv[1], ["--port", "0", "--documents", require("os").tmpdir(), "--no-audio"]);
    let e = "";
    c.stderr.on("data", (d) => e += d);
    c.stdout.on("data", (d) => { if (/VJENGINE_READY/.test(d)) { c.kill(); process.exit(0); } });
    c.on("exit", () => process.exit(/contexto OpenGL/.test(e) ? 77 : 1));
    setTimeout(() => { c.kill(); process.exit(1); }, 20000);
  ' build/engine/app/vjengine
}

step "Programa real"
gl=0; hay_opengl || gl=$?
if [ "$gl" -eq 77 ]; then
  echo "OMITIDO: sin contexto OpenGL 4.1 en esta máquina (necesita GPU)"
  if [ "${1:-}" = "--app" ]; then echo "--app necesita GPU para probar el instalador"; exit 1; fi
else
  node tests/smoke/run_smoke.mjs "$ROOT/ui/node_modules/electron/dist/Electron.app/Contents/MacOS/Electron" .
fi

if [ "${1:-}" = "--app" ]; then
  step "Instalador"
  cmake --build build --target app
  APP="$ROOT/dist/mac-arm64/VJ Studio.app/Contents/MacOS/VJ Studio"
  step "Programa empaquetado"
  node tests/smoke/run_smoke.mjs "$APP"
  ls -lh dist/*.dmg
fi
printf '\nREGRESIÓN OK\n'
