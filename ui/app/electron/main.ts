import { app, BrowserWindow, dialog, globalShortcut, ipcMain, Menu, shell } from 'electron';
import { mkdir, readdir, readFile, writeFile } from 'node:fs/promises';
import { homedir } from 'node:os';
import { basename, extname, join } from 'node:path';
import { EngineSupervisor, enginePath } from './engine';
import { runSmoke } from './smoke';

const VIDEO_EXTENSIONS = ['mp4', 'mov', 'm4v', 'webm', 'avi', 'mkv', 'mpg', 'mpeg', 'wav', 'mp3', 'aif', 'aiff', 'm4a', 'flac', 'aac'];
const IMAGE_EXTENSIONS = ['png', 'jpg', 'jpeg', 'tif', 'tiff', 'bmp', 'gif', 'webp', 'heic'];

let win: BrowserWindow | null = null;
let engine: EngineSupervisor;
let dirty = false;
let closeConfirmed = false;

let advanced: BrowserWindow | null = null;
const send = (id: string) => () => (BrowserWindow.getFocusedWindow() ?? win)?.webContents.send('menu', id);

let nodes: BrowserWindow | null = null;
function openNodes() {
  if (nodes) {
    nodes.focus();
    return;
  }
  nodes = new BrowserWindow({
    width: 1360,
    height: 860,
    minWidth: 900,
    minHeight: 560,
    backgroundColor: '#0a0b0e',
    title: 'Editor de nodos — VJ Studio',
    webPreferences: { preload: join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true },
  });
  void nodes.loadFile(join(__dirname, '..', 'dist', 'index.html'), { hash: 'nodes' });
  nodes.on('closed', () => { nodes = null; });
}

function openAdvanced() {
  if (advanced) {
    advanced.focus();
    return;
  }
  advanced = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 980,
    minHeight: 620,
    backgroundColor: '#0a0b0e',
    title: 'Advanced Output — VJ Studio',
    webPreferences: { preload: join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true },
  });
  void advanced.loadFile(join(__dirname, '..', 'dist', 'index.html'), { hash: 'advanced' });
  advanced.on('closed', () => { advanced = null; });
}

function buildMenu() {
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    { role: 'appMenu' },
    { label: 'Composición', submenu: [
      { label: 'Nueva', accelerator: 'CmdOrCtrl+N', click: send('new') },
      { label: 'Abrir…', accelerator: 'CmdOrCtrl+O', click: send('open') },
      { type: 'separator' },
      { label: 'Guardar', accelerator: 'CmdOrCtrl+S', click: send('save') },
      { label: 'Guardar como…', accelerator: 'CmdOrCtrl+Shift+S', click: send('saveAs') },
      { type: 'separator' },
      { label: 'Deshacer', accelerator: 'CmdOrCtrl+Z', click: send('undo') },
      { label: 'Rehacer', accelerator: 'CmdOrCtrl+Y', click: send('redo') },
    ] },
    { label: 'Deck', submenu: [
      { label: 'Nuevo', accelerator: 'CmdOrCtrl+K', click: send('deck:new') },
      { label: 'Duplicar', click: send('deck:duplicate') },
      { type: 'separator' },
      { label: 'Anterior', click: send('deck:previous') },
      { label: 'Siguiente', click: send('deck:next') },
    ] },
    { label: 'Capa', submenu: [
      { label: 'Nueva', accelerator: 'CmdOrCtrl+L', click: send('layer:new') },
      { label: 'Insertar encima', click: send('layer:insert') },
      { label: 'Duplicar', click: send('layer:duplicate') },
      { type: 'separator' },
      { label: 'Subir', click: send('layer:up') },
      { label: 'Bajar', click: send('layer:down') },
      { label: 'Plegar o desplegar', click: send('layer:fold') },
      { type: 'separator' },
      { label: 'Vaciar clips', click: send('layer:clear') },
      { label: 'Quitar', click: send('layer:remove') },
    ] },
    { label: 'Columna', submenu: [
      { label: 'Nueva', accelerator: 'CmdOrCtrl+T', click: send('column:new') },
      { label: 'Insertar a la derecha', click: send('column:insert') },
      { label: 'Duplicar', click: send('column:duplicate') },
      { type: 'separator' },
      { label: 'Mover a la izquierda', click: send('column:left') },
      { label: 'Mover a la derecha', click: send('column:right') },
      { type: 'separator' },
      { label: 'Quitar', click: send('column:remove') },
    ] },
    { label: 'Clip', submenu: [
      { label: 'Cortar', accelerator: 'CmdOrCtrl+X', click: send('cut'), registerAccelerator: false },
      { label: 'Copiar', accelerator: 'CmdOrCtrl+C', click: send('copy'), registerAccelerator: false },
      { label: 'Pegar', accelerator: 'CmdOrCtrl+V', click: send('paste'), registerAccelerator: false },
      { type: 'separator' },
      { label: 'Vaciar', click: send('clip:clear') },
    ] },
    { label: 'Salida', submenu: [
      { label: 'Configurar pantallas…', accelerator: 'CmdOrCtrl+Shift+A', click: openAdvanced },
      { label: 'Editor de nodos…', accelerator: 'CmdOrCtrl+Shift+N', click: openNodes },
      { type: 'separator' },
      { label: 'Carta de prueba', accelerator: 'CmdOrCtrl+Shift+L', click: () => win?.webContents.send('menu', 'testcard') },
      { label: 'Identificar pantallas', click: () => win?.webContents.send('menu', 'identify') },
      { label: 'Instantánea', accelerator: 'CmdOrCtrl+Shift+P', click: send('snapshot') },
      { type: 'separator' },
      { label: 'Desactivar salida', accelerator: 'CmdOrCtrl+Shift+D', click: () => win?.webContents.send('menu', 'disable') },
    ] },
    { role: 'windowMenu' },
  ]));
}

