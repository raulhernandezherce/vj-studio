import { describe, expect, it } from 'vitest';
import { moveInput, newScreen, newSlice, overlapForBlend, regularPolygon, type Setup, snap, splitHorizontally, warpGrid, duplicateScreen, duplicateSlice, fitCorners, insertPoint, matchInputToOutput, matchOutputToInput, rectCorners, removePoint, toggleSelection, parseExpression, snapOffset, snapTargets, splitSlice, moveSliceToScreen, selfCrosses, polygonSlice, intersects, type Pt, AREAS, scaledRect, HEART, maskOutline, centerSlice, cornersRect, makeCircle, mirrorSlice, pasteSlices, placeSlice, reorderSlice, swapShapes } from '../src/model';

const setup = (n: number): Setup => ({ format: 'vjstudio-screens', version: 1, name: 'x', screens: Array.from({ length: n }, (_, i) => newScreen(i + 1, 'virtual')) });

describe('reparto', () => {
  it('parte la composición sin huecos', () => {
    const s = splitHorizontally(setup(3)).screens;
    expect(s.map((x) => x.slices[0].inX)).toEqual([0, 1 / 3, 2 / 3]);
    expect(s[2].slices[0].inX + s[2].slices[0].inW).toBeCloseTo(1);
  });
  it('el solape cubre toda la composición y solo en bordes interiores', () => {
    const s = overlapForBlend(setup(3), 0.15).screens.map((x) => x.slices[0]);
    expect(s[0].inX).toBe(0);
    expect(s[2].inX + s[2].inW).toBeCloseTo(1);
    expect(s[0].blendLeft).toBe(0);
    expect(s[1].blendLeft).toBe(0.15);
    expect(s[1].blendRight).toBe(0.15);
    expect(s[2].blendRight).toBe(0);
    // Dos vecinas comparten justo la franja que se funde.
    expect(s[0].inX + s[0].inW - s[1].inX).toBeCloseTo(s[0].inW * 0.15);
  });
});

describe('geometría de la interfaz', () => {
  it('la entrada no se sale de la composición', () => {
    const s = { ...newSlice(1), inW: 0.5, inH: 0.5 };
    expect(moveInput(s, 0.9, -0.9).inX).toBe(0.5);
    expect(moveInput(s, 0.9, -0.9).inY).toBe(0);
  });
  it('el imán atrae a bordes y centro', () => {
    expect(snap(0.495)).toBe(0.5);
    expect(snap(0.4)).toBe(0.4);
  });
  it('rejilla identidad con el número de puntos pedido', () => {
    const g = warpGrid(4, 3);
    expect(g).toHaveLength(12);
    expect(g[11]).toEqual([1, 1]);
  });
  it('polígono regular dentro del slice', () => {
    for (const [x, y] of regularPolygon(6)) {
      expect(x).toBeGreaterThanOrEqual(0);
      expect(y).toBeLessThanOrEqual(1);
    }
  });
});

describe('Herramientas de slice', () => {
  const base = () => newSlice(1);
  it('duplica un slice con id propio y a continuación del original', () => {
    const screen = newScreen(1, 'virtual');
    const { screen: next, id } = duplicateSlice(screen, screen.slices[0]);
    expect(next.slices.map((s) => s.id)).toEqual([1, id]);
    expect(id).not.toBe(1);
  });
  it('una pantalla duplicada no reutiliza la salida física', () => {
    const screen = newScreen(1, 'display:0');
    const setup = { format: 'x', version: 1, name: 'a', screens: [screen] };
    const { setup: next } = duplicateScreen(setup, screen);
    expect(next.screens[1].device).toBe('virtual');
    expect(next.screens[0].device).toBe('display:0');
  });
  it('iguala la salida a la entrada conservando los píxeles', () => {
    const s = { ...base(), inX: 0.5, inY: 0, inW: 0.25, inH: 0.5 };
    const { corners } = matchOutputToInput(s, [1920, 1080], [3840, 2160]);
    expect(corners![0]).toEqual([0.25, 0]);
    expect(corners![2]).toEqual([0.375, 0.25]);
  });
  it('iguala la entrada a la salida sin salirse de la composición', () => {
    const s = { ...base(), corners: rectCorners({ left: 0.9, top: 0, width: 0.5, height: 0.5 }) };
    const r = matchInputToOutput(s, [1000, 1000], [1000, 1000]);
    expect(r.inX! + r.inW!).toBeLessThanOrEqual(1);
  });
  it('fitCorners conserva una deformación al mover el rectángulo', () => {
    const c: Pt[] = [[0, 0], [1, 0.1], [1, 1], [0, 0.9]];
    const n = fitCorners(c, { left: 0.5, top: 0, width: 0.5, height: 1 });
    expect(n[1]).toEqual([1, 0.1]);
    expect(n[0]).toEqual([0.5, 0]);
  });
  it('inserta un punto en el lado más cercano y nunca baja de tres', () => {
    const square: Pt[] = [[0, 0], [1, 0], [1, 1], [0, 1]];
    expect(insertPoint(square, [0.5, 0.01])[1]).toEqual([0.5, 0.01]);
    const tri: Pt[] = [[0, 0], [1, 0], [1, 1]];
    expect(removePoint(tri, 0)).toEqual(tri);
    expect(removePoint(square, 0)).toHaveLength(3);
  });
});

