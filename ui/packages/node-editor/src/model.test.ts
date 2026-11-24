import { describe, expect, it } from 'vitest';
import { addNode, connect, duplicate, emptyPatch, History, linkError, type NodeDef, removeNodes, search, setHidden } from './model';

const def = (id: string, inputs: string[] = ['a', 'b']): NodeDef => ({
  id, name: id, category: 'Matemáticas', description: 'suma', outputs: [],
  inputs: inputs.map((name) => ({ name, type: 'float', def: 0, min: 0, max: 1, text: '' })), attrs: [],
});

describe('editor de nodos', () => {
  it('crea, conecta y destruye 10 nodos con deshacer y rehacer', () => {
    const h = new History();
    let p = emptyPatch();
    for (let i = 0; i < 10; i++) { h.push(p); p = addNode(p, def('add'), i, 0); }
    h.push(p);
    p = connect(p, { from: 1, fromPort: 'out', to: 2, toPort: 'a' });
    h.push(p);
    p = removeNodes(p, [1]);
    expect(p.nodes).toHaveLength(9);
    expect(p.links).toHaveLength(0);
    p = h.undo(p);
    expect(p.links).toHaveLength(1);
    p = h.redo(p);
    expect(p.nodes).toHaveLength(9);
  });
  it('una entrada solo admite una conexión', () => {
    let p = addNode(addNode(addNode(emptyPatch(), def('a'), 0, 0), def('a'), 0, 0), def('a'), 0, 0);
    p = connect(p, { from: 1, fromPort: 'out', to: 3, toPort: 'a' });
    p = connect(p, { from: 2, fromPort: 'out', to: 3, toPort: 'a' });
    expect(p.links).toEqual([{ from: 2, fromPort: 'out', to: 3, toPort: 'a' }]);
  });
  it('rechaza tipos incompatibles con la misma regla del motor', () => {
    expect(linkError('float', 'int')).toBe('');
    expect(linkError('float', 'string')).toBe('');
    expect(linkError('texture', 'float')).not.toBe('');
    expect(linkError('color', 'float')).not.toBe('');
  });
  it('ocultar todo respeta los puertos conectados y mostrar todo los recupera', () => {
    let p = addNode(addNode(emptyPatch(), def('a'), 0, 0), def('a'), 0, 0);
    p = connect(p, { from: 1, fromPort: 'out', to: 2, toPort: 'a' });
    const defs = new Map([['a', def('a')]]);
    p = setHidden(p, defs, [1, 2], true);
    expect(p.nodes[1].hidden).toEqual(['b']);
    p = setHidden(p, defs, [1, 2], false);
    expect(p.nodes[1].hidden).toEqual([]);
  });
  it('duplicar da ids nuevos y busca por descripción', () => {
    const p = addNode(emptyPatch(), def('a'), 0, 0);
    expect(duplicate(p, [1]).ids).toEqual([2]);
    expect(search([def('a')], 'SUMA')).toHaveLength(1);
    expect(search([def('a')], 'nada')).toHaveLength(0);
  });
});
