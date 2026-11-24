import { useEffect } from 'react';
import { useEngine } from '@vj/api-client';
import { useSelection } from '@vj/panels-core';

// Órdenes del menú nativo de la aplicación.
export function useMenu() {
  const engine = useEngine();
  const selection = useSelection();
  useEffect(() => window.vjHost.onMenu(async (id) => {
    const documents = String(engine.value('/project/folder') ?? '');
    const folder = `${documents}/Compositions`;
    const dirty = Boolean(engine.value('/project/dirty'));
    const safe = (fn: () => Promise<unknown>) => fn().catch((e: Error) => window.vjHost.confirm('No se pudo completar', e.message));
    const keep = () => !dirty || window.vjHost.confirm('Hay cambios sin guardar', '¿Descartarlos?');
    const saveAs = async () => {
      const path = await window.vjHost.pickSavePath(folder, String(engine.value('/project/name') ?? 'Sin título'));
      if (path) await safe(() => engine.run('project.save', { path }));
    };
    const layer = Number(engine.value('/composition/activelayer') ?? 1);
    const column = Number(engine.value('/composition/selectedclip') ?? 1);
    const layers = Number(engine.value('/composition/structure/layers') ?? 1);
    const columns = Number(engine.value('/composition/structure/columns') ?? 1);
    const deck = Number(engine.value('/composition/deck') ?? 1) - 1;
    const stepDeck = (delta: number) => {
      const open = (JSON.parse(String(engine.value('/project/decks') ?? '[]')) as { index: number; closed: boolean }[]).filter((d) => !d.closed);
      const at = open.findIndex((d) => d.index === deck);
      const next = open[(at + delta + open.length) % open.length];
      return next ? safe(() => engine.run('deck.select', { index: next.index })) : Promise.resolve();
    };
    const sure = (title: string, text: string) => window.vjHost.confirm(title, text);
    switch (id) {
      case 'deck:new': await safe(() => engine.run('deck.add')); break;
      case 'deck:duplicate': await safe(() => engine.run('deck.duplicate', { index: deck })); break;
      case 'deck:next': await stepDeck(1); break;
      case 'deck:previous': await stepDeck(-1); break;
      case 'layer:new': await safe(() => engine.run('structure.addLayer')); break;
      case 'layer:insert': await safe(() => engine.run('structure.insertLayer', { layer: layer + 1 })); break;
      case 'layer:duplicate': await safe(() => engine.run('structure.duplicateLayer', { layer })); break;
      case 'layer:up': if (layer < layers) await safe(() => engine.run('structure.moveLayer', { layer, to: layer + 1 })); break;
      case 'layer:down': if (layer > 1) await safe(() => engine.run('structure.moveLayer', { layer, to: layer - 1 })); break;
      case 'layer:fold': await safe(() => engine.set(`/composition/layers/${layer}/folded`, !engine.value(`/composition/layers/${layer}/folded`))); break;
      case 'layer:clear':
        if (await sure('¿Vaciar esta capa?', 'Se quitan sus clips del deck actual. Se puede deshacer.')) await safe(() => engine.run('structure.clearLayer', { layer }));
        break;
      case 'layer:remove':
        if (await sure('¿Quitar esta capa?', 'Se elimina con sus clips en todos los decks. Se puede deshacer.')) await safe(() => engine.run('structure.removeLayer', { layer }));
        break;
      case 'column:new': await safe(() => engine.run('structure.addColumn')); break;
      case 'column:insert': await safe(() => engine.run('structure.insertColumn', { column: column + 1 })); break;
      case 'column:duplicate': await safe(() => engine.run('structure.duplicateColumn', { column })); break;
      case 'column:left': if (column > 1) await safe(() => engine.run('structure.moveColumn', { column, to: column - 1 })); break;
      case 'column:right': if (column < columns) await safe(() => engine.run('structure.moveColumn', { column, to: column + 1 })); break;
      case 'column:remove':
        if (await sure('¿Quitar esta columna?', 'Se elimina con sus clips en todos los decks. Se puede deshacer.')) await safe(() => engine.run('structure.removeColumn', { column }));
        break;
      case 'clip:clear': await safe(() => engine.run('clip.clear', { ...selection })); break;
      case 'snapshot': await safe(() => engine.run('output.snapshot')); break;
      case 'new':
        if (await keep()) await safe(() => engine.run('project.new'));
        break;
      case 'open': {
        if (!(await keep())) break;
        const path = await window.vjHost.pickComposition(folder);
        if (path) await safe(() => engine.run('project.open', { path }));
        break;
      }
      case 'save':
        if (engine.value('/project/path')) await safe(() => engine.run('project.save'));
        else await saveAs();
        break;
      case 'saveAs':
        await saveAs();
        break;
      case 'disable':
        await safe(() => engine.set('/output/disabled', !engine.value('/output/disabled')));
        break;
      case 'testcard':
        await safe(() => engine.set('/output/testcard', !engine.value('/output/testcard')));
        break;
      case 'identify':
        await safe(() => engine.set('/output/identify', !engine.value('/output/identify')));
        break;
      case 'undo':
        await safe(() => engine.run('history.undo'));
        break;
      case 'redo':
        await safe(() => engine.run('history.redo'));
        break;
      case 'copy':
        await safe(() => engine.run('clip.copy', { ...selection }));
        break;
      case 'cut':
        await safe(() => engine.run('clip.cut', { ...selection }));
        break;
      case 'paste':
        await safe(() => engine.run('clip.paste', { ...selection }));
        break;
    }
  }), [engine, selection]);
}
