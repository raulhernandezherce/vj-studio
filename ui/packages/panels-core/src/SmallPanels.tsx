import { useParam } from '@vj/api-client';
import { ParamForm } from './ParamForm';
import { clipPath } from './paths';
import { clock } from './paths.extra';
import { useSelection } from './selection';

export function NotesPanel() {
  const [notes, setNotes] = useParam('/composition/notes', '');
  return <textarea className="notes" aria-label="Notas" placeholder="Notas de esta composición" value={notes} onChange={(e) => setNotes(e.target.value)} />;
}

export function ClipTimePanel() {
  const { layer, column } = useSelection();
  const path = clipPath(layer, column);
  const [position] = useParam(`${path}/transport/position`, 0);
  const [duration] = useParam(`${path}/duration`, 0);
  const [live] = useParam(`${path}/connected`, false);
  return (
    <div className={live ? 'clip-time live' : 'clip-time'}>
      <b>{clock(position * duration)}</b>
      <span>{duration > 0 ? `queda ${clock((1 - position) * duration)}` : 'sin duración'}</span>
    </div>
  );
}

export function PreferencesPanel() {
  return (
    <>
      <p className="hint">Se guardan al momento y valen para todas las composiciones.</p>
      <ParamForm prefix="/preferences" />
    </>
  );
}
