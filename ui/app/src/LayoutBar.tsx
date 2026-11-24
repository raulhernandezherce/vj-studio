import { useEffect, useState, useSyncExternalStore } from 'react';
import { useParam } from '@vj/api-client';
import { defaultLayout, getLayout, type Layout, panels, setLayout, subscribeLayout } from '@vj/panel-registry';

const CURRENT = 'vj.layout.current';

function parse(text: string): Layout | null {
  try {
    const l = JSON.parse(text);
    return l?.version === 2 ? l : null;
  } catch {
    return null;
  }
}

// Recupera la última disposición y la guarda cada vez que cambia.
export function restoreLayout() {
  try {
    const saved = parse(localStorage.getItem(CURRENT) ?? '');
    if (saved) setLayout(saved);
  } catch { /* sin almacenamiento: se usa la disposición por defecto */ }
  subscribeLayout(() => {
    try { localStorage.setItem(CURRENT, JSON.stringify(getLayout())); } catch { /* idem */ }
  });
}

export function LayoutBar() {
  const layout = useSyncExternalStore(subscribeLayout, getLayout);
  const [documents] = useParam('/project/folder', '');
  const [names, setNames] = useState<string[]>([]);
  const [naming, setNaming] = useState(false);
  const folder = `${documents}/Presets/Interface`;
  const refresh = () => { if (documents) void window.vjHost.listLayouts(folder).then(setNames); };
  useEffect(refresh, [documents]);

  const hiddenPanels = panels.all().filter((p) => layout.hidden.includes(p.id));
  const choose = async (value: string) => {
    if (value === 'default') setLayout(defaultLayout());
    else if (value === 'save') setNaming(true);
    else if (value.startsWith('show:')) setLayout({ ...layout, hidden: layout.hidden.filter((h) => h !== value.slice(5)) });
    else if (value.startsWith('load:')) {
      const l = parse(await window.vjHost.loadLayout(folder, value.slice(5)));
      if (l) setLayout(l);
    }
  };
  if (naming) {
    return (
      <input className="layout-name" autoFocus placeholder="Nombre del layout" data-testid="layout-name"
        onBlur={() => setNaming(false)}
        onKeyDown={async (e) => {
          if (e.key === 'Escape') setNaming(false);
          if (e.key === 'Enter' && e.currentTarget.value.trim()) {
            await window.vjHost.saveLayout(folder, e.currentTarget.value.trim(), JSON.stringify(layout, null, 2));
            setNaming(false);
            refresh();
          }
        }} />
    );
  }
  return (
    <select className="layout-select" aria-label="Layout" data-testid="layout-select" value="" onChange={(e) => void choose(e.target.value)}>
      <option value="" disabled>Layout</option>
      <option value="save">Guardar layout…</option>
      <option value="default">Restablecer</option>
      {names.map((n) => <option key={n} value={`load:${n}`}>{n}</option>)}
      {hiddenPanels.map((p) => <option key={p.id} value={`show:${p.id}`}>Mostrar {p.title}</option>)}
    </select>
  );
}
