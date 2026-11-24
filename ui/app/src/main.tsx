import { createRoot } from 'react-dom/client';
import { EngineClient, EngineContext } from '@vj/api-client';
import '@vj/panels-core';
import { AdvancedOutput } from '@vj/advanced-output';
import { NodeEditor } from '@vj/node-editor';
import { App } from './App';
import './styles.css';

const engine = new EngineClient({ url: () => window.vjHost.engineUrl() });
void engine.start();

createRoot(document.getElementById('root')!).render(
  <EngineContext.Provider value={engine}>
    {location.hash === '#advanced' ? <AdvancedOutput /> : location.hash === '#nodes' ? <NodeEditor /> : <App />}
  </EngineContext.Provider>,
);
