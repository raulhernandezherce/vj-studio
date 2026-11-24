// Modelo del editor: funciones puras sobre el parche, con deshacer. El motor valida de nuevo.
export interface PortDef { name: string; type: string; def: number; min: number; max: number; text: string }
export interface AttrDef { name: string; label: string; type: string; def: unknown; options: string[] }
export interface NodeDef { id: string; name: string; category: string; description: string; inputs: PortDef[]; outputs: PortDef[]; attrs: AttrDef[] }
export interface PatchNode {
  id: number; type: string; name: string; x: number; y: number;
  values: Record<string, unknown>; attrs: Record<string, unknown>; hidden: string[];
}
export interface PatchLink { from: number; fromPort: string; to: number; toPort: string }
export interface Patch { format: 'vjstudio-patch'; version: number; name: string; category: string; nodes: PatchNode[]; links: PatchLink[] }

export const emptyPatch = (): Patch => ({ format: 'vjstudio-patch', version: 1, name: 'Sin título', category: 'none', nodes: [], links: [] });

const NUMBER = new Set(['float', 'int', 'bool', 'trigger']);
// La misma regla que el motor: aquí sirve para avisar antes de enviar.
export function linkError(from: string, to: string): string {
  if (from === to || (NUMBER.has(from) && NUMBER.has(to))) return '';
  if (to === 'string' && from !== 'texture') return '';
  return `No se puede conectar ${from} con ${to}`;
}

export function addNode(p: Patch, def: NodeDef, x: number, y: number): Patch {
  const id = p.nodes.reduce((m, n) => Math.max(m, n.id), 0) + 1;
  return { ...p, nodes: [...p.nodes, { id, type: def.id, name: def.name, x, y, values: {}, attrs: {}, hidden: [] }] };
}

export function removeNodes(p: Patch, ids: number[]): Patch {
  const gone = new Set(ids);
  return { ...p, nodes: p.nodes.filter((n) => !gone.has(n.id)), links: p.links.filter((l) => !gone.has(l.from) && !gone.has(l.to)) };
}

// Una entrada admite una sola conexión: conectar otra reemplaza la anterior.
export function connect(p: Patch, link: PatchLink): Patch {
  return { ...p, links: [...p.links.filter((l) => !(l.to === link.to && l.toPort === link.toPort)), link] };
}

export function duplicate(p: Patch, ids: number[]): { patch: Patch; ids: number[] } {
  let next = p;
  const created: number[] = [];
  for (const n of p.nodes.filter((q) => ids.includes(q.id))) {
    const id = next.nodes.reduce((m, q) => Math.max(m, q.id), 0) + 1;
    next = { ...next, nodes: [...next.nodes, { ...structuredClone(n), id, x: n.x + 30, y: n.y + 30 }] };
    created.push(id);
  }
  return { patch: next, ids: created };
}

export function setHidden(p: Patch, defs: Map<string, NodeDef>, ids: number[], hide: boolean): Patch {
  return { ...p, nodes: p.nodes.map((n) => {
    if (!ids.includes(n.id)) return n;
    const linked = new Set(p.links.filter((l) => l.to === n.id).map((l) => l.toPort));
    // Un puerto conectado nunca se oculta: esconderlo escondería la conexión.
    return { ...n, hidden: hide ? (defs.get(n.type)?.inputs ?? []).map((q) => q.name).filter((q) => !linked.has(q)) : [] };
  }) };
}

export function search(defs: NodeDef[], text: string): NodeDef[] {
  const q = text.trim().toLowerCase();
  if (!q) return defs;
  return defs.filter((d) => `${d.name} ${d.category} ${d.description}`.toLowerCase().includes(q));
}

export class History {
  private past: Patch[] = [];
  private future: Patch[] = [];
  push(p: Patch) { this.past.push(p); if (this.past.length > 200) this.past.shift(); this.future = []; }
  undo(now: Patch) { const p = this.past.pop(); if (p) this.future.push(now); return p ?? now; }
  redo(now: Patch) { const p = this.future.pop(); if (p) this.past.push(now); return p ?? now; }
}
