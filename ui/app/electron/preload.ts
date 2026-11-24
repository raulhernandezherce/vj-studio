import { contextBridge, ipcRenderer, webUtils } from 'electron';

contextBridge.exposeInMainWorld('vjHost', {
  engineUrl: () => ipcRenderer.invoke('engine:url'),
  pickMedia: () => ipcRenderer.invoke('media:pick'),
  pickImage: () => ipcRenderer.invoke('image:pick'),
  readImage: (path: string) => ipcRenderer.invoke('image:read', path),
  pickFile: (title: string, extensions: string[]) => ipcRenderer.invoke('file:pick', title, extensions),
  openAdvanced: () => ipcRenderer.invoke('window:advanced'),
  openNodes: () => ipcRenderer.invoke('window:nodes'),
  pickFolder: () => ipcRenderer.invoke('folder:pick'),
  pickComposition: (folder: string) => ipcRenderer.invoke('composition:pick', folder),
  pickSavePath: (folder: string, name: string) => ipcRenderer.invoke('composition:savePath', folder, name),
  listDir: (path: string) => ipcRenderer.invoke('fs:list', path),
  homeDir: () => ipcRenderer.invoke('fs:home'),
  reveal: (path: string) => ipcRenderer.invoke('fs:reveal', path),
  listLayouts: (folder: string) => ipcRenderer.invoke('layout:list', folder),
  saveLayout: (folder: string, name: string, text: string) => ipcRenderer.invoke('layout:save', folder, name, text),
  loadLayout: (folder: string, name: string) => ipcRenderer.invoke('layout:load', folder, name),
  confirm: (message: string, detail: string) => ipcRenderer.invoke('dialog:confirm', message, detail),
  setDirty: (dirty: boolean) => ipcRenderer.send('project:dirty', dirty),
  setTitle: (title: string) => ipcRenderer.send('window:title', title),
  onMenu: (fn: (id: string) => void) => {
    const listener = (_: unknown, id: string) => fn(id);
    ipcRenderer.on('menu', listener);
    return () => ipcRenderer.removeListener('menu', listener);
  },
  pathForFile: (file: File) => webUtils.getPathForFile(file),
  onEngineState: (fn: (state: string) => void) => {
    const listener = (_: unknown, state: string) => fn(state);
    ipcRenderer.on('engine:state', listener);
    void ipcRenderer.invoke('engine:state').then(fn);
    return () => ipcRenderer.removeListener('engine:state', listener);
  },
});
