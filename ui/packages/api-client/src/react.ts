import { createContext, useCallback, useContext, useEffect, useState, useSyncExternalStore } from 'react';
import type { ConnectionState, EngineClient, ParamValue } from './client';

export const EngineContext = createContext<EngineClient | null>(null);

export function useEngine(): EngineClient {
  const c = useContext(EngineContext);
  if (!c) throw new Error('falta EngineContext');
  return c;
}

export function useConnection(): ConnectionState {
  const engine = useEngine();
  return useSyncExternalStore(
    useCallback((fn) => engine.onState(fn), [engine]),
    () => engine.state,
  );
}

// Vuelve a pintar cuando llega la lista completa de parámetros.
export function useParamList() {
  const engine = useEngine();
  const [, bump] = useState(0);
  useEffect(() => engine.onList(() => bump((n) => n + 1)), [engine]);
  return engine.params;
}

// Valor vivo de un parámetro y función para cambiarlo.
export function useParam(path: string, fallback: boolean): [boolean, (v: boolean) => void];
export function useParam(path: string, fallback: number): [number, (v: number) => void];
export function useParam(path: string, fallback: string): [string, (v: string) => void];
export function useParam<T extends ParamValue>(path: string, fallback: T): [T, (v: T) => void] {
  const engine = useEngine();
  const read = useCallback(() => (engine.value(path) as T | undefined) ?? fallback, [engine, path, fallback]);
  const [value, setValue] = useState<T>(read);
  useEffect(() => {
    setValue(read());
    const offParam = engine.onParam((p, v) => {
      if (p === path) setValue(v as T);
    });
    const offList = engine.onList(() => setValue(read()));
    return () => {
      offParam();
      offList();
    };
  }, [engine, path, read]);
  const set = useCallback(
    (v: T) => {
      setValue(v);
      engine.set(path, v, true).catch(() => setValue(read()));
    },
    [engine, path, read],
  );
  return [value, set];
}
