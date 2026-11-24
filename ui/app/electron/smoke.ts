import { app, BrowserWindow } from 'electron';
import { statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import type { EngineSupervisor } from './engine';

const js = <T>(win: BrowserWindow, code: string) => win.webContents.executeJavaScript(code) as Promise<T>;

async function until(fn: () => Promise<boolean>, what: string) {
  for (let i = 0; i < 300; ++i) {
    if (await fn()) return;
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error(`tiempo agotado: ${what}`);
}

async function askEngine(port: number, body: object): Promise<{ value: unknown }> {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://127.0.0.1:${port}/api/v1`);
    ws.onopen = () => ws.send(JSON.stringify({ id: 1, ...body }));
    ws.onmessage = (e) => { resolve(JSON.parse(String(e.data))); ws.close(); };
    ws.onerror = () => reject(new Error('sin conexión'));
  });
}

// Clic de ratón real sobre la celda, a través de Chromium.
async function clickCell(win: BrowserWindow, cell: string) {
  const rect = await js<{ x: number; y: number }>(win, `(() => {
    const r = document.querySelector('[data-cell="${cell}"]').getBoundingClientRect();
    return { x: Math.round(r.x + r.width / 2), y: Math.round(r.y + r.height / 2) };
  })()`);
  win.webContents.sendInputEvent({ type: 'mouseMove', x: rect.x, y: rect.y });
  win.webContents.sendInputEvent({ type: 'mouseDown', x: rect.x, y: rect.y, button: 'left', clickCount: 1 });
  win.webContents.sendInputEvent({ type: 'mouseUp', x: rect.x, y: rect.y, button: 'left', clickCount: 1 });
}

async function openAdvancedForTest(win: BrowserWindow) {
  await js(win, `document.querySelector('[data-testid=open-advanced]').click()`);
  for (let i = 0; i < 100; ++i) {
    const other = BrowserWindow.getAllWindows().find((w) => w !== win);
    if (other) return other;
    await new Promise((r) => setTimeout(r, 50));
  }
  throw new Error('no se abrió la ventana de pantallas');
}

const wait = (ms: number) => new Promise((r) => setTimeout(r, ms));
type At = { x: number; y: number };
const centerOf = (win: BrowserWindow, selector: string) => js<At>(win, `(() => {
  const r = document.querySelector('${selector}').getBoundingClientRect();
  return { x: Math.round(r.x + r.width / 2), y: Math.round(r.y + r.height / 2) };
})()`);

async function press(win: BrowserWindow, at: At, button: 'left' | 'right' = 'left', clickCount = 1) {
  win.webContents.sendInputEvent({ type: 'mouseMove', ...at });
  win.webContents.sendInputEvent({ type: 'mouseDown', ...at, button, clickCount });
  win.webContents.sendInputEvent({ type: 'mouseUp', ...at, button, clickCount });
  await wait(60);
}

async function drag(win: BrowserWindow, from: At, to: At, keys: ('shift' | 'alt')[] = []) {
  win.webContents.sendInputEvent({ type: 'mouseMove', ...from });
  win.webContents.sendInputEvent({ type: 'mouseDown', ...from, button: 'left', clickCount: 1, modifiers: keys });
  await wait(30);
  for (let i = 1; i <= 8; ++i) {
    win.webContents.sendInputEvent({ type: 'mouseMove', x: Math.round(from.x + ((to.x - from.x) * i) / 8), y: Math.round(from.y + ((to.y - from.y) * i) / 8), modifiers: ['leftbuttondown', ...keys] });
    await wait(16);
  }
  win.webContents.sendInputEvent({ type: 'mouseUp', ...to, button: 'left', clickCount: 1 });
  await wait(120);
}

interface TestSlice { inX: number; inY: number; inW: number; inRotation: number; polygon: unknown[]; corners: number[][]; masks: { points: number[][] }[] }

