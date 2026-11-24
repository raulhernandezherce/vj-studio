import { describe, expect, it } from 'vitest';
import { PanelRegistry, defaultLayout, visibleIn } from '../src/index';

const panel = (id: string, slot: 'main' | 'side', order: number) => ({
  id, title: id, milestone: 'M0', slot, order, component: () => null,
});

describe('PanelRegistry', () => {
  it('ordena por posición dentro de cada zona', () => {
    const r = new PanelRegistry();
    r.register(panel('b', 'side', 2));
    r.register(panel('a', 'side', 1));
    r.register(panel('c', 'main', 0));
    expect(r.inSlot('side').map((p) => p.id)).toEqual(['a', 'b']);
    expect(r.inSlot('main').map((p) => p.id)).toEqual(['c']);
  });

  it('rechaza ids duplicados', () => {
    const r = new PanelRegistry();
    r.register(panel('a', 'side', 1));
    expect(() => r.register(panel('a', 'main', 1))).toThrow('duplicado');
  });
});

describe('Layout', () => {
  const build = () => {
    const r = new PanelRegistry();
    r.register(panel('a', 'side', 1));
    r.register({ ...panel('b', 'side', 2), hidden: true });
    r.register({ ...panel('t', 'main', 0), fixed: true });
    return r;
  };

  it('el layout por defecto respeta los paneles ocultos y fijos', () => {
    const l = defaultLayout(build());
    expect(l.hidden).toEqual(['b']);
    expect(l.slots).toEqual({ a: 'side', b: 'side' });
  });

  it('mueve y oculta según el layout', () => {
    const r = build();
    const l = { ...defaultLayout(r), hidden: [], slots: { a: 'bottom', b: 'side' } as const };
    expect(visibleIn(l, 'bottom', r).map((p) => p.id)).toEqual(['a']);
    expect(visibleIn(l, 'side', r).map((p) => p.id)).toEqual(['b']);
  });

  it('un panel nuevo aparece aunque el layout guardado no lo conozca', () => {
    const r = build();
    const old = defaultLayout(r);
    r.register(panel('nuevo', 'side', 3));
    expect(visibleIn(old, 'side', r).map((p) => p.id)).toContain('nuevo');
  });

  it('sobrevive a JSON', () => {
    const l = defaultLayout(build());
    expect(JSON.parse(JSON.stringify(l))).toEqual(l);
  });
});
