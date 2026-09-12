import { useCallback, useEffect, useRef, useState } from 'react';
import { Engine } from '../lib/engine';
import { parseCommand } from '../lib/commands';
import { format, type ExchangeState } from '../lib/models';
import { Scope } from './Plots';
import { CommandBar } from './CommandBar';

const initial = {
  seed: 48291,
  flow: 'poisson' as 'poisson' | 'hawkes',
  maker: true,
  rate: 20,
  alpha: 0.7,
  beta: 1.2,
  skew: 0.05,
};
export default function Exchange() {
  const [config, setConfig] = useState(initial),
    [applied, setApplied] = useState(initial);
  const [data, setData] = useState<ExchangeState | null>(null),
    [running, setRunning] = useState(false),
    [ready, setReady] = useState(false);
  const [speed, setSpeed] = useState(1),
    [help, setHelp] = useState(false),
    [error, setError] = useState('');
  const engine = useRef<Engine | null>(null),
    locked = useRef(false),
    epoch = useRef(0);
  const reset = useCallback(async (next: typeof initial) => {
    const generation = ++epoch.current;
    setRunning(false);
    setReady(false);
    setError('');
    try {
      const snapshot = await engine.current?.call<ExchangeState>({
        action: 'exchange-init',
        ...next,
      });
      if (snapshot && generation === epoch.current) {
        setData(snapshot);
        setApplied(next);
        setReady(true);
      }
    } catch (e) {
      if (generation === epoch.current) {
        setError(e instanceof Error ? e.message : String(e));
        setReady(true);
      }
    }
  }, []);
  useEffect(() => {
    const e = new Engine();
    engine.current = e;
    reset(initial);
    return () => {
      engine.current = null;
      e.close();
    };
  }, [reset]);
  const advance = useCallback(
    async (events: number) => {
      if (locked.current || !engine.current || !ready) return;
      locked.current = true;
      const generation = epoch.current;
      try {
        const d = await engine.current.call<ExchangeState>({
          action: 'exchange-step',
          events,
        });
        if (generation === epoch.current) setData(d);
      } catch (e) {
        setRunning(false);
        setError(e instanceof Error ? e.message : String(e));
      } finally {
        locked.current = false;
      }
    },
    [ready],
  );
  useEffect(() => {
    if (!running) return;
    const id = setInterval(() => advance(8 * speed), 120);
    return () => clearInterval(id);
  }, [running, speed, advance]);
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
        if (ready) setRunning((r) => !r);
      }
      if (e.key.toLowerCase() === 'r') reset(config);
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
  }, [ready, reset, config]);
  function command(text: string) {
    try {
      const c = parseCommand(text);
      setError('');
      if (c.type === 'help') setHelp(true);
      else if (c.type === 'exchange') {
        if (c.action === 'reset') reset(config);
        else if (ready) setRunning(c.action === 'start');
      } else if (c.type === 'flow') {
        const next = { ...config, flow: c.value };
        setConfig(next);
        reset(next);
      } else if (c.type === 'maker') {
        const next = { ...config, maker: c.value };
        setConfig(next);
        reset(next);
      } else if (c.type === 'speed') setSpeed(c.value);
      else if (c.type === 'set' && c.key === 'seed') {
        const next = { ...config, seed: c.value };
        setConfig(next);
        reset(next);
      } else
        throw new Error(
          'Use exchange start, exchange pause, flow hawkes, maker enable, or speed 4x.',
        );
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  }
  const depthMax = Math.max(
    ...(data?.bids ?? []).map((l) => l.quantity),
    ...(data?.asks ?? []).map((l) => l.quantity),
    1,
  );
  const dirty = JSON.stringify(config) !== JSON.stringify(applied);
  const history = data?.history ?? [];
  const start = history[0]?.time ?? 0;
  const t = history.map((p) => p.time - start),
    T = Math.max(0.01, (history.at(-1)?.time ?? 1) - start);
  const midSeries = [{ t, y: history.map((p) => p.mid) }],
    intensitySeries = [{ t, y: history.map((p) => p.intensity) }];
  function level(
    l: { price: number; quantity: number },
    side: string,
    i: number,
  ) {
    return (
      <div className={`depth-row ${side}`} key={`${side}-${i}`}>
        <i style={{ width: `${(l.quantity / depthMax) * 100}%` }} />
        <span>{format(l.price, 2)}</span>
        <span>{l.quantity.toLocaleString()}</span>
      </div>
    );
  }
  return (
    <div className="lab-island exchange-island">
      <div className="experiment-heading">
        <div>
          <div className="eyebrow">SYSTEMS / 12</div>
          <h1>Synthetic exchange</h1>
          <p>
            A limit order book driven by stochastic events. Observe liquidity,
            matching, and a quoting agent.
          </p>
        </div>
        <div
          className={`engine-status ${running ? 'working' : ''}`}
          data-testid="engine-status"
        >
          <i />
          {!ready
            ? 'LOADING ENGINE'
            : running
              ? 'WASM · RUNNING'
              : 'WASM · PAUSED'}
        </div>
      </div>
      {error && (
        <div role="alert" className="error-banner">
          {error}
          <button onClick={() => setError('')} aria-label="Dismiss error">
            ×
          </button>
        </div>
      )}
      <div className="exchange-toolbar">
        <div className="transport">
          <button
            className="run-button"
            disabled={!ready}
            onClick={() => setRunning(!running)}
          >
            {running ? 'Ⅱ PAUSE' : '▶ START'}
            <kbd>SPACE</kbd>
          </button>
          <button disabled={!ready || running} onClick={() => advance(1)}>
            Step +1
          </button>
          <button disabled={!ready} onClick={() => reset(config)}>
            Reset
          </button>
        </div>
        <label>
          SPEED{' '}
          <select
            aria-label="Simulation speed"
            value={speed}
            onChange={(e) => setSpeed(Number(e.target.value))}
          >
            {[1, 2, 4, 8].map((n) => (
              <option key={n} value={n}>
                {n}×
              </option>
            ))}
          </select>
        </label>
        <span className="micro muted">
          SIMULATION CLOCK <b>{format(data?.time, 2)} s</b>
        </span>
      </div>
      <div className="market-stats">
        {[
          ['MID-PRICE', format(data?.mid, 3)],
          ['SPREAD', format(data?.spread, 3)],
          ['IMBALANCE', format(data?.imbalance, 3)],
          ['INTENSITY', format(data?.intensity, 2)],
          ['EVENTS', data?.events.toLocaleString() ?? '—'],
        ].map(([label, value]) => (
          <div key={label}>
            <span>{label}</span>
            <strong data-testid={label.toLowerCase()}>{value}</strong>
          </div>
        ))}
      </div>
      <div className="exchange-grid">
        <section className="book-pane">
          <div className="pane-heading">
            <span>ORDER BOOK</span>
            <span className="micro muted">TICK .01</span>
          </div>
          <div className="depth-label">
            <span>PRICE</span>
            <span>QUANTITY</span>
          </div>
          <div className="ask-depth">
            {data?.asks
              .slice(0, 8)
              .reverse()
              .map((l, i) => level(l, 'ask', i))}
          </div>
          <div className="mid-divider">
            <span>MID</span>
            <strong>{format(data?.mid, 3)}</strong>
            <small>↔ {format(data?.spread, 2)}</small>
          </div>
          <div className="bid-depth">
            {data?.bids.slice(0, 8).map((l, i) => level(l, 'bid', i))}
          </div>
          <div className="book-key">
            <span className="coral-text">ASK / SELL</span>
            <span className="teal-text">BID / BUY</span>
          </div>
        </section>
        <section className="price-pane">
          <div className="pane-heading">
            <span>MID-PRICE</span>
            <span className="micro muted">EVENT TIME</span>
          </div>
          <Scope
            series={midSeries}
            T={T}
            height={285}
            label="Exchange mid-price history"
            yLabel="mid"
            timeOffset={start}
            includeZero={false}
            step
          />
          <div className="pane-heading subheading">
            <span>ARRIVAL INTENSITY</span>
            <span className="micro muted">{applied.flow.toUpperCase()}</span>
          </div>
          <Scope
            series={intensitySeries}
            T={T}
            height={155}
            label="Exchange event intensity history"
            yLabel="λ(t)"
            timeOffset={start}
          />
        </section>
        <section className="tape-pane">
          <div className="pane-heading">
            <span>EVENT TAPE</span>
            <span className="micro muted">LATEST FIRST</span>
          </div>
          <div className="tape-scroll">
            <table className="tape-table">
              <thead>
                <tr>
                  <th>TIME</th>
                  <th>EVENT</th>
                  <th>QTY</th>
                </tr>
              </thead>
              <tbody>
                {data?.tape.slice(0, 20).map((e, i) => (
                  <tr key={`${data.events}-${i}`}>
                    <td>{format(e.time, 2)}</td>
                    <td
                      className={e.side === 'buy' ? 'teal-text' : 'coral-text'}
                      title={`${e.side} @ ${format(e.price, 2)}`}
                    >
                      {e.type}
                    </td>
                    <td>{e.quantity}</td>
                  </tr>
                ))}
              </tbody>
            </table>
            {!data?.tape.length && (
              <p className="empty-note">
                Book initialized.
                <br />
                Start the event clock or step through one arrival.
              </p>
            )}
          </div>
          <div className="pane-heading subheading">
            <span>RECENT TRADES</span>
          </div>
          <div className="recent-trades">
            {data?.trades.slice(0, 4).map((e, i) => (
              <div key={i}>
                <span className={e.side === 'buy' ? 'teal-text' : 'coral-text'}>
                  {e.side.toUpperCase()}
                </span>
                <span>{format(e.price, 2)}</span>
                <span>× {e.quantity}</span>
              </div>
            ))}
            {!data?.trades.length && (
              <p className="empty-note">No trades yet.</p>
            )}
          </div>
        </section>
      </div>
      <div className="maker-config-grid">
        <section className="maker-pane">
          <div className="pane-heading">
            <span>MARKET-MAKING AGENT</span>
            <span className="micro muted">
              {data?.maker.enabled ? 'QUOTING' : 'DISABLED'}
            </span>
          </div>
          <div className="maker-stats">
            {[
              ['INVENTORY', format(data?.maker.inventory, 0)],
              ['CASH', format(data?.maker.cash, 2)],
              ['MARK-TO-MARKET P&L', format(data?.maker.pnl, 2)],
              ['FILLS', format(data?.maker.fills, 0)],
              ['MAX |INVENTORY|', format(data?.maker.max_inventory, 0)],
              ['SPREAD CAPTURE', format(data?.maker.spread_capture, 3)],
            ].map(([label, value]) => (
              <div key={label}>
                <span>{label}</span>
                <strong>{value}</strong>
              </div>
            ))}
          </div>
          <p className="distribution-note">
            Quotes shift against inventory. Fills come from actual resting
            orders. P&amp;L = cash + inventory × mid-price. No fees, latency, or
            adverse-selection model.{' '}
            <a href="/methods/#exchange">Read assumptions ↗</a>
          </p>
        </section>
        <section className="exchange-config">
          <div className="pane-heading">
            <span>ORDER FLOW / AGENT</span>
            <span className="micro muted">{dirty ? 'MODIFIED' : 'CONFIG'}</span>
          </div>
          <form
            onSubmit={(e) => {
              e.preventDefault();
              reset(config);
            }}
          >
            <div className="config-fields">
              <label>
                Arrival process
                <select
                  aria-label="Arrival process"
                  value={config.flow}
                  onChange={(e) =>
                    setConfig({
                      ...config,
                      flow: e.target.value as 'poisson' | 'hawkes',
                    })
                  }
                >
                  <option value="poisson">Poisson</option>
                  <option value="hawkes">Hawkes</option>
                </select>
              </label>
              <label>
                Baseline rate
                <input
                  aria-label="Baseline rate"
                  type="number"
                  min=".1"
                  max="100"
                  step=".1"
                  value={config.rate}
                  onChange={(e) =>
                    setConfig({ ...config, rate: Number(e.target.value) })
                  }
                />
              </label>
              <label>
                Inventory skew
                <input
                  aria-label="Inventory skew"
                  type="number"
                  min="0"
                  max="1"
                  step=".01"
                  value={config.skew}
                  onChange={(e) =>
                    setConfig({ ...config, skew: Number(e.target.value) })
                  }
                />
              </label>
              <label>
                Seed
                <input
                  aria-label="Exchange seed"
                  type="number"
                  min="0"
                  max="4294967295"
                  step="1"
                  value={config.seed}
                  onChange={(e) =>
                    setConfig({ ...config, seed: Number(e.target.value) })
                  }
                />
              </label>
              {config.flow === 'hawkes' && (
                <>
                  <label>
                    Excitation · α
                    <input
                      aria-label="Exchange excitation"
                      type="number"
                      min="0"
                      max="20"
                      step=".1"
                      value={config.alpha}
                      onChange={(e) =>
                        setConfig({ ...config, alpha: Number(e.target.value) })
                      }
                    />
                  </label>
                  <label>
                    Decay · β
                    <input
                      aria-label="Exchange decay"
                      type="number"
                      min=".1"
                      max="20"
                      step=".1"
                      value={config.beta}
                      onChange={(e) =>
                        setConfig({ ...config, beta: Number(e.target.value) })
                      }
                    />
                  </label>
                </>
              )}
            </div>
            <div className="config-footer">
              <label className="checkbox">
                <input
                  type="checkbox"
                  checked={config.maker}
                  onChange={(e) =>
                    setConfig({ ...config, maker: e.target.checked })
                  }
                />{' '}
                Enable maker
              </label>
              <button type="submit" disabled={!ready}>
                Apply &amp; reset
              </button>
            </div>
          </form>
        </section>
      </div>
      {data?.warnings.map((w, i) => (
        <div className="warning-note" key={i}>
          NOTE / {w}
        </div>
      ))}
      <div className="method-note">
        <span>MODEL</span>
        <p>
          Price-time priority · limit / market / cancellation events · integer
          price ticks · synthetic liquidity replenishment. This sandbox does not
          reproduce a real financial market.
        </p>
      </div>
      <CommandBar onCommand={command} help={help} setHelp={setHelp} exchange />
    </div>
  );
}
