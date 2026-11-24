import { type ParamInfo, useEngine, useParam, useParamList } from '@vj/api-client';
import { openAnimation } from './AnimationPanel';
import { Slider, Toggle } from './controls';
import { formSections } from './paths';

export function Field({ info }: { info: ParamInfo }) {
  const engine = useEngine();
  const reset = () => void engine.reset(info.path);
  const label = info.label || info.path;
  const unit = info.unit ? ` ${info.unit}` : '';

  const body = field(info, label, unit, reset, engine);
  // El nombre arrastrado a una perilla del Dashboard la asigna a este parámetro.
  return (
    <div className="field" draggable={!info.readOnly} onDragStart={(e) => e.dataTransfer.setData('application/x-vj-param', info.path)}>
      {body}
      {!info.readOnly && info.type !== 'string' && (
        <button type="button" className="learn-open" title="Asignar a MIDI, teclado, DMX u OSC: pulsa y mueve el control" data-learn={info.path}
          onClick={() => void engine.run('shortcuts.learn', { target: info.path })}>⌁</button>
      )}
      {!info.readOnly && info.type !== 'string' && (
        <button type="button" className="anim-open" title="Animar este parámetro" data-animate={info.path}
          onClick={() => openAnimation(info.path, label)}>∿</button>
      )}
    </div>
  );
}

function field(info: ParamInfo, label: string, unit: string, reset: () => void, engine: ReturnType<typeof useEngine>) {
  if (info.type === 'trigger') {
    return <button type="button" className="field-trigger" onClick={() => void engine.trigger(info.path)}>{label}</button>;
  }
  if (info.readOnly) return <ReadOnly info={info} />;
  if (info.type === 'bool') return <BoolField info={info} />;
  if (info.type === 'enum') return <EnumField info={info} />;
  if (info.type === 'string') return <TextField info={info} />;
  return <NumberField info={info} reset={reset} label={label} unit={unit} />;
}

function ReadOnly({ info }: { info: ParamInfo }) {
  const [value] = useParam(info.path, (info.value ?? '') as string);
  const shown = typeof value === 'number' ? (Number.isInteger(value) ? value : (value as number).toFixed(2)) : String(value);
  return <div className="field-ro"><span>{info.label}</span><span>{shown}{info.unit ? ` ${info.unit}` : ''}</span></div>;
}

function BoolField({ info }: { info: ParamInfo }) {
  const [value, set] = useParam(info.path, Boolean(info.value));
  return <Toggle label={info.label || info.path} on={value} onChange={set} />;
}

function EnumField({ info }: { info: ParamInfo }) {
  const [value, set] = useParam(info.path, String(info.value ?? ''));
  return (
    <label className="field-enum">
      <span>{info.label}</span>
      <select value={value} data-path={info.path} onChange={(e) => set(e.target.value)}>
        {info.options?.map((o) => <option key={o} value={o}>{o}</option>)}
      </select>
    </label>
  );
}

function TextField({ info }: { info: ParamInfo }) {
  const [value, set] = useParam(info.path, String(info.value ?? ''));
  return (
    <label className="field-enum">
      <span>{info.label}</span>
      <input type="text" value={value} data-path={info.path} onChange={(e) => set(e.target.value)} />
    </label>
  );
}

function NumberField({ info, reset, label, unit }: { info: ParamInfo; reset: () => void; label: string; unit: string }) {
  const [value, set] = useParam(info.path, Number(info.value ?? 0));
  const span = info.max - info.min;
  const step = info.type === 'int' ? 1 : span > 100 ? 1 : span > 20 ? 0.1 : 0.01;
  return (
    <Slider label={label} value={value} min={info.min} max={info.max} step={step} onChange={set} onReset={reset}
      format={(v) => (info.type === 'int' ? String(v) : v.toFixed(span > 100 ? 0 : 2)) + unit} />
  );
}

// Formulario completo generado desde los parámetros que declara el motor.
export function ParamForm({ prefix, hidden }: { prefix: string; hidden?: readonly string[] }) {
  const params = useParamList();
  const sections = formSections(params.values(), prefix, hidden);
  return (
    <div className="form">
      {sections.map((s) => (
        <details key={s.title} open={s.title === 'General' || s.title === 'Transporte'}>
          <summary>{s.title}</summary>
          {s.fields.map((f) => <Field key={f.path} info={f} />)}
        </details>
      ))}
    </div>
  );
}
