import { useEffect, useRef, useState } from 'react';
import { format, type Series, type Simulation } from '../lib/models';
import { plotCrossing, reflectedPolyline } from '../lib/reflection';

const colors = {
  grid: '#263034',
  text: '#87999c',
  trace: '#70b4ad',
  selected: '#bde9df',
  barrier: '#e4b36b',
  reflect: '#d99b8e',
};
export function Scope({
  series,
  T,
  selected = 0,
  onSelect,
  barrier,
  reflect = false,
  maximum = false,
  step = false,
  label = 'Trajectory plot',
  height = 330,
  intensity = false,
  includeZero = true,
  timeOffset = 0,
  yLabel,
}: {
  series: Series[];
  T: number;
  selected?: number;
  onSelect?: (n: number) => void;
  barrier?: number;
  reflect?: boolean;
  maximum?: boolean;
  step?: boolean;
  label?: string;
  height?: number;
  intensity?: boolean;
  includeZero?: boolean;
  timeOffset?: number;
  yLabel?: string;
}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [cursor, setCursor] = useState<{ t: number; y: number } | null>(null);
  const geometry = useRef({
    w: 800,
    h: height,
    min: -1,
    max: 1,
    left: 56,
    right: 25,
    top: 26,
    bottom: 35,
  });
  useEffect(() => {
    const node = canvas.current;
    if (!node) return;
    function draw() {
      if (!node) return;
      const w = node.clientWidth,
        h = height,
        dpr = Math.min(window.devicePixelRatio || 1, 2);
      node.width = w * dpr;
      node.height = h * dpr;
      const c = node.getContext('2d');
      if (!c) return;
      c.scale(dpr, dpr);
      let min = includeZero ? 0 : Infinity,
        max = includeZero ? 0 : -Infinity;
      for (const s of series)
        for (const y of intensity ? (s.intensity ?? []) : s.y) {
          min = Math.min(min, y);
          max = Math.max(max, y);
        }
      if (barrier !== undefined) {
        min = Math.min(min, barrier);
        max = Math.max(max, barrier);
      }
      const path = series[selected];
      if (reflect && path && barrier !== undefined && path.hit != null)
        for (let i = path.hit; i < path.y.length; i++) {
          min = Math.min(min, 2 * barrier - path.y[i]);
          max = Math.max(max, 2 * barrier - path.y[i]);
        }
      if (!Number.isFinite(min) || !Number.isFinite(max)) {
        min = 0;
        max = 1;
      }
      const span = max - min || (includeZero ? 1 : 0.02);
      min -= span * 0.12;
      max += span * 0.12;
      const left = !includeZero ? 70 : w < 420 ? 42 : 56,
        right = 36,
        top = 26,
        bottom = 35;
      geometry.current = { w, h, min, max, left, right, top, bottom };
      const X = (t: number) => left + (t / T) * (w - left - right),
        Y = (y: number) => top + ((max - y) / (max - min)) * (h - top - bottom);
      c.clearRect(0, 0, w, h);
      c.font = '11px ui-monospace, monospace';
      c.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const y = min + ((max - min) * i) / 4,
          py = Y(y);
        c.strokeStyle = colors.grid;
        c.beginPath();
        c.moveTo(left, py);
        c.lineTo(w - right, py);
        c.stroke();
        c.fillStyle = colors.text;
        c.textAlign = 'right';
        c.fillText(
          y.toFixed(max - min < 1 ? 3 : Math.abs(y) > 100 ? 0 : 1),
          left - 10,
          py + 4,
        );
      }
      for (let i = 0; i <= 5; i++) {
        const t = (T * i) / 5,
          px = X(t);
        c.strokeStyle = colors.grid;
        c.beginPath();
        c.moveTo(px, top);
        c.lineTo(px, h - bottom);
        c.stroke();
        c.fillStyle = colors.text;
        c.textAlign = 'center';
        c.fillText((t + timeOffset).toFixed(T < 2 ? 2 : 1), px, h - 12);
      }
      c.save();
      c.beginPath();
      c.rect(left, top, w - left - right, h - top - bottom);
      c.clip();
      function line(
        s: Series,
        color: string,
        width: number,
        opacity: number,
        reflected = false,
      ) {
        if (!c) return;
        const drawn =
          reflected && barrier !== undefined
            ? reflectedPolyline(s, barrier)
            : s;
        const values = intensity ? (drawn.intensity ?? []) : drawn.y;
        if (!values.length) return;
        c.strokeStyle = color;
        c.lineWidth = width;
        c.globalAlpha = opacity;
        c.beginPath();
        let peak = -Infinity;
        for (let i = 0; i < values.length; i++) {
          let y = values[i];
          if (maximum) {
            peak = Math.max(peak, y);
            y = peak;
          }
          const x = X(drawn.t[i]);
          if (i === 0) c.moveTo(x, Y(y));
          else {
            if (step) c.lineTo(x, Y(values[i - 1]));
            c.lineTo(x, Y(y));
          }
        }
        c.stroke();
        c.globalAlpha = 1;
      }
      series.forEach((s, i) => {
        if (i !== selected) line(s, colors.trace, 1, 0.25);
      });
      if (path) line(path, colors.selected, 1.7, 1);
      if (barrier !== undefined) {
        c.setLineDash([6, 5]);
        c.strokeStyle = colors.barrier;
        c.lineWidth = 1;
        c.beginPath();
        c.moveTo(left, Y(barrier));
        c.lineTo(w - right, Y(barrier));
        c.stroke();
        c.setLineDash([]);
        c.textAlign = 'right';
        c.fillStyle = colors.barrier;
        c.fillText(`a = ${barrier.toFixed(2)}`, w - right - 6, Y(barrier) - 8);
        if (
          path &&
          path.hit != null &&
          path.hit >= 0 &&
          path.hit < path.t.length
        ) {
          c.beginPath();
          c.arc(
            X(plotCrossing(path, barrier) ?? 0),
            Y(barrier),
            4,
            0,
            Math.PI * 2,
          );
          c.fill();
        }
      }
      if (reflect && path && path.hit != null) {
        c.setLineDash([5, 4]);
        line(path, colors.reflect, 2, 1, true);
        c.setLineDash([]);
      }
      c.restore();
      c.fillStyle = colors.text;
      c.textAlign = 'left';
      c.fillText(
        yLabel ?? (intensity ? 'λ(t)' : maximum ? 'M(t)' : 'X(t)'),
        left,
        14,
      );
      c.textAlign = 'right';
      c.fillText('t', w - 8, h - 12);
    }
    draw();
    const observer = new ResizeObserver(draw);
    observer.observe(node);
    return () => observer.disconnect();
  }, [
    series,
    T,
    selected,
    barrier,
    reflect,
    maximum,
    step,
    height,
    intensity,
    includeZero,
    timeOffset,
    yLabel,
  ]);
  function point(e: React.MouseEvent<HTMLCanvasElement>) {
    const r = e.currentTarget.getBoundingClientRect(),
      g = geometry.current;
    return {
      t: Math.max(
        0,
        Math.min(
          T,
          ((e.clientX - r.left - g.left) / (g.w - g.left - g.right)) * T,
        ),
      ),
      y:
        g.max -
        ((e.clientY - r.top - g.top) / (g.h - g.top - g.bottom)) *
          (g.max - g.min),
    };
  }
  return (
    <div className="scope-wrap">
      <canvas
        ref={canvas}
        style={{ height }}
        role="img"
        aria-label={label}
        onPointerMove={(e) => setCursor(point(e))}
        onPointerLeave={() => setCursor(null)}
        onClick={(e) => {
          if (!onSelect) return;
          const p = point(e);
          let best = 0,
            distance = Infinity;
          series.forEach((s, i) => {
            let j = s.t.findIndex((t) => t >= p.t);
            if (j < 0) j = s.t.length - 1;
            const value = intensity
              ? (s.intensity?.[j] ?? s.y[j])
              : maximum
                ? Math.max(...s.y.slice(0, j + 1))
                : s.y[j];
            const d = Math.abs(value - p.y);
            if (d < distance) {
              best = i;
              distance = d;
            }
          });
          onSelect(best);
        }}
      />
      {cursor && (
        <div className="scope-cursor">
          t {format(cursor.t + timeOffset, 3)} · y {format(cursor.y, 3)}
        </div>
      )}
    </div>
  );
}

