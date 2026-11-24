import { describe, expect, it } from 'vitest';
import type { ParamInfo } from '@vj/api-client';
import { clipPath, fileName, fillCells, formSections } from '../src/paths';
import { envelopeAdd, envelopeDrag, envelopeRemove } from '../src/paths.extra';

const p = (path: string): ParamInfo => ({ path, type: 'float', readOnly: false, label: path, group: '', unit: '', min: 0, max: 1, milestone: 'M1' });

describe('formSections', () => {
  const list = [
    p('/composition/layers/1/opacity'),
    p('/composition/layers/1/transform/x'),
    p('/composition/layers/1/clips/1/path'),
    p('/composition/layers/10/opacity'),
    p('/composition/columns/1/connect'),
  ];
  it('no mezcla entidades hijas ni prefijos parecidos', () => {
    const s = formSections(list, '/composition/layers/1');
    expect(s.map((x) => x.title)).toEqual(['General', 'Transformación']);
    expect(s[0].fields.map((f) => f.path)).toEqual(['/composition/layers/1/opacity']);
  });
  it('respeta los campos ocultos', () => {
    expect(formSections(list, '/composition/layers/1', ['opacity']).map((x) => x.title)).toEqual(['Transformación']);
  });
  it('coloca autopilot junto al transporte', () => {
    const s = formSections([p('/c/transport/speed'), p('/c/autopilot/action')], '/c');
    expect(s).toHaveLength(1);
    expect(s[0].fields).toHaveLength(2);
  });
});

describe('fillCells', () => {
  it('salta celdas ocupadas y corta al final', () => {
    const used = new Set([3]);
    expect(fillCells(['a', 'b', 'c', 'd'], 2, 4, (c) => used.has(c))).toEqual([
      { column: 2, path: 'a' }, { column: 4, path: 'b' },
    ]);
  });
  it('la celda de partida acepta aunque esté ocupada', () => {
    expect(fillCells(['a'], 1, 4, () => true)).toEqual([{ column: 1, path: 'a' }]);
  });
});

describe('rutas', () => {
  it('forman la ruta del motor', () => expect(clipPath(2, 3)).toBe('/composition/layers/2/clips/3'));
  it('muestran un nombre corto', () => {
    expect(fileName('/a/b/clip.final.mp4')).toBe('clip.final');
    expect(fileName('vj://pattern')).toBe('pattern');
  });
});

describe('envolvente', () => {
  const base: [number, number][] = [[0, 0], [1, 1]];
  it('añade un punto en su orden', () => {
    expect(envelopeAdd(base, [0.5, 0.2]).map((p) => p[0])).toEqual([0, 0.5, 1]);
  });
  it('los extremos no se mueven en el tiempo', () => {
    expect(envelopeDrag(base, 0, [0.4, 0.7])[0]).toEqual([0, 0.7]);
    expect(envelopeDrag(base, 1, [0.4, 0.7])[1]).toEqual([1, 0.7]);
  });
  it('un punto no adelanta a sus vecinos', () => {
    const p: [number, number][] = [[0, 0], [0.4, 0], [0.6, 0], [1, 1]];
    expect(envelopeDrag(p, 1, [0.9, 0.5])[1][0]).toBe(0.6);
  });
  it('los extremos no se borran', () => {
    expect(envelopeRemove(base, 0)).toEqual(base);
    expect(envelopeRemove([[0, 0], [0.5, 1], [1, 0]], 1)).toHaveLength(2);
  });
});
