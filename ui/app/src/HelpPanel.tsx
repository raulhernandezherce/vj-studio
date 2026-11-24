import { useEffect, useState } from 'react';

// Muestra la ayuda del control que hay bajo el ratón.
export function HelpPanel() {
  const [text, setText] = useState('');
  useEffect(() => {
    const over = (e: MouseEvent) => {
      const el = (e.target as Element).closest?.('[title]');
      const title = el?.getAttribute('title');
      if (title) setText(title);
    };
    document.addEventListener('mouseover', over);
    return () => document.removeEventListener('mouseover', over);
  }, []);
  return (
    <section className="help" data-testid="help">
      <h3>Ayuda</h3>
      <p>{text || 'Pasa el ratón por un control para saber qué hace.'}</p>
    </section>
  );
}