export function Histogram({ result }: { result: Simulation }) {
  const bins = result.histogram;
  if (!bins.length)
    return (
      <p className="empty-note">
        No distribution returned for this experiment.
      </p>
    );
  const max = Math.max(...bins.map((b) => b.count), 1),
    w = 600,
    h = 150,
    left = 12,
    right = 12,
    bottom = 28;
  const bw = (w - left - right) / bins.length;
  return (
    <svg
      className="mini-plot"
      viewBox={`0 0 ${w} ${h}`}
      role="img"
      aria-label="Empirical terminal distribution histogram"
    >
      {[0.5, 1].map((f) => (
        <line
          key={f}
          x1={left}
          x2={w - right}
          y1={h - bottom - f * (h - bottom - 14)}
          y2={h - bottom - f * (h - bottom - 14)}
          className="plot-grid"
        />
      ))}
      {bins.map((b, i) => (
        <rect
          key={i}
          x={left + i * bw}
          y={h - bottom - (b.count / max) * (h - bottom - 14)}
          width={Math.max(1, bw - 2)}
          height={(b.count / max) * (h - bottom - 14)}
          fill="#649e98"
        >
          <title>
            {format(b.x, 3)}: {b.count} samples
          </title>
        </rect>
      ))}
      <text x={left} y={h - 8}>
        {format(bins[0].x, 2)}
      </text>
      <text x={w / 2} y={h - 8} textAnchor="middle">
        terminal value
      </text>
      <text x={w - right} y={h - 8} textAnchor="end">
        {format(bins.at(-1)?.x, 2)}
      </text>
    </svg>
  );
}
export function EventRaster({ series, T }: { series: Series[]; T: number }) {
  return (
    <svg
      className="mini-plot"
      viewBox="0 0 600 150"
      role="img"
      aria-label="Event times across six realizations"
    >
      {series.slice(0, 6).map((s, row) => (
        <g key={row}>
          <line
            x1="30"
            x2="582"
            y1={14 + row * 21}
            y2={14 + row * 21}
            className="plot-grid"
          />
          <text x="6" y={18 + row * 21}>
            {row + 1}
          </text>
          {(s.events ?? []).map((t, i) => (
            <line
              key={i}
              x1={30 + (t / T) * 552}
              x2={30 + (t / T) * 552}
              y1={8 + row * 21}
              y2={20 + row * 21}
              stroke="#9acdc4"
              strokeWidth="1.4"
            />
          ))}
        </g>
      ))}
      <text x="30" y="147">
        0
      </text>
      <text x="582" y="147" textAnchor="end">
        {format(T, 1)} s
      </text>
    </svg>
  );
}
export function Occupation({ result }: { result: Simulation }) {
  return (
    <div className="occupation">
      {result.occupation?.map((p, i) => (
        <div className="occupation-row" key={i}>
          <span>S{i}</span>
          <div className="occupation-track">
            <i style={{ width: `${p * 100}%` }} />
            {result.stationary?.[i] !== undefined && (
              <b
                style={{ left: `${result.stationary[i] * 100}%` }}
                title="Stationary theory"
              />
            )}
          </div>
          <span>{format(p * 100, 1)}%</span>
          <small>π {format(result.stationary?.[i], 3)}</small>
        </div>
      ))}
      <p className="micro muted">
        Filled: occupation fraction · marker: stationary probability
      </p>
    </div>
  );
}
export function QV({ result }: { result: Simulation }) {
  const q = result.qv ?? [];
  if (!q.length) return null;
  const max = Math.max(result.T * 1.3, ...q.map((x) => x.value)),
    x = (i: number) => 32 + (i / Math.max(1, q.length - 1)) * 536,
    y = (v: number) => 120 - (v / max) * 98;
  return (
    <svg
      className="mini-plot"
      viewBox="0 0 600 150"
      role="img"
      aria-label="Quadratic variation at nested partitions"
    >
      <line
        x1="32"
        x2="568"
        y1={y(result.T)}
        y2={y(result.T)}
        stroke="#e4b36b"
        strokeDasharray="5 5"
      />
      <polyline
        points={q.map((v, i) => `${x(i)},${y(v.value)}`).join(' ')}
        fill="none"
        stroke="#9acdc4"
        strokeWidth="2"
      />
      {q.map((v, i) => (
        <g key={i}>
          <circle cx={x(i)} cy={y(v.value)} r="3" fill="#9acdc4">
            <title>
              {v.partitions} partitions: {format(v.value)}
            </title>
          </circle>
          <text x={x(i)} y="145" textAnchor="middle">
            {v.partitions}
          </text>
        </g>
      ))}
      <text x="32" y="13">
        [B]ₜ · target {format(result.T, 2)}
      </text>
    </svg>
  );
}