// Ratón y teclado de verdad sobre la ventana de pantallas y la matriz.
async function mouseChecks(win: BrowserWindow, port: number) {
  const slices = async () => ((await askEngine(port, { action: 'command', name: 'screens.get' })) as unknown as { result: { screens: { slices: TestSlice[] }[] } }).result.screens[0].slices;
  const settled = (test: (s: TestSlice[]) => boolean, what: string) => until(async () => test(await slices()), what);
  await askEngine(port, { action: 'command', name: 'screens.set', args: { setup: { format: 'vjstudio-screens', version: 1, name: 'Ratón', screens: [{
    id: 1, name: 'P', device: 'virtual', width: 1920, height: 1080,
    slices: [{ id: 1, name: 'A', inX: 0.1, inY: 0.1, inW: 0.3, inH: 0.3 }, { id: 2, name: 'B', inX: 0.6, inY: 0.6, inW: 0.2, inH: 0.2 }],
  }] } } });
  const adv = await openAdvancedForTest(win);
  await until(() => js<boolean>(adv, `Boolean(document.querySelector('[data-testid=input-stage] [data-slice="2"]'))`), 'lienzo de entrada');
  await wait(300);
  const stage = await js<{ x: number; y: number; w: number; h: number }>(adv, `(() => { const r = document.querySelector('[data-testid=input-stage]').getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; })()`);
  const on = (u: number, v: number): At => ({ x: Math.round(stage.x + u * stage.w), y: Math.round(stage.y + v * stage.h) });
  const a = '[data-testid=input-stage] [data-slice="1"]', b = '[data-testid=input-stage] [data-slice="2"]';

  const hit = await js<string>(adv, `document.elementFromPoint(${on(0.25, 0.25).x}, ${on(0.25, 0.25).y})?.outerHTML.slice(0, 300) ?? 'nada'`);
  if (!hit.includes('data-slice="1"')) throw new Error(`el slice no está bajo el ratón: ${hit}`);
  await drag(adv, on(0.25, 0.25), on(0.33, 0.25));
  await settled((s) => Math.abs(s[0].inX - 0.18) < 0.03 && Math.abs(s[1].inX - 0.6) < 1e-6, 'arrastrar un slice');
  await drag(adv, await centerOf(adv, '[data-handle=rotate]'), on(0.6, 0.25));
  await settled((s) => s[0].inRotation > 45, 'rotar con el asa');
  await drag(adv, on(0.03, 0.97), on(0.97, 0.03));
  const before = await slices();
  adv.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Right' });
  adv.webContents.sendInputEvent({ type: 'keyUp', keyCode: 'Right' });
  await settled((s) => s[0].inX > before[0].inX && s[1].inX > before[1].inX, 'lazo y flecha sobre los dos');

  await press(adv, await centerOf(adv, a));  // deja la selección solo en este
  await press(adv, await centerOf(adv, b));
  await press(adv, await centerOf(adv, b), 'right');
  await until(() => js<boolean>(adv, `Boolean(document.querySelector('[data-area=left]'))`), 'menú con clic derecho');
  await js(adv, `document.querySelector('[data-area=left]').click()`);
  await settled((s) => s[1].inX === 0 && s[1].inW === 0.5, 'mitad izquierda');
  await press(adv, await centerOf(adv, b), 'right');
  await until(() => js<boolean>(adv, `Boolean(document.querySelector('[data-testid=menu-copy]'))`), 'menú de nuevo');
  await js(adv, `document.querySelector('[data-testid=menu-copy]').click()`);
  await press(adv, await centerOf(adv, b), 'right');
  await until(() => js<boolean>(adv, `document.querySelector('[data-testid=menu-paste]')?.disabled === false`), 'pegar activo');
  await js(adv, `document.querySelector('[data-testid=menu-paste]').click()`);
  await settled((s) => s.length === 3, 'pegar');

  await js(adv, `document.querySelector('[data-testid=draw-polygon]').click()`);
  await wait(100);
  await press(adv, on(0.8, 0.1));
  await press(adv, on(0.95, 0.1));
  await press(adv, on(0.9, 0.3));
  await press(adv, on(0.9, 0.3), 'left', 2);
  await settled((s) => s.length === 4 && s[3].polygon.length >= 3, 'dibujar un polígono');

  await js(adv, `document.querySelector('[data-testid=slice-mask-rect]').click()`);
  await settled((s) => s[3].masks.length === 1, 'máscara de slice');
  await until(() => js<boolean>(adv, `Boolean(document.querySelector('[data-mask-point="0"]'))`), 'puntos de la máscara');
  const start = await centerOf(adv, '[data-mask-point="0"]');
  await drag(adv, start, { x: start.x - 12, y: start.y + 12 });
  await settled((s) => s[3].masks[0].points[0][0] !== 0.2, 'arrastrar un punto de máscara');

  // Modificadores del manual: ⌘D, Retroceso, Alt para duplicar y Mayús para bloquear el eje.
  adv.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'D', modifiers: ['meta'] });
  await settled((s) => s.length === 5, 'Cmd+D duplica');
  adv.webContents.sendInputEvent({ type: 'keyDown', keyCode: 'Backspace' });
  await settled((s) => s.length === 4, 'Retroceso quita');
  const pasted = '[data-testid=input-stage] [data-slice="3"]';  // la copia queda encima de B
  const lone = await centerOf(adv, pasted);
  await press(adv, lone);
  await drag(adv, lone, { x: lone.x + 20, y: lone.y + 20 }, ['alt']);
  await settled((s) => s.length === 5, 'Alt + arrastrar duplica');
  const held = (await slices())[2];
  // La copia tapa casi todo el original: se agarra por la esquina que asoma.
  const second = await js<At>(adv, `(() => { const r = document.querySelector('${pasted}').getBoundingClientRect(); return { x: Math.round(r.right - 6), y: Math.round(r.bottom - 6) }; })()`);
  await drag(adv, second, { x: second.x + 40, y: second.y + 8 }, ['shift']);
  await settled((s) => s[2].inX > held.inX && Math.abs(s[2].inY - held.inY) < 1e-9, 'Mayús bloquea el eje');

  await js(adv, `document.querySelector('[data-testid=tab-output]').click()`);
  await until(() => js<boolean>(adv, `Boolean(document.querySelector('[data-testid=output-stage] rect.handle'))`), 'lienzo de salida');
  const corner = await centerOf(adv, '[data-testid=output-stage] rect.handle');
  await drag(adv, corner, { x: corner.x + 30, y: corner.y + 30 });
  const moved = await slices();
  if (moved.every((s) => s.corners[0][0] === 0)) throw new Error('la esquina de salida no se movió');
  adv.close();
  await until(async () => BrowserWindow.getAllWindows().length === 1, 'ventana de pantallas cerrada');

  const layers = async () => Number((await askEngine(port, { action: 'get', parameter: '/composition/structure/layers' })).value);
  const count = await layers();
  await press(win, await centerOf(win, '[data-layer="1"] .layer-name'), 'right');
  await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-testid=layer-duplicate]'))`), 'menú de capa');
  await js(win, `document.querySelector('[data-testid=layer-duplicate]').click()`);
  await until(async () => (await layers()) === count + 1, 'duplicar capa');
  await askEngine(port, { action: 'command', name: 'history.undo' });
  await until(async () => (await layers()) === count, 'deshacer duplicar capa');
  const height = () => js<number>(win, `document.querySelector('[data-layer="1"]').getBoundingClientRect().height`);
  await wait(500);  // deshacer vuelve a leer los parámetros
  const tall = await height();
  await press(win, await centerOf(win, '[data-testid=layer-fold-1]'));
  await until(async () => (await height()) < tall / 2, 'plegar capa');
  await press(win, await centerOf(win, '[data-testid=layer-fold-1]'));
  await until(async () => (await height()) === tall, 'desplegar capa');
}