describe('Selección de slices', () => {
  it('un clic normal deja solo ese slice', () => expect(toggleSelection([1, 2], 3, false)).toEqual([3]));
  it('con modificador añade y quita', () => {
    expect(toggleSelection([1], 2, true)).toEqual([1, 2]);
    expect(toggleSelection([1, 2], 1, true)).toEqual([2]);
  });
  it('nunca deja la selección vacía', () => expect(toggleSelection([1], 1, true)).toEqual([1]));
});

describe('Edición de slices', () => {
  it('calcula expresiones y rechaza lo demás', () => {
    expect(parseExpression('1920/3')).toBe(640);
    expect(parseExpression('100+20*2')).toBe(140);
    expect(parseExpression('-5 + 10')).toBe(5);
    expect(parseExpression('10/0')).toBeNull();
    expect(parseExpression('alert(1)')).toBeNull();
    expect(parseExpression('')).toBeNull();
  });
  it('el imán corrige solo si está cerca', () => {
    expect(snapOffset(0.498, 0.7, [0.5])).toBeCloseTo(0.002);
    expect(snapOffset(0.3, 0.4, [0.9])).toBe(0);
  });
  it('los objetivos del imán excluyen lo que se mueve', () => {
    const a = { ...newSlice(1), inX: 0.2, inW: 0.1 }, b = { ...newSlice(2), inX: 0.6, inW: 0.2 };
    expect(snapTargets([a, b], [1]).x).toContain(0.8);
    expect(snapTargets([a, b], [1]).x).not.toContain(0.3);
  });
  it('divide entrada y salida en partes iguales', () => {
    const screen = newScreen(1, 'virtual');
    const next = splitSlice(screen, screen.slices[0], 3, false);
    expect(next.slices).toHaveLength(3);
    expect(new Set(next.slices.map((s) => s.id)).size).toBe(3);
    expect(next.slices[1].inX).toBeCloseTo(1 / 3);
    expect(next.slices[2].corners[1][0]).toBeCloseTo(1);
  });
  it('mueve un slice a otra pantalla sin dejar la de origen vacía', () => {
    const one = newScreen(1, 'virtual'), two = newScreen(2, 'virtual');
    const blocked = { format: 'x', version: 1, name: 'a', screens: [one, two] };
    expect(moveSliceToScreen(blocked, 1, 2, one.slices[0].id)).toBe(blocked);
    const full = { ...one, slices: [newSlice(1), newSlice(2)] };
    const moved = moveSliceToScreen({ ...blocked, screens: [full, two] }, 1, 2, 2);
    expect(moved.screens[0].slices).toHaveLength(1);
    expect(moved.screens[1].slices).toHaveLength(2);
    expect(moved.screens[1].slices[1].id).toBe(2);
  });
  it('detecta polígonos cruzados', () => {
    expect(selfCrosses([[0, 0], [1, 1], [1, 0], [0, 1]])).toBe(true);
    expect(selfCrosses([[0, 0], [1, 0], [1, 1], [0, 1]])).toBe(false);
  });
  it('crea el polígono relativo a su rectángulo', () => {
    const s = polygonSlice(4, [[0.2, 0.2], [0.6, 0.2], [0.4, 0.6]]);
    expect(s.inX).toBeCloseTo(0.2);
    expect(s.polygon[1]).toEqual([1, 0]);
  });
  it('el lazo detecta solapes', () => {
    const s = { ...newSlice(1), inX: 0.5, inY: 0.5, inW: 0.2, inH: 0.2 };
    expect(intersects(s, { left: 0.4, top: 0.4, width: 0.2, height: 0.2 })).toBe(true);
    expect(intersects(s, { left: 0, top: 0, width: 0.2, height: 0.2 })).toBe(false);
  });
});

