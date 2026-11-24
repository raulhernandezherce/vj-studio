import type { ReactNode } from 'react';

export function Slider(props: {
  label: string; value: number; min: number; max: number; step?: number;
  onChange: (v: number) => void; onReset?: () => void; format?: (v: number) => string; testId?: string;
}) {
  const { label, value, min, max, step = 0.01, onChange, onReset, format = (v) => v.toFixed(2), testId } = props;
  return (
    <label className="slider" onContextMenu={(e) => { if (onReset) { e.preventDefault(); onReset(); } }}>
      <span>{label}</span>
      <input type="range" data-testid={testId} min={min} max={max} step={step} value={value}
        onChange={(e) => onChange(Number(e.target.value))} />
      <output>{format(value)}</output>
    </label>
  );
}

export function Toggle(props: { label: string; on: boolean; onChange: (v: boolean) => void; testId?: string; title?: string }) {
  return (
    <button type="button" data-testid={props.testId} title={props.title} className={props.on ? 'toggle on' : 'toggle'}
      aria-pressed={props.on} onClick={() => props.onChange(!props.on)}>
      {props.label}
    </button>
  );
}

export function Row({ children }: { children: ReactNode }) {
  return <div className="row">{children}</div>;
}
