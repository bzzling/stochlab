import { useCallback, useEffect, useRef, useState } from 'react';
import {
  defaults,
  fieldInfo,
  format,
  models,
  parametersFor,
  type Params,
  type Simulation,
} from '../lib/models';
import { Engine } from '../lib/engine';
import { parseCommand } from '../lib/commands';
import { Scope, Histogram, EventRaster, Occupation, QV } from './Plots';
import { CommandBar } from './CommandBar';
import { plotCrossing } from '../lib/reflection';

export default function Workbench({
  modelId = 'first-passage',
}: {
  modelId?: string;
}) {
  const model = models.find((m) => m.id === modelId) ?? models[0];
  const [params, setParams] = useState<Params>(() => parametersFor(modelId));
  const [used, setUsed] = useState<Params | null>(null);
  const [result, setResult] = useState<Simulation | null>(null);
  const [status, setStatus] = useState('LOADING ENGINE');
  const [busy, setBusy] = useState(true),
    [error, setError] = useState('');
  const [selected, setSelected] = useState(0),
    [reflect, setReflect] = useState(modelId === 'reflection'),
    [maximum, setMaximum] = useState(false);
  const [help, setHelp] = useState(false),
    [logs, setLogs] = useState<string[]>([]);
  const [qText, setQText] = useState(JSON.stringify(defaults.Q));
  const engine = useRef<Engine | null>(null),
    locked = useRef(false);
  const isBarrier = ['brownian', 'first-passage', 'reflection'].includes(
    modelId,
  );
  const isEvent = ['poisson', 'compound-poisson', 'hawkes', 'ctmc'].includes(
    modelId,
  );
  const isHawkes = modelId === 'hawkes';
  const dirty =
    used !== null && JSON.stringify(params) !== JSON.stringify(used);
  const run = useCallback(
    async (p: Params) => {
      if (!engine.current || locked.current) return;
      locked.current = true;
      setBusy(true);
      setError('');
      setStatus('COMPUTING');
      try {
        const next = await engine.current.call<Simulation>({
          action: 'simulate',
          model: modelId,
          ...p,
        });
        setResult(next);
        setUsed({ ...p });
        setSelected(
          Math.max(
            0,
            next.series.findIndex((s) => s.hit != null),
          ),
        );
        setStatus('WASM READY');
        setLogs((logs) =>
          [
            `${modelId} · seed ${next.seed} · ${next.paths.toLocaleString()} realizations · ${format(next.elapsed_ms, 1)} ms`,
            ...logs,
          ].slice(0, 5),
        );
      } catch (e) {
        setError(e instanceof Error ? e.message : String(e));
        setStatus('REQUEST FAILED');
      } finally {
        locked.current = false;
        setBusy(false);
      }
    },
    [modelId],
  );
  useEffect(() => {
    const current = new Engine();
    engine.current = current;
    run(parametersFor(modelId));
    return () => {
      engine.current = null;
      current.close();
    };
  }, [modelId, run]);
  const reseed = useCallback(() => {
    const seed = crypto.getRandomValues(new Uint32Array(1))[0];
    const next = { ...params, seed };
    setParams(next);
    run(next);
  }, [params, run]);
  useEffect(() => {
    const key = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement;
      if (
        target.matches('input,textarea,select,button') ||
        e.metaKey ||
        e.ctrlKey ||
        e.altKey
      )
        return;
      if (e.key === ' ') {
        e.preventDefault();
        run(params);
      }
      if (e.key.toLowerCase() === 'r') reseed();
      if (e.key === ':' || e.key === '/') {
        e.preventDefault();
        document.getElementById('command-input')?.focus();
      }
      if (e.key === '?') {
        e.preventDefault();
        setHelp((h) => !h);
      }
      if (e.key === 'Escape') setHelp(false);
    };
    window.addEventListener('keydown', key);
    return () => window.removeEventListener('keydown', key);
  }, [params, run, reseed]);
  function command(text: string) {
    try {
      const c = parseCommand(text);
      setError('');
      if (c.type === 'run') run(params);
      else if (c.type === 'reseed') reseed();
      else if (c.type === 'help') setHelp(true);
      else if (c.type === 'set') setParams((p) => ({ ...p, [c.key]: c.value }));
      else if (c.type === 'model') {
        const m = models.find((m) => m.id === c.model)!;
        window.location.assign(`/${m.group.toLowerCase()}/${m.id}/`);
      } else
        throw new Error(
          'Exchange commands are available in Systems → Exchange.',
        );
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }
  const fields = [
    'paths',
    'T',
    ...(!isEvent ? ['steps'] : []),
    ...model.fields,
    ...(modelId === 'brownian' ? ['barrier'] : []),
  ];
  function crossing() {
    if (!result) return;
    for (let i = 1; i <= result.series.length; i++) {
      const j = (selected + i) % result.series.length;
      if (result.series[j].hit != null) {
        setSelected(j);
        return;
      }
    }
    setError(
      'None of the displayed paths cross this barrier. Reseed or lower the barrier.',
    );
  }
  const path = result?.series[selected];
  const hitting = result?.metrics.find((m) => m.key === 'hit_probability');
  return (
    <div className="lab-island">
      <div className="experiment-heading">
        <div>
          <div className="eyebrow">
            {model.group.toUpperCase()} / {model.code}
          </div>
          <h1>{model.name}</h1>
          <p>{model.description}</p>
        </div>
        <div
          className={`engine-status ${busy ? 'working' : ''}`}
          data-testid="engine-status"
        >
          <i />
          {status}
        </div>
      </div>
      {error && (
        <div role="alert" className="error-banner">
          <span>{error}</span>
          <button onClick={() => setError('')} aria-label="Dismiss error">
            ×
          </button>
        </div>
      )}
      {['first-passage', 'reflection'].includes(modelId) && (
        <div className="experiment-summary">
          <div>
            <span>EMPIRICAL · P(τₐ ≤ T)</span>
            <strong>{format(hitting?.value, 5)}</strong>
          </div>
          <div>
            <span>REFLECTION PRINCIPLE</span>
            <strong className="theory">{format(hitting?.theory, 5)}</strong>
          </div>
          <div>
            <span>ABSOLUTE ERROR</span>
            <strong>
              {format(
                hitting?.theory !== undefined
                  ? Math.abs(hitting.value - hitting.theory)
                  : undefined,
                5,
              )}
            </strong>
          </div>
          <div>
            <span>MONTE CARLO SE</span>
            <strong>{format(hitting?.se, 5)}</strong>
          </div>
        </div>
      )}
      <div className="workbench-grid">
        <section className="plot-pane">
          <div className="pane-heading">
            <span>
              {isHawkes ? 'CONDITIONAL INTENSITY' : 'SAMPLE TRAJECTORIES'}
            </span>
            <div className="plot-options">
              {isBarrier && (
                <button
                  className={reflect ? 'active' : ''}
                  onClick={() => {
                    setReflect(!reflect);
                    setMaximum(false);
                  }}
                  aria-pressed={reflect}
                >
                  Reflect tail
                </button>
              )}
              {modelId === 'brownian' && (
                <button
                  className={maximum ? 'active' : ''}
                  onClick={() => {
                    setMaximum(!maximum);
                    setReflect(false);
                  }}
                  aria-pressed={maximum}
                >
                  Running maximum
                </button>
              )}
              <span className="micro muted">
                {result
                  ? `${result.series.length} / ${result.paths.toLocaleString()} paths`
                  : 'AWAITING ENGINE'}
              </span>
            </div>
          </div>
          {result ? (
            <Scope
              series={result.series}
              T={result.T}
              selected={selected}
              onSelect={setSelected}
              barrier={isBarrier ? used?.barrier : undefined}
              reflect={reflect}
              maximum={maximum}
              step={isEvent && !isHawkes}
              intensity={isHawkes}
              label={`${model.name} ${isHawkes ? 'intensity' : 'trajectory'} plot`}
            />
          ) : (
            <div className="loading-scope">
              <span className="loading-cross">+</span>
              <p>Initializing the simulation engine</p>
              <small>C++20 → WebAssembly</small>
            </div>
          )}
          <div className="plot-legend">
            <span>
              <i className="legend-line" /> selected path
            </span>
            <span>
              <i className="legend-line faint" /> ensemble
            </span>
            {isBarrier && (
              <span>
                <i className="legend-line amber" /> barrier
              </span>
            )}
            {reflect && (
              <span>
                <i className="legend-line coral" /> reflected
              </span>
            )}
            <span className="muted legend-end">
              {isHawkes
                ? 'EXACT EVENTS · EXPONENTIAL DECAY'
                : 'CLICK A PATH TO INSPECT'}
            </span>
          </div>
          <div className="path-inspector">
            <label>
              PATH{' '}
              <select
                aria-label="Selected path"
                value={selected}
                onChange={(e) => setSelected(Number(e.target.value))}
              >
                {(result?.series ?? []).map((s, i) => (
                  <option value={i} key={i}>
                    {String(i + 1).padStart(2, '0')}
                    {s.hit != null ? ' · crossing' : ''}
                  </option>
                ))}
              </select>
            </label>
            <span>
              {maximum ? 'grid maximum' : 'terminal'}{' '}
              <b>
                {format(maximum && path ? Math.max(...path.y) : path?.y.at(-1))}
              </b>
            </span>
            {isBarrier && (
              <>
                <span>
                  plot hit{' '}
                  <b>
                    {path?.hit != null
                      ? format(
                          plotCrossing(path, used?.barrier ?? 1) ?? undefined,
                          3,
                        )
                      : '—'}
                  </b>
                </span>
                <button onClick={crossing} disabled={!result}>
                  Next crossing →
                </button>
              </>
            )}
          </div>
        </section>
        <aside className="parameter-pane">
          <div className="pane-heading">
            <span>PARAMETERS</span>
            <span className="micro muted">{dirty ? 'MODIFIED' : 'CONFIG'}</span>
          </div>
          <form
            onSubmit={(e) => {
              e.preventDefault();
              run(params);
            }}
          >
            {fields.map((key) => (
              <label className="parameter" key={key}>
                <span>{fieldInfo[key].label}</span>
                <input
                  aria-label={fieldInfo[key].label}
                  type="number"
                  step={
                    ['paths', 'steps'].includes(key) ||
                    (key === 'x0' && modelId === 'ctmc')
                      ? 1
                      : 'any'
                  }
                  min={key === 'mu' && isHawkes ? 0.01 : fieldInfo[key].min}
                  max={fieldInfo[key].max}
                  value={params[key] as number}
                  onChange={(e) =>
                    setParams((p) => ({ ...p, [key]: Number(e.target.value) }))
                  }
                />
              </label>
            ))}
            {modelId === 'ctmc' && (
              <label className="parameter matrix-input">
                <span>Generator · Q (JSON)</span>
                <textarea
                  aria-label="Generator matrix Q"
                  rows={4}
                  value={qText}
                  onChange={(e) => {
                    setQText(e.target.value);
                    try {
                      const Q = JSON.parse(e.target.value);
                      setParams((p) => ({ ...p, Q }));
                      setError('');
                    } catch {
                      setParams((p) => ({ ...p, Q: [] }));
                      setError(
                        'Enter a valid JSON generator matrix before running.',
                      );
                    }
                  }}
                />
                <small>Rows sum to 0. Off-diagonal rates ≥ 0.</small>
              </label>
            )}
            <label className="parameter seed-field">
              <span>RNG seed</span>
              <div>
                <input
                  aria-label="RNG seed"
                  type="number"
                  min="0"
                  max="4294967295"
                  step="1"
                  value={params.seed}
                  onChange={(e) =>
                    setParams((p) => ({ ...p, seed: Number(e.target.value) }))
                  }
                />
                <button
                  type="button"
                  onClick={reseed}
                  disabled={busy}
                  title="Reseed and run (R)"
                  aria-label="Reseed and run"
                >
                  ↻
                </button>
              </div>
            </label>
            <button type="submit" className="run-button" disabled={busy}>
              {busy ? 'COMPUTING…' : 'RUN EXPERIMENT'}
              <kbd>↵</kbd>
            </button>
            <p className="micro muted">
              {dirty
                ? 'Parameters changed. Run to update results.'
                : 'Same seed + parameters → same experiment.'}
            </p>
          </form>
          <div className="model-equation">
            <span className="eyebrow">MODEL</span>
            <div>{model.formula}</div>
            {result?.diagnostics?.map((d) => (
              <p className="model-diagnostic" key={d.key}>
                <span>{d.label}</span>
                <b>{format(d.value, 3)}</b>
              </p>
            ))}
            <a href={`/methods/#${modelId}`}>Method & assumptions ↗</a>
          </div>
        </aside>
        <section className="validation-pane">
          <div className="pane-heading">
            <span>MONTE CARLO / VALIDATION</span>
            <span className="micro muted">
              {result ? `N = ${result.paths.toLocaleString()}` : '—'}
            </span>
          </div>
          <div className="validation-table-wrap">
            <table className="validation-table">
              <thead>
                <tr>
                  <th>QUANTITY</th>
                  <th>EMPIRICAL</th>
                  <th>THEORY</th>
                </tr>
              </thead>
              <tbody>
                {result?.metrics.map((m) => (
                  <tr key={m.key}>
                    <th scope="row" title={m.note}>
                      {m.label}
                      {m.se !== undefined && (
                        <small>SE {format(m.se, 5)}</small>
                      )}
                    </th>
                    <td>{format(m.value)}</td>
                    <td className="theory">{format(m.theory)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          {result && (
            <div className="computation-note">
              <span>
                {format(result.elapsed_ms, 1)} ms <small>ENGINE</small>
              </span>
              <button
                onClick={() => {
                  const url = URL.createObjectURL(
                    new Blob(
                      [JSON.stringify({ parameters: used, result }, null, 2)],
                      { type: 'application/json' },
                    ),
                  );
                  const a = document.createElement('a');
                  a.href = url;
                  a.download = `stochlab-${modelId}-${result.seed}.json`;
                  a.click();
                  URL.revokeObjectURL(url);
                }}
              >
                Export result ↓
              </button>
            </div>
          )}
        </section>
        <section className="distribution-pane">
          <div className="pane-heading">
            <span>
              {modelId === 'ctmc'
                ? 'OCCUPATION / STATIONARY LAW'
                : modelId === 'qv'
                  ? 'NESTED PARTITIONS'
                  : isEvent
                    ? 'EVENT RASTER'
                    : 'TERMINAL DISTRIBUTION'}
            </span>
            <span className="micro muted">
              {isEvent ? 'EVENT TIME' : 'EMPIRICAL'}
            </span>
          </div>
          {result &&
            (modelId === 'ctmc' ? (
              <Occupation result={result} />
            ) : modelId === 'qv' ? (
              <QV result={result} />
            ) : isEvent ? (
              <EventRaster series={result.series} T={result.T} />
            ) : (
              <Histogram result={result} />
            ))}
          {result && (
            <p className="distribution-note">
              {modelId === 'qv'
                ? 'One path, nested observations. Convergence need not be monotone.'
                : modelId === 'ctmc'
                  ? 'Occupation uses actual holding times, including the final interval.'
                  : isEvent
                    ? 'Each tick is a simulated arrival. Rows are independent realizations.'
                    : 'All realizations contribute to the histogram; only a subset is drawn above.'}
            </p>
          )}
        </section>
      </div>
      <div className="method-note">
        <span>METHOD</span>
        <p>
          {result?.algorithm ?? 'Loading WebAssembly…'}
          {isBarrier
            ? ' The crossing marker interpolates the displayed polyline; Monte Carlo maximum probabilities include unobserved bridge crossings.'
            : ''}
        </p>
      </div>
      {result?.warnings.map((w, i) => (
        <div className="warning-note" key={i}>
          NOTE / {w}
        </div>
      ))}
      <details className="experiment-log">
        <summary>
          EXPERIMENT LOG <span>{logs.length} runs</span>
        </summary>
        {logs.map((log, i) => (
          <p key={i}>
            <span>{String(logs.length - i).padStart(2, '0')}</span>
            {log}
          </p>
        ))}
      </details>
      <CommandBar onCommand={command} help={help} setHelp={setHelp} />
    </div>
  );
}
