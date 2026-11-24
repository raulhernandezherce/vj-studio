import { useRef, useState } from 'react';
import { useEngine, useParam } from '@vj/api-client';
import { parseDecks, PASTEL } from './paths.extra';

function Crossfader() {
  const engine = useEngine();
  const [phase, setPhase] = useParam('/composition/crossfader/phase', 0.5);
  const [behaviour] = useParam('/composition/crossfader/behaviour', 'Jump');
  const held = useRef<number | null>(null);
  const hit = (v: number) => {
    if (behaviour === 'Jump & Return') held.current = phase;
    setPhase(v);
  };
  const back = () => {
    if (held.current === null) return;
    setPhase(held.current);
    held.current = null;
  };
  return (
    <div className="crossfader" title="Crossfader: mezcla las capas del bus A y del bus B. Clic derecho para centrar."
      onContextMenu={(e) => { e.preventDefault(); void engine.reset('/composition/crossfader/phase'); }}>
      <button type="button" data-testid="cross-a" title="Salta al lado A" onPointerDown={() => hit(0)} onPointerUp={back} onPointerLeave={back}>A</button>
      <input type="range" aria-label="Crossfader" min={0} max={1} step={0.01} value={phase} onChange={(e) => setPhase(Number(e.target.value))} />
      <button type="button" data-testid="cross-b" title="Salta al lado B" onPointerDown={() => hit(1)} onPointerUp={back} onPointerLeave={back}>B</button>
      <button type="button" title="Columna anterior" onClick={() => void engine.trigger('/composition/columns/previous')}>◀</button>
      <button type="button" title="Columna siguiente" onClick={() => void engine.trigger('/composition/columns/next')}>▶</button>
    </div>
  );
}

export function DeckBar() {
  const engine = useEngine();
  const [text] = useParam('/project/decks', '[]');
  const [current] = useParam('/composition/deck', 1);
  const [renaming, setRenaming] = useState<number | null>(null);
  const [showClosed, setShowClosed] = useState(false);
  const [menu, setMenu] = useState<{ x: number; y: number; index: number } | null>(null);
  const decks = parseDecks(text);
  const open = decks.filter((d) => !d.closed);
  const closed = decks.filter((d) => d.closed);
  const run = (name: string, args: Record<string, unknown> = {}) => void engine.run(name, args);
  const close = (fn: () => void) => () => { setMenu(null); fn(); };
  const target = menu ? decks.find((d) => d.index === menu.index) : undefined;

  return (
    <div className="decks">
      <Crossfader />
      {open.map((d) => renaming === d.index ? (
        <input key={d.id} autoFocus defaultValue={d.name} aria-label="Nombre del deck"
          onBlur={(e) => { run('deck.rename', { index: d.index, name: e.target.value }); setRenaming(null); }}
          onKeyDown={(e) => { if (e.key === 'Enter') e.currentTarget.blur(); if (e.key === 'Escape') setRenaming(null); }} />
      ) : (
        <button key={d.id} type="button" data-deck={d.index} className={d.index === current - 1 ? 'deck on' : 'deck'}
          title="Clic: abrir. Doble clic: renombrar. Clic derecho: más opciones."
          style={PASTEL[d.color] ? { background: PASTEL[d.color], color: '#000' } : undefined}
          onClick={() => run('deck.select', { index: d.index })} onDoubleClick={() => setRenaming(d.index)}
          onContextMenu={(e) => { e.preventDefault(); setMenu({ x: e.clientX, y: e.clientY, index: d.index }); }}>{d.name}</button>
      ))}
      <button type="button" data-testid="deck-add" title="Nuevo deck" onClick={() => run('deck.add')}>＋</button>
      <span className="grow" />
      {closed.length > 0 && (
        <span className="decks-closed">
          <button type="button" onClick={() => setShowClosed(!showClosed)}>Cerrados ({closed.length})</button>
          {showClosed && closed.map((d) => (
            <button key={d.id} type="button" onClick={() => { run('deck.close', { index: d.index, closed: false }); setShowClosed(false); }}>{d.name}</button>
          ))}
        </span>
      )}
      {menu && target && (
        <>
          <div className="menu-veil" onClick={() => setMenu(null)} onContextMenu={(e) => { e.preventDefault(); setMenu(null); }} />
          <div className="menu" role="menu" style={{ left: menu.x, bottom: window.innerHeight - menu.y }}>
            <button type="button" onClick={close(() => run('deck.add'))}>Nuevo</button>
            <button type="button" onClick={close(() => run('deck.duplicate', { index: target.index }))}>Duplicar</button>
            <button type="button" onClick={close(() => setRenaming(target.index))}>Renombrar</button>
            <button type="button" onClick={close(async () => {
              if (await window.vjHost.confirm('¿Vaciar este deck?', 'Se quitan sus clips. Se puede deshacer.')) run('deck.clear', { index: target.index });
            })}>Vaciar</button>
            <button type="button" disabled={open.length < 2} onClick={close(() => {
              const next = open.find((d) => d.index !== target.index);
              if (next && target.index === current - 1) run('deck.select', { index: next.index });
              run('deck.close', { index: target.index, closed: true });
            })}>Cerrar</button>
            <button type="button" disabled={decks.length < 2} onClick={close(async () => {
              if (await window.vjHost.confirm('¿Quitar este deck?', 'Se elimina de la composición. Se puede deshacer.')) run('deck.remove', { index: target.index });
            })}>Quitar</button>
            <div className="menu-colors">
              {Object.keys(PASTEL).map((c) => (
                <button key={c} type="button" title={c} aria-label={`Color ${c}`} style={{ background: PASTEL[c] || 'var(--panel2)' }}
                  onClick={close(() => run('deck.color', { index: target.index, color: c }))} />
              ))}
            </div>
          </div>
        </>
      )}
    </div>
  );
}
