import { useEffect, useState, useSyncExternalStore } from 'react';
import { useParam } from '@vj/api-client';
import { getLayout, type Layout, type PanelDefinition, type PanelSlot, panels, setLayout, subscribeLayout, visibleIn } from '@vj/panel-registry';
import { restoreLayout } from './LayoutBar';
import { useMenu } from './menu';
import { HelpPanel } from './HelpPanel';
import { StatusBar } from './StatusBar';

const useLayout = () => useSyncExternalStore(subscribeLayout, getLayout);

function PanelBox({ panel, tabbed }: { panel: PanelDefinition; tabbed?: boolean }) {
  const { id, title, fixed, component: Panel } = panel;
  const layout = useLayout();
  const move = (slot: PanelSlot) => setLayout({ ...layout, slots: { ...layout.slots, [id]: slot } });
  return (
    <section className="panel" data-panel={id}>
      {title && !tabbed && (
        <header className="panel-head">
          <h2>{title}</h2>
          {!fixed && <PanelMenu layout={layout} id={id} move={move} />}
        </header>
      )}
      {tabbed && !fixed && <div className="panel-tools"><PanelMenu layout={layout} id={id} move={move} /></div>}
      <Panel />
    </section>
  );
}

function PanelMenu({ layout, id, move }: { layout: Layout; id: string; move: (s: PanelSlot) => void }) {
  return (
    <select className="panel-menu" aria-label="Mover panel" value={layout.slots[id]} onChange={(e) => {
      if (e.target.value === 'hide') setLayout({ ...layout, hidden: [...layout.hidden, id] });
      else move(e.target.value as PanelSlot);
    }}>
      <option value="main">Centro</option>
      <option value="side">Lateral</option>
      <option value="bottom">Inferior</option>
      <option value="hide">Ocultar</option>
    </select>
  );
}

// Lateral e inferior muestran una pestaña por panel.
function TabbedSlot({ slot }: { slot: PanelSlot }) {
  const layout = useLayout();
  const list = visibleIn(layout, slot);
  const [active, setActive] = useState('');
  const shown = list.find((p) => p.id === active) ?? list[0];
  if (!shown) return null;
  return (
    <>
      <nav className="tabs slot-tabs" role="tablist">
        {list.map((p) => (
          <button key={p.id} type="button" role="tab" aria-selected={p === shown} className={p === shown ? 'on' : ''} data-tab={p.id} onClick={() => setActive(p.id)}>{p.title}</button>
        ))}
      </nav>
      <PanelBox key={shown.id} panel={shown} tabbed />
    </>
  );
}

function Splitter({ axis }: { axis: 'x' | 'y' }) {
  return (
    <div className={`splitter ${axis}`} role="separator" onPointerDown={(e) => {
      e.preventDefault();
      const start = getLayout();
      const from = axis === 'x' ? e.clientX : e.clientY;
      const move = (m: PointerEvent) => {
        const delta = from - (axis === 'x' ? m.clientX : m.clientY);
        setLayout(axis === 'x'
          ? { ...start, sideWidth: Math.min(700, Math.max(280, start.sideWidth + delta)) }
          : { ...start, lowerHeight: Math.min(window.innerHeight - 220, Math.max(220, start.lowerHeight + delta)) });
      };
      const up = () => { window.removeEventListener('pointermove', move); window.removeEventListener('pointerup', up); };
      window.addEventListener('pointermove', move);
      window.addEventListener('pointerup', up);
    }} />
  );
}

export function App() {
  const layout = useLayout();
  const [dirty] = useParam('/project/dirty', false);
  const [name] = useParam('/project/name', 'Sin título');
  const [scale] = useParam('/preferences/uiscale', 1);
  const [confirmQuit] = useParam('/preferences/confirmquit', true);
  useMenu();
  useEffect(() => { restoreLayout(); }, []);
  useEffect(() => {
    window.vjHost.setDirty(dirty && confirmQuit);
    window.vjHost.setTitle(`${dirty ? '● ' : ''}${name} — VJ Studio`);
  }, [dirty, name, confirmQuit]);
  useEffect(() => { document.documentElement.style.fontSize = `${13 * scale}px`; }, [scale]);

  const hasBottom = visibleIn(layout, 'bottom').length > 0;
  return (
    <div className="app">
      <main className="main">
        <div className="main-panels">{visibleIn(layout, 'main').map((p) => <PanelBox key={p.id} panel={p} />)}</div>
        {hasBottom && <div className="bottom" style={{ height: layout.bottomHeight }}><TabbedSlot slot="bottom" /></div>}
      </main>
      <header className="top">{panels.inSlot('top').map((p) => <PanelBox key={p.id} panel={p} />)}</header>
      <Splitter axis="y" />
      <div className="lower" style={{ height: layout.lowerHeight, gridTemplateColumns: `300px minmax(0, 1fr) minmax(0, 1fr) 4px ${layout.sideWidth}px` }}>
        {panels.inSlot('lower').map((p) => <PanelBox key={p.id} panel={p} />)}
        <Splitter axis="x" />
        <aside className="side">
          <div className="side-tabs"><TabbedSlot slot="side" /></div>
          <HelpPanel />
        </aside>
      </div>
      <StatusBar />
    </div>
  );
}