describe('Menú de slice', () => {
  const small = { ...newSlice(1), inX: 0.1, inY: 0.2, inW: 0.2, inH: 0.4, corners: rectCorners({ left: 0.1, top: 0.2, width: 0.2, height: 0.4 }) };
  it('centra y refleja en entrada y en salida', () => {
    expect(centerSlice(small, 'input', 'x').inX).toBeCloseTo(0.4);
    expect(mirrorSlice(small, 'input', 'x').inX).toBeCloseTo(0.7);
    expect(mirrorSlice(small, 'input', 'y').inY).toBeCloseTo(0.4);
    expect(cornersRect(centerSlice(small, 'output', 'y').corners as Pt[]).top).toBeCloseTo(0.3);
  });
  it('coloca el slice en una mitad sin tocar el otro lado', () => {
    const p = placeSlice(small, 'output', AREAS.right);
    expect(cornersRect(p.corners as Pt[])).toEqual(AREAS.right);
    expect(p.inX).toBeUndefined();
  });
  it('el círculo es redondo en píxeles y conserva el centro', () => {
    const whole = newSlice(1);
    const p = makeCircle(whole, 'input', [1920, 1080]);
    expect(p.inW! * 1920).toBeCloseTo(1080);
    expect(p.inH! * 1080).toBeCloseTo(1080);
    expect(p.inX! + p.inW! / 2).toBeCloseTo(0.5);
    expect(p.polygon).toHaveLength(64);
  });
  it('intercambia entrada y salida', () => {
    const s = { ...small, corners: rectCorners({ left: 0.5, top: 0, width: 0.5, height: 0.5 }) };
    const p = swapShapes(s, [1000, 1000], [1000, 1000]);
    expect(p.inX).toBeCloseTo(0.5);
    expect(p.inW).toBeCloseTo(0.5);
    expect(cornersRect(p.corners as Pt[]).left).toBeCloseTo(0.1);
  });
  it('cambia el orden sin salirse de la lista', () => {
    const screen = { ...newScreen(1, 'virtual'), slices: [newSlice(1), newSlice(2), newSlice(3)] };
    expect(reorderSlice(screen, 1, 1).slices.map((s) => s.id)).toEqual([2, 1, 3]);
    expect(reorderSlice(screen, 3, 1)).toBe(screen);
  });
  it('pega con ids nuevos, también en otra pantalla', () => {
    const r = pasteSlices(newScreen(2, 'virtual'), [newSlice(1), newSlice(2)]);
    expect(r.ids).toEqual([2, 3]);
    expect(r.screen.slices.map((s) => s.id)).toEqual([1, 2, 3]);
  });
});

describe('Máscaras', () => {
  it('la curva pasa por los puntos y añade tramos', () => {
    const points: Pt[] = [[0, 0], [1, 0], [1, 1], [0, 1]];
    const out = maskOutline({ points, invert: false, smooth: true });
    expect(out).toHaveLength(48);
    expect(out[12][0]).toBeCloseTo(1);
    expect(out[12][1]).toBeCloseTo(0);
    expect(maskOutline({ points, invert: false, smooth: false })).toBe(points);
  });
  it('el corazón cabe en el slice y no se cruza', () => {
    expect(HEART.every(([x, y]) => x >= 0 && x <= 1 && y >= 0 && y <= 1)).toBe(true);
    expect(selfCrosses(HEART)).toBe(false);
  });
});

describe('Modificadores al escalar', () => {
  const s = { ...newSlice(1), inX: 0.2, inY: 0.2, inW: 0.4, inH: 0.2 };
  it('desde la esquina fija', () => {
    const r = scaledRect(s, { x: 0.8, y: 0.5 }, { x: 0.2, y: 0.2 }, false, false);
    expect(r).toEqual({ inX: 0.2, inY: 0.2, inW: 0.6000000000000001, inH: 0.3 });
  });
  it('hacia arriba y a la izquierda', () => {
    const r = scaledRect(s, { x: 0.1, y: 0.1 }, { x: 0.6, y: 0.4 }, false, false);
    expect(r.inX).toBeCloseTo(0.1);
    expect(r.inY).toBeCloseTo(0.1);
  });
  it('desde el centro crece a los dos lados', () => {
    const r = scaledRect(s, { x: 0.7, y: 0.4 }, { x: 0.4, y: 0.3 }, true, false);
    expect(r.inW).toBeCloseTo(0.6);
    expect(r.inX! + r.inW! / 2).toBeCloseTo(0.4);
  });
  it('Mayús conserva la proporción', () => {
    const r = scaledRect(s, { x: 0.8, y: 0.25 }, { x: 0.2, y: 0.2 }, false, true);
    expect(r.inW! / r.inH!).toBeCloseTo(2);
  });
});
