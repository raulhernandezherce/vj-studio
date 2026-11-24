import type { ComponentType } from 'react';

export type PanelSlot = 'top' | 'main' | 'lower' | 'side' | 'bottom';

export interface PanelDefinition {
  id: string;
  title: string;
  milestone: string;
  slot: PanelSlot;
  order: number;
  component: ComponentType;
  hidden?: boolean;  // oculto en el layout por defecto
  fixed?: boolean;   // no se mueve ni se oculta
}

// Cada hito añade sus paneles con registerPanel; la ventana no cambia.
export class PanelRegistry {
  private panels = new Map<string, PanelDefinition>();

  register(panel: PanelDefinition) {
    if (this.panels.has(panel.id)) throw new Error(`panel duplicado: ${panel.id}`);
    this.panels.set(panel.id, panel);
  }

  inSlot(slot: PanelSlot): PanelDefinition[] {
    return [...this.panels.values()].filter((p) => p.slot === slot).sort((a, b) => a.order - b.order);
  }

  all(): PanelDefinition[] {
    return [...this.panels.values()];
  }
}

export const panels = new PanelRegistry();

// Disposición de la ventana: dónde va cada panel y qué se oculta. Es un JSON transferible.
export interface Layout {
  version: 2;
  slots: Record<string, PanelSlot>;
  hidden: string[];
  sideWidth: number;
  bottomHeight: number;
  lowerHeight: number;
}

export function defaultLayout(registry: PanelRegistry = panels): Layout {
  const list = registry.all().filter((p) => !p.fixed);
  return {
    version: 2,
    slots: Object.fromEntries(list.map((p) => [p.id, p.slot])),
    hidden: list.filter((p) => p.hidden).map((p) => p.id),
    sideWidth: 340,
    bottomHeight: 200,
    lowerHeight: 400,
  };
}

// Paneles visibles de una zona según el layout. Un panel nuevo, que un layout
// guardado aún no conoce, aparece en su zona por defecto.
export function visibleIn(layout: Layout, slot: PanelSlot, registry: PanelRegistry = panels): PanelDefinition[] {
  return registry
    .all()
    .filter((p) => !layout.hidden.includes(p.id) && (layout.slots[p.id] ?? p.slot) === slot)
    .sort((a, b) => a.order - b.order);
}

let current: Layout | null = null;
const listeners = new Set<() => void>();

export function getLayout(): Layout {
  current ??= defaultLayout();
  return current;
}

export function setLayout(next: Layout) {
  current = next;
  listeners.forEach((fn) => fn());
}

export function subscribeLayout(fn: () => void) {
  listeners.add(fn);
  return () => void listeners.delete(fn);
}
