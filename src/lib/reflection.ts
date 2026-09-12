import type { Series } from './models.ts';

// Geometry of the displayed polyline, not an estimate of an unobserved bridge hit.
export function plotCrossing(path: Series, barrier: number): number | null {
  const k = path.hit;
  if (k == null || k < 0 || k >= path.t.length) return null;
  if (k === 0) return path.t[0];
  const fraction = (barrier - path.y[k - 1]) / (path.y[k] - path.y[k - 1]);
  return path.t[k - 1] + fraction * (path.t[k] - path.t[k - 1]);
}

export function reflectedPolyline(path: Series, barrier: number): Series {
  const crossing = plotCrossing(path, barrier);
  if (crossing === null || path.hit == null) return path;
  const k = path.hit;
  return {
    t: [...path.t.slice(0, k), crossing, ...path.t.slice(k)],
    y: [
      ...path.y.slice(0, k),
      barrier,
      ...path.y.slice(k).map((y) => 2 * barrier - y),
    ],
  };
}
