import { useSyncExternalStore } from 'react';

export interface Selection {
  layer: number;
  column: number;
}

let current: Selection = { layer: 1, column: 1 };
const listeners = new Set<() => void>();

export function select(next: Selection) {
  current = next;
  listeners.forEach((fn) => fn());
}

export function useSelection(): Selection {
  return useSyncExternalStore(
    (fn) => {
      listeners.add(fn);
      return () => void listeners.delete(fn);
    },
    () => current,
  );
}