// Hay que preguntar antes de apagar el motor: después ya no quedaría nada que guardar.
function confirmDiscard() {
  if (closeConfirmed || !dirty || process.env.VJ_SMOKE_DIR) return true;
  const answer = dialog.showMessageBoxSync(win!, {
    type: 'warning', message: 'Hay cambios sin guardar', detail: 'Si sales ahora se perderán.',
    buttons: ['Salir sin guardar', 'Cancelar'], cancelId: 1, defaultId: 1,
  });
  closeConfirmed = answer === 0;
  return closeConfirmed;
}

function createWindow() {
  win = new BrowserWindow({
    width: 1440,
    height: 900,
    minWidth: 900,
    minHeight: 600,
    backgroundColor: '#0a0b0e',
    title: 'VJ Studio',
    webPreferences: { preload: join(__dirname, 'preload.js'), contextIsolation: true, sandbox: true },
  });
  void win.loadFile(join(__dirname, '..', 'dist', 'index.html'));
  win.on('closed', () => {
    win = null;
    advanced?.close();
    nodes?.close();
  });
  win.on('close', (e) => {
    if (!confirmDiscard()) e.preventDefault();
  });
}

ipcMain.handle('engine:url', async () => {
  if (engine.state !== 'running') throw new Error('motor no disponible');
  return `ws://127.0.0.1:${engine.port}/api/v1`;
});
ipcMain.handle('engine:state', () => engine.state);
ipcMain.handle('window:advanced', () => openAdvanced());
ipcMain.handle('window:nodes', () => openNodes());
ipcMain.handle('image:pick', async () => {
  const r = await dialog.showOpenDialog(win!, { title: 'Elegir imagen', properties: ['openFile'], filters: [{ name: 'Imagen', extensions: IMAGE_EXTENSIONS }] });
  return r.canceled ? null : r.filePaths[0];
});
// Solo imágenes elegidas por el usuario: se devuelven como data URL para mostrarlas de guía.
ipcMain.handle('image:read', async (_, path: string) => {
  const extension = extname(path).slice(1).toLowerCase();
  if (!IMAGE_EXTENSIONS.includes(extension)) return '';
  const mime = extension === 'jpg' ? 'jpeg' : extension;
  return readFile(path).then((data) => `data:image/${mime};base64,${data.toString('base64')}`, () => '');
});
ipcMain.handle('file:pick', async (_, title: string, extensions: string[]) => {
  const r = await dialog.showOpenDialog(win!, { title, properties: ['openFile'], filters: [{ name: title, extensions }] });
  return r.canceled ? null : r.filePaths[0];
});
ipcMain.handle('folder:pick', async () => {
  const r = await dialog.showOpenDialog(win!, { title: 'Elegir carpeta', properties: ['openDirectory', 'createDirectory'] });
  return r.canceled ? null : r.filePaths[0];
});
ipcMain.handle('composition:pick', async (_, folder: string) => {
  const r = await dialog.showOpenDialog(win!, { title: 'Abrir composición', defaultPath: folder, properties: ['openFile'], filters: [{ name: 'Composición', extensions: ['vjcomp'] }] });
  return r.canceled ? null : r.filePaths[0];
});
ipcMain.handle('composition:savePath', async (_, folder: string, name: string) => {
  const r = await dialog.showSaveDialog(win!, { title: 'Guardar composición', defaultPath: join(folder, `${name}.vjcomp`), filters: [{ name: 'Composición', extensions: ['vjcomp'] }] });
  return r.canceled ? null : r.filePath;
});
ipcMain.handle('fs:home', () => homedir());
ipcMain.handle('fs:list', async (_, path: string) => {
  const entries = await readdir(path, { withFileTypes: true }).catch(() => []);
  return entries
    .filter((e) => !e.name.startsWith('.'))
    .map((e) => ({ name: e.name, path: join(path, e.name), directory: e.isDirectory() }))
    .sort((a, b) => Number(b.directory) - Number(a.directory) || a.name.localeCompare(b.name));
});
ipcMain.handle('fs:reveal', (_, path: string) => shell.showItemInFolder(path));
const layoutFile = (folder: string, name: string) => join(folder, `${basename(name).replace(/[\\/:]/g, '-')}.json`);
ipcMain.handle('layout:list', async (_, folder: string) => {
  const files = await readdir(folder).catch(() => []);
  return files.filter((f) => f.endsWith('.json')).map((f) => f.slice(0, -5)).sort();
});
ipcMain.handle('layout:save', async (_, folder: string, name: string, text: string) => {
  await mkdir(folder, { recursive: true });
  await writeFile(layoutFile(folder, name), text);
});
ipcMain.handle('layout:load', (_, folder: string, name: string) => readFile(layoutFile(folder, name), 'utf8'));
ipcMain.handle('dialog:confirm', async (_, message: string, detail: string) => {
  const r = await dialog.showMessageBox(win!, { type: 'question', message, detail, buttons: ['Aceptar', 'Cancelar'], cancelId: 1, defaultId: 0 });
  return r.response === 0;
});
ipcMain.on('project:dirty', (_, value: boolean) => { dirty = value; });
ipcMain.on('window:title', (_, title: string) => win?.setTitle(title));
ipcMain.handle('media:pick', async () => {
  const r = await dialog.showOpenDialog(win!, {
    title: 'Cargar clips',
    properties: ['openFile', 'multiSelections'],
    filters: [{ name: 'Vídeo e imagen', extensions: [...VIDEO_EXTENSIONS, ...IMAGE_EXTENSIONS] }, { name: 'Todos', extensions: ['*'] }],
  });
  return r.canceled ? [] : r.filePaths;
});

app.whenReady().then(async () => {
  engine = new EngineSupervisor(enginePath(app.isPackaged, app.getAppPath()), app.getVersion());
  engine.on('state', (s) => win?.webContents.send('engine:state', s));
  await engine.start();
  buildMenu();
  createWindow();
  // Global: una salida a pantalla completa puede tapar la propia ventana del programa.
  globalShortcut.register('CommandOrControl+Shift+D', () => win?.webContents.send('menu', 'disable'));
  if (process.env.VJ_SMOKE_DIR) void runSmoke(win!, process.env.VJ_SMOKE_DIR, engine);
  app.on('activate', () => { if (!win) createWindow(); });
});

app.on('will-quit', () => globalShortcut.unregisterAll());
app.on('window-all-closed', () => app.quit());

let stopping = false;
app.on('before-quit', (e) => {
  if (stopping) return;
  e.preventDefault();
  if (!confirmDiscard()) return;
  stopping = true;
  void engine.stop().finally(() => app.quit());
});
