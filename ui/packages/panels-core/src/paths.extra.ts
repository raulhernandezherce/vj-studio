export const PASTEL: Record<string, string> = {
  None: '', Rojo: '#7a3a44', Naranja: '#7d5a33', Amarillo: '#756e36', Verde: '#3f6e4b',
  Cian: '#35686f', Azul: '#3a5482', Violeta: '#5a4580', Rosa: '#7c4670',
};

export interface DeckInfo {
  index: number;
  id: number;
  name: string;
  color: string;
  closed: boolean;
  clips: number;
}

export function parseDecks(text: string): DeckInfo[] {
  try {
    const list = JSON.parse(text);
    return Array.isArray(list) ? list : [];
  } catch {
    return [];
  }
}

export interface DashboardLink {
  param: string;
  in: number;
  out: number;
  invert: boolean;
}

export function parseLinks(text: string): DashboardLink[] {
  try {
    const list = JSON.parse(text);
    return Array.isArray(list) ? list : [];
  } catch {
    return [];
  }
}

export const clock = (s: number) => {
  const m = Math.floor(s / 60);
  return `${m}:${(s - m * 60).toFixed(2).padStart(5, '0')}`;
};

// Nombre visible de un clip: el propio, o el del archivo.
export function clipLabel(name: string, file: string) {
  if (name) return name;
  if (file.startsWith('vj://')) return file.slice(5);
  return file.split('/').pop()?.replace(/\.[^.]+$/, '') ?? file;
}

export interface CatalogEntry {
  id: string;
  name: string;
  category: string;
  source: boolean;
}

export type EnvelopePoint = [number, number];

// Los extremos se quedan en t = 0 y t = 1: la curva cubre siempre todo el recorrido.
export function envelopeAdd(points: EnvelopePoint[], p: EnvelopePoint): EnvelopePoint[] {
  return [...points, p].sort((a, b) => a[0] - b[0]);
}

export function envelopeDrag(points: EnvelopePoint[], index: number, p: EnvelopePoint): EnvelopePoint[] {
  const last = points.length - 1;
  const lo = index === 0 ? 0 : index === last ? 1 : points[index - 1][0];
  const hi = index === 0 ? 0 : index === last ? 1 : points[index + 1][0];
  return points.map((q, i) => (i === index ? [Math.min(hi, Math.max(lo, p[0])), p[1]] : q));
}

export function envelopeRemove(points: EnvelopePoint[], index: number): EnvelopePoint[] {
  if (index === 0 || index === points.length - 1) return points;
  return points.filter((_, i) => i !== index);
}
