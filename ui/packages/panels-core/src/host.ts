// Lo que la interfaz pide al proceso principal de Electron.
export interface DirEntry {
  name: string;
  path: string;
  directory: boolean;
}

export interface HostApi {
  engineUrl(): Promise<string>;
  onEngineState(fn: (state: string) => void): () => void;
  pickMedia(): Promise<string[]>;
  pickImage(): Promise<string | null>;
  readImage(path: string): Promise<string>;
  pickFile(title: string, extensions: string[]): Promise<string | null>;
  pickFolder(): Promise<string | null>;
  openAdvanced(): Promise<void>;
  openNodes(): Promise<void>;
  pickComposition(folder: string): Promise<string | null>;
  pickSavePath(folder: string, name: string): Promise<string | null>;
  pathForFile(file: File): string;
  listDir(path: string): Promise<DirEntry[]>;
  homeDir(): Promise<string>;
  reveal(path: string): Promise<void>;
  onMenu(fn: (id: string) => void): () => void;
  listLayouts(folder: string): Promise<string[]>;
  saveLayout(folder: string, name: string, text: string): Promise<void>;
  loadLayout(folder: string, name: string): Promise<string>;
  confirm(message: string, detail: string): Promise<boolean>;
  setDirty(dirty: boolean): void;
  setTitle(title: string): void;
}

declare global {
  interface Window {
    vjHost: HostApi;
  }
}