// Recorre el programa real: interfaz → motor → interfaz. Lo usa tools/regress.sh.
export async function runSmoke(win: BrowserWindow, dir: string, engine: EngineSupervisor) {
  const result: Record<string, unknown> = {};
  const consoleErrors: string[] = [];
  let code = 0;
  try {
    await new Promise<void>((res) => win.webContents.once('did-finish-load', () => res()));
    await until(() => js<boolean>(win, `document.body.dataset.engine === 'connected'`), 'conexión con el motor');
    result.backend = (await askEngine(engine.port, { action: 'get', parameter: '/engine/backend' })).value;

    await until(() => js<boolean>(win, `document.querySelectorAll('.cell').length > 0`), 'matriz de clips');
    win.webContents.on('console-message', (e) => { if (e.level === 'error') consoleErrors.push(e.message); });
    result.cells = await js<number>(win, `document.querySelectorAll('.cell').length`);

    // Mueve el maestro como lo haría el ratón y comprueba que llega al motor.
    await js(win, `(() => {
      const el = document.querySelector('[data-testid=master]');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, '0.5');
      el.dispatchEvent(new Event('input', { bubbles: true }));
    })()`);
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/composition/master' })).value === 0.5, 'maestro en el motor');
    result.masterFromUi = 0.5;

    // Cambia un valor por la API y comprueba que la interfaz lo refleja.
    await askEngine(engine.port, { action: 'set', parameter: '/composition/speed', value: 2 });
    await until(() => js<boolean>(win, `document.body.innerText.includes('2.00×')`), 'velocidad en la interfaz');
    result.speedToUi = 2;

    // Carga un clip, lo dispara con un clic real sobre la celda y mira la barra de progreso.
    const clip = '/composition/layers/1/clips/2';
    const source = process.env.VJ_SMOKE_VIDEO ?? 'vj://pattern';
    await askEngine(engine.port, { action: 'set', parameter: `${clip}/path`, value: source });
    await until(() => js<boolean>(win, `document.querySelector('[data-cell="1-2"]').classList.contains('filled')`), 'clip en la celda');
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: `${clip}/status` })).value === 'ready', 'clip preparado');
    await clickCell(win, '1-2');
    await until(() => js<boolean>(win, `document.querySelector('[data-cell="1-2"]').classList.contains('live')`), 'clip en vivo en la interfaz');
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: `${clip}/connected` })).value === true, 'clip en vivo en el motor');
    result.clipFiredFromCell = true;

    // Columna entera, con el botón de cabecera.
    await askEngine(engine.port, { action: 'set', parameter: '/composition/layers/2/clips/3/path', value: 'vj://pattern' });
    await askEngine(engine.port, { action: 'set', parameter: '/composition/layers/3/clips/3/path', value: 'vj://pattern' });
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/composition/layers/3/clips/3/status' })).value === 'ready', 'columna preparada');
    await js(win, `document.querySelector('[data-column="3"]').click()`);
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/composition/layers/2/clips/3/connected' })).value === true, 'columna disparada');
    result.columnFired = true;

    // Un parámetro del inspector generado desde el contrato.
    await js(win, `(() => { const el = document.querySelector('select[data-path$="/transport/playmode"]');
      Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value').set.call(el, 'Ping Pong');
      el.dispatchEvent(new Event('change', { bubbles: true })); })()`);
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: `${clip}/transport/playmode` })).value === 'Ping Pong', 'inspector en el motor');
    result.inspector = true;

    // M2: miniatura en la celda e imagen real en los monitores.
    await until(() => js<boolean>(win, `document.querySelector('[data-cell="1-2"]').style.backgroundImage.includes('data:image')`), 'miniatura en la celda');
    result.thumbnail = true;
    await askEngine(engine.port, { action: 'reset', parameter: '/composition/master' });
    const lit = (id: string) => js<boolean>(win, `(() => {
      const c = document.querySelector('[data-testid=${id}]');
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      for (let i = 0; i < d.length; i += 400) if (d[i] + d[i + 1] + d[i + 2] > 60) return true;
      return false;
    })()`);
    await until(() => lit('monitor-output'), 'imagen en el monitor Output');
    result.outputMonitor = true;

    await js(win, `document.querySelector('[data-cell="1-2"]').dispatchEvent(new MouseEvent('dblclick', { bubbles: true }))`);
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/preview/column' })).value === 2, 'clip en preview');
    await until(() => lit('monitor-preview'), 'imagen en el monitor Preview');
    result.previewMonitor = true;
    await askEngine(engine.port, { action: 'command', name: 'preview.stop' });
    await until(async () => !(await lit('monitor-preview')), 'preview vacío al cerrar');

    // Deck nuevo con el botón, cambio y vuelta.
    await js(win, `document.querySelector('[data-testid=deck-add]').click()`);
    await until(() => js<boolean>(win, `document.querySelectorAll('[data-deck]').length === 2`), 'deck nuevo');
    await js(win, `document.querySelector('[data-deck="1"]').click()`);
    await until(() => js<boolean>(win, `!document.querySelector('[data-cell="1-2"]').classList.contains('filled')`), 'deck 2 vacío');
    await js(win, `document.querySelector('[data-deck="0"]').click()`);
    await until(() => js<boolean>(win, `document.querySelector('[data-cell="1-2"]').classList.contains('filled')`), 'vuelta al deck 1');
    result.decks = true;

    // Deshacer desde el menú real: quita el deck añadido.
    win.webContents.send('menu', 'undo');
    await until(() => js<boolean>(win, `document.querySelectorAll('[data-deck]').length === 1`), 'deshacer desde el menú');
    result.undoFromMenu = true;

    // Guardar y volver a abrir por la API, y ver la composición en la matriz.
    const saved = join(dir, 'smoke.vjcomp');
    await askEngine(engine.port, { action: 'command', name: 'project.save', args: { path: saved } });
    await until(async () => win.getTitle().includes('smoke'), 'título del proyecto');
    await askEngine(engine.port, { action: 'command', name: 'project.new' });
    await until(() => js<boolean>(win, `!document.querySelector('[data-cell="1-2"]').classList.contains('filled')`), 'composición nueva');
    await askEngine(engine.port, { action: 'command', name: 'project.open', args: { path: saved } });
    await until(() => js<boolean>(win, `document.querySelector('[data-cell="1-2"]').classList.contains('filled')`), 'composición abierta');
    result.saveOpen = true;

    // M3: añade un efecto desde el panel y comprueba que cambia la imagen de salida.
    await askEngine(engine.port, { action: 'trigger', parameter: '/composition/eject' });
    await askEngine(engine.port, { action: 'set', parameter: '/composition/master', value: 1 });
    await askEngine(engine.port, { action: 'command', name: 'source.add', args: { layer: 1, column: 1, type: 'solid' } });
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/composition/layers/1/clips/1/status' })).value === 'ready', 'fuente preparada');
    await askEngine(engine.port, { action: 'trigger', parameter: '/composition/layers/1/clips/1/connect' });
    const center = () => js<number[]>(win, `(() => { const c = document.querySelector('[data-testid=monitor-output]');
      return [...c.getContext('2d').getImageData(c.width / 2, c.height / 2, 1, 1).data].slice(0, 3); })()`);
    await until(async () => (await center())[0] > 200, 'fuente en el monitor');
    await js(win, `[...document.querySelectorAll('.tabs button')].find((b) => b.innerText === 'Efectos').click()`);
    await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-testid=effect-add]')) && document.querySelector('[data-testid=effect-add]').options.length > 5`), 'catálogo en el panel');
    const pick = (selector: string, value: string) => js(win, `(() => { const el = document.querySelector('${selector}');
      Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value').set.call(el, '${value}');
      el.dispatchEvent(new Event('change', { bubbles: true })); })()`);
    await pick('[data-testid=effect-add]', 'invert');
    await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-effect=invert]'))`), 'tarjeta del efecto');
    await until(async () => (await center())[0] < 60, 'efecto en la imagen');
    await js(win, `document.querySelector('[data-effect=invert] .effect-tools button').click()`);
    await until(async () => (await center())[0] > 200, 'bypass en la imagen');
    writeFileSync(join(dir, 'effects.png'), (await win.webContents.capturePage()).toPNG());
    await js(win, `[...document.querySelectorAll('.tabs button')].find((b) => b.innerText === 'Clip').click()`);
    result.effects = true;

    // M4: el botón Tap y el botón ∿ del inspector llegan al motor.
    for (let i = 0; i < 5; ++i) {
      await js(win, `document.querySelector('[data-testid=tap]').click()`);
      await new Promise((r) => setTimeout(r, 300));
    }
    await until(async () => Math.abs(Number((await askEngine(engine.port, { action: 'get', parameter: '/composition/tempo/bpm' })).value) - 200) < 40, 'Tap en el motor');
    result.tap = true;
    await askEngine(engine.port, { action: 'set', parameter: '/composition/tempo/bpm', value: 120 });
    await js(win, `document.querySelector('[data-animate="/composition/speed"]')?.click()`);
    await js(win, `[...document.querySelectorAll('.tabs button')].find((b) => b.innerText === 'Composición').click()`);
    await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-animate="/composition/speed"]'))`), 'botón de animar');
    await js(win, `document.querySelector('[data-animate="/composition/speed"]').click()`);
    await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-testid=anim-add]'))`), 'panel de animación');
    await js(win, `document.querySelector('[data-testid=anim-add]').click()`);
    await until(async () => { const a = await askEngine(engine.port, { action: 'command', name: 'animation.get', args: { path: '/composition/speed' } }) as { result?: { mode?: string } }; return a.result?.mode === 'timeline'; }, 'animación en el motor');
    await until(async () => Number((await askEngine(engine.port, { action: 'get', parameter: '/composition/speed' })).value) !== 2, 'parámetro animado');
    await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-testid=envelope]'))`), 'envolvente');
    writeFileSync(join(dir, 'animation.png'), (await win.webContents.capturePage()).toPNG());
    await js(win, `document.querySelector('[data-testid=anim-remove]').click()`);
    result.animation = true;
    await js(win, `document.querySelector('[data-tab=audio]').click()`);
    await until(() => js<boolean>(win, `document.querySelector('[data-testid=audio-status]')?.innerText === 'activo'`), 'audio activo');
    result.audioDevice = true;

    // M6: pantallas y escenarios, por la ventana real de Advanced Output.
    const advancedWindow = await openAdvancedForTest(win);
    await until(() => js<boolean>(advancedWindow, `Boolean(document.querySelector('[data-testid=add-screen]'))`), 'ventana de pantallas');
    // El escenario persiste entre arranques: el número de partida puede no ser 1.
    const before = await js<number>(advancedWindow, `document.querySelectorAll('[data-screen]').length`);
    await js(advancedWindow, `document.querySelector('[data-testid=add-screen]').click()`);
    await until(() => js<boolean>(advancedWindow, `document.querySelectorAll('[data-screen]').length === ${before + 1}`), 'pantalla nueva');
    await js(advancedWindow, `document.querySelector('[data-testid=duplicate-slice]').click()`);
    await until(() => js<boolean>(advancedWindow, `document.querySelectorAll('.adv-item.sub').length >= 2`), 'slice duplicado');
    await js(advancedWindow, `document.querySelector('[data-testid=stage-grid]').click()`);
    await until(() => js<boolean>(advancedWindow, `Boolean(document.querySelector('.stage-grid'))`), 'rejilla del lienzo');
    await js(advancedWindow, `document.querySelector('[data-testid=match-output]').click()`);
    // Clic derecho real en la lista: dividir en dos columnas añade slices.
    const slicesBefore = await js<number>(advancedWindow, `document.querySelectorAll('.adv-item.sub').length`);
    await js(advancedWindow, `document.querySelector('[data-slice-item]').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, clientX: 60, clientY: 200 }))`);
    await until(() => js<boolean>(advancedWindow, `Boolean(document.querySelector('[data-split="2"]'))`), 'menú de slice');
    await js(advancedWindow, `document.querySelector('[data-split="2"]').click()`);
    await until(() => js<boolean>(advancedWindow, `document.querySelectorAll('.adv-item.sub').length === ${slicesBefore + 1}`), 'slice dividido');
    await js(advancedWindow, `document.querySelector('[data-testid=mask-rect]')?.click()`);
    await js(advancedWindow, `document.querySelector('[data-testid=split]').click()`);
    await until(async () => {
      const r = await askEngine(engine.port, { action: 'command', name: 'screens.get' }) as { result?: { screens: { slices: { inX: number }[] }[] } };
      const n = r.result?.screens.length ?? 0;
      return n === before + 1 && Math.abs((r.result?.screens[n - 1].slices[0].inX ?? 0) - (n - 1) / n) < 1e-9;
    }, 'reparto en el motor');
    await js(advancedWindow, `(() => { const el = document.querySelector('[data-testid=scenario-name]');
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, 'Sala');
      el.dispatchEvent(new Event('input', { bubbles: true })); })()`);
    await until(() => js<boolean>(advancedWindow, `!document.querySelector('[data-testid=scenario-save]').disabled`), 'nombre de escenario');
    await js(advancedWindow, `document.querySelector('[data-testid=scenario-save]').click()`);
    await until(() => js<boolean>(advancedWindow, `[...document.querySelector('[data-testid=scenario-select]').options].some((o) => o.value === 'Sala')`), 'escenario guardado');
    writeFileSync(join(dir, 'advanced.png'), (await advancedWindow.webContents.capturePage()).toPNG());
    await askEngine(engine.port, { action: 'command', name: 'screens.preset.delete', args: { name: 'Sala' } });
    advancedWindow.close();
    await until(async () => BrowserWindow.getAllWindows().length === 1, 'ventana de pantallas cerrada');

    await mouseChecks(win, engine.port);
    result.mouse = true;

    // M8: crear un nodo de verdad desde la ventana y guardar el parche.
    const demoNode = (id: number, type: string, name: string, x: number, y: number) => ({ id, type, name, x, y, values: {}, attrs: {}, hidden: [] });
    await askEngine(engine.port, { action: 'command', name: 'patch.set', args: { patch: {
      format: 'vjstudio-patch', version: 1, name: 'Ejemplo', category: 'effect',
      nodes: [demoNode(1, 'tex.in', 'Texture Input', 60, 120), demoNode(2, 'fx.kaleidoscope', 'Caleidoscopio', 300, 90), demoNode(3, 'fx.invert', 'Invertir', 540, 120), demoNode(4, 'tex.out', 'Texture Output', 780, 150)],
      links: [{ from: 1, fromPort: 'out', to: 2, toPort: 'in' }, { from: 2, fromPort: 'out', to: 3, toPort: 'in' }, { from: 3, fromPort: 'out', to: 4, toPort: 'in' }],
    } } });
    await js(win, `document.querySelector('[data-testid=open-nodes]').click()`);
    let nodesWindow: BrowserWindow | undefined;
    await until(async () => (nodesWindow = BrowserWindow.getAllWindows().find((w) => w !== win)) !== undefined, 'ventana de nodos');
    const nw = nodesWindow!;
    await until(() => js<boolean>(nw, `Boolean(document.querySelector('[data-testid=node-canvas]'))`), 'lienzo de nodos');
    await new Promise((r) => setTimeout(r, 300));
    await js(nw, `document.querySelector('[data-testid=node-canvas]').dispatchEvent(new MouseEvent('dblclick', { bubbles: true, clientX: 420, clientY: 420 }))`);
    await until(() => js<boolean>(nw, `Boolean(document.querySelector('[data-testid=node-search]'))`), 'buscador de nodos');
    await js(nw, `(() => { const el = document.querySelector('[data-testid=node-search]'); Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, 'Solid Color'); el.dispatchEvent(new Event('input', { bubbles: true })); })()`);
    await until(() => js<boolean>(nw, `Boolean(document.querySelector('.node-finder li button'))`), 'resultado de la búsqueda');
    await js(nw, `document.querySelector('.node-finder li button').click()`);
    await until(() => js<boolean>(nw, `Boolean(document.querySelector('[data-node="tex.solid"]'))`), 'nodo creado');
    await new Promise((r) => setTimeout(r, 600));
    writeFileSync(join(dir, 'nodes.png'), (await nw.webContents.capturePage()).toPNG());
    await js(nw, `document.querySelector('[data-testid=patch-save]').click()`);
    await until(() => js<boolean>(nw, `document.querySelector('.nodes-msg').textContent.includes('Guardado')`), 'parche guardado');
    result.nodes = true;
    await until(() => js<boolean>(win, `/CPU \\d+ %/.test(document.querySelector('[data-testid=perf]')?.innerText ?? '')`), 'rendimiento visible');
    result.perf = true;
    nw.close();
    result.screens = true;

    // M7: el botón de grabar deja un archivo de verdad.
    await js(win, `document.querySelector('[data-testid=record]').click()`);
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/record/status' })).value === 'grabando', 'grabación en marcha');
    await new Promise((r) => setTimeout(r, 700));
    await js(win, `document.querySelector('[data-testid=record]').click()`);
    await until(async () => (await askEngine(engine.port, { action: 'get', parameter: '/record/status' })).value === 'parado', 'grabación parada');
    const file = String((await askEngine(engine.port, { action: 'get', parameter: '/record/file' })).value);
    if (statSync(file).size < 1000) throw new Error('grabación vacía');
    result.recording = true;

    // Estructura: una capa y una columna más, en vivo, y vuelta atrás.
    const cellsBefore = await js<number>(win, `document.querySelectorAll('.cell').length`);
    await askEngine(engine.port, { action: 'command', name: 'structure.addLayer' });
    await askEngine(engine.port, { action: 'command', name: 'structure.addColumn' });
    await until(() => js<boolean>(win, `document.querySelectorAll('.cell').length > ${cellsBefore}`), 'matriz ampliada');
    await askEngine(engine.port, { action: 'command', name: 'structure.removeColumn' });
    await askEngine(engine.port, { action: 'command', name: 'structure.removeLayer' });
    await until(() => js<boolean>(win, `document.querySelectorAll('.cell').length === ${cellsBefore}`), 'matriz restaurada');
    result.structure = true;

    // Pestañas del lateral: cada panel se monta sin errores.
    for (const tab of ['compositions', 'effects-browser', 'sources-browser', 'animation', 'audio', 'media', 'notes', 'cliptime', 'preferences', 'files']) {
      await js(win, `document.querySelector('[data-tab=${tab}]').click()`);
      await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-panel=${tab}]'))`), `panel ${tab}`);
    }
    result.panels = true;
    await js(win, `[...document.querySelectorAll('.tabs button')].find((b) => b.innerText === 'DMX')?.click()`);
    await until(() => js<boolean>(win, `Boolean(document.querySelector('[data-testid=dmx-add]'))`), 'panel DMX');
    await js(win, `document.querySelector('[data-testid=dmx-add]').click()`);
    await new Promise((r) => setTimeout(r, 400));
    writeFileSync(join(dir, 'dmx.png'), (await win.webContents.capturePage()).toPNG());

    await askEngine(engine.port, { action: 'reset', parameter: '/composition/speed' });
    await new Promise((r) => setTimeout(r, 300));
    const image = await win.webContents.capturePage();
    writeFileSync(join(dir, 'ui.png'), image.toPNG());
    result.adopted = engine.adopted;
    if (consoleErrors.length) throw new Error(`errores en la interfaz: ${consoleErrors.join(' | ')}`);

    if (!engine.adopted) {
      engine.crashForTest();
      await until(() => js<boolean>(win, `document.body.dataset.engine !== 'connected'`), 'la interfaz ve la caída');
      await until(() => js<boolean>(win, `document.body.dataset.engine === 'connected'`), 'recuperación tras la caída');
      result.recoveredFromCrash = true;
    }
    result.ok = true;
  } catch (e) {
    result.ok = false;
    result.error = e instanceof Error ? e.stack ?? e.message : String(e);
    result.console = consoleErrors;
    for (const w of BrowserWindow.getAllWindows())
      if (w !== win) result.other = await js<string>(w, `(document.querySelector('.warn')?.innerText ?? 'sin aviso') + ' | ' + document.querySelectorAll('[data-screen]').length + ' pantallas'`).catch((x) => String(x));
    result.engineScreens = await askEngine(engine.port, { action: 'command', name: 'screens.get' }).then((r) => JSON.stringify(r).slice(0, 300), (x) => String(x));
    result.body = await js<string>(win, `document.querySelector('.side')?.innerText.slice(0, 300) ?? 'sin lateral'`).catch(() => 'sin página');
    code = 1;
  }
  writeFileSync(join(dir, 'smoke.json'), JSON.stringify(result, null, 2));
  await engine.stop();
  app.exit(code);
}
