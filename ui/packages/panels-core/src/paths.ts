import type { ParamInfo } from '@vj/api-client';

export const layerPath = (l: number) => `/composition/layers/${l}`;
export const clipPath = (l: number, c: number) => `${layerPath(l)}/clips/${c}`;
export const groupPath = (g: number) => `/composition/groups/${g}`;
export const columnPath = (c: number) => `/composition/columns/${c}`;

export const fileName = (path: string) => {
  if (path.startsWith('vj://')) return path.slice(5);
  return path.split('/').pop()?.replace(/\.[^.]+$/, '') ?? path;
};

export interface FormSection {
  title: string;
  fields: ParamInfo[];
}

const SECTION_TITLES: Record<string, string> = {
  transform: 'Transformación', transport: 'Transporte', video: 'Vídeo', audio: 'Audio', transition: 'Transición', autopilot: 'Autopilot', crossfader: 'Crossfader', tempo: 'Tempo', structure: 'Estructura', columns: 'Columnas',
};

// Agrupa los parámetros directos de un prefijo. Omite lo que cuelga de otras entidades.
export function formSections(params: Iterable<ParamInfo>, prefix: string, hidden: readonly string[] = []): FormSection[] {
  const general: ParamInfo[] = [];
  const sections = new Map<string, ParamInfo[]>();
  for (const p of params) {
    if (!p.path.startsWith(`${prefix}/`)) continue;
    const rest = p.path.slice(prefix.length + 1);
    const parts = rest.split('/');
    if (parts[0] === 'clips' || parts[0] === 'columns' || parts[0] === 'dashboard' || parts[0] === 'effects' || parts[0] === 'layers' || parts[0] === 'groups' || hidden.includes(rest)) continue;
    if (parts.length === 1) general.push(p);
    else if (parts[0] === 'autopilot' && parts.length === 2) (sections.get('transport') ?? sections.set('transport', []).get('transport')!).push(p);
    else (sections.get(parts[0]) ?? sections.set(parts[0], []).get(parts[0])!).push(p);
  }
  const out: FormSection[] = [];
  if (general.length) out.push({ title: 'General', fields: general });
  for (const [key, fields] of sections) out.push({ title: SECTION_TITLES[key] ?? key, fields });
  return out;
}

// Reparte varios archivos en celdas libres consecutivas desde una columna.
export function fillCells(paths: string[], start: number, columns: number, occupied: (column: number) => boolean) {
  const out: { column: number; path: string }[] = [];
  let c = start;
  for (const path of paths) {
    while (c <= columns && occupied(c) && c !== start) c++;
    if (c > columns) break;
    out.push({ column: c, path });
    c++;
  }
  return out;
}
