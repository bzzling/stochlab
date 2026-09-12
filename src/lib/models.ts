export const models = [
  {
    id: 'brownian',
    name: 'Brownian motion',
    group: 'Processes',
    code: '01',
    formula: 'dBₜ ∼ N(0, dt)',
    description:
      'Independent Gaussian increments. Continuous paths, nowhere differentiable.',
    defaults: { T: 1 },
    fields: [],
  },
  {
    id: 'random-walk',
    name: 'Random walk',
    group: 'Processes',
    code: '02',
    formula: 'Xₙ = √Δt ∑ ξᵢ',
    description: 'Symmetric ±1 increments under diffusive scaling.',
    defaults: { T: 1, paths: 2000, steps: 256 },
    fields: [],
  },
  {
    id: 'ou',
    name: 'Ornstein–Uhlenbeck',
    group: 'Processes',
    code: '03',
    formula: 'dXₜ = θ(μ − Xₜ)dt + σdBₜ',
    description: 'Mean reversion with exact Gaussian transitions.',
    defaults: { T: 10 },
    fields: ['theta', 'mu', 'sigma', 'x0'],
  },
  {
    id: 'poisson',
    name: 'Poisson',
    group: 'Processes',
    code: '04',
    formula: 'Nₜ ∼ Poisson(λt)',
    description:
      'Independent exponential waiting times. Exact event-driven simulation.',
    defaults: { T: 10, paths: 2000 },
    fields: ['lambda'],
  },
  {
    id: 'compound-poisson',
    name: 'Compound Poisson',
    group: 'Processes',
    code: '05',
    formula: 'Xₜ = ∑ᵢ₌₁ᴺᵗ Jᵢ',
    description: 'Poisson arrivals with independent Gaussian jump marks.',
    defaults: { T: 10, paths: 2000 },
    fields: ['lambda', 'jump_mean', 'jump_sigma'],
  },
  {
    id: 'ctmc',
    name: 'Markov chain',
    group: 'Processes',
    code: '06',
    formula: 'P′(t) = P(t)Q',
    description:
      'A finite-state chain with exact holding times and a configurable generator.',
    defaults: { T: 50, paths: 1000 },
    fields: ['x0'],
  },
  {
    id: 'hawkes',
    name: 'Hawkes',
    group: 'Processes',
    code: '07',
    formula: 'λ(t) = μ + ∑ αe⁻ᵝ⁽ᵗ⁻ᵗⁱ⁾',
    description:
      'Self-exciting arrivals. Each event raises the intensity, then excitation decays.',
    defaults: { T: 30, paths: 1000, mu: 0.8 },
    fields: ['mu', 'alpha', 'beta'],
  },
  {
    id: 'first-passage',
    name: 'First passage',
    group: 'Experiments',
    code: '08',
    formula: 'P(τₐ ≤ T) = 2[1 − Φ(a/√T)]',
    description:
      'When does Brownian motion first reach a barrier? Compare exact Monte Carlo with theory.',
    defaults: { T: 1 },
    fields: ['barrier'],
  },
  {
    id: 'reflection',
    name: 'Reflection principle',
    group: 'Experiments',
    code: '09',
    formula: 'B̃ₜ = 2a − Bₜ,  t ≥ τₐ',
    description:
      'Select a crossing path. Reflect its tail about the barrier and inspect the construction.',
    defaults: { T: 1 },
    fields: ['barrier'],
  },
  {
    id: 'qv',
    name: 'Quadratic variation',
    group: 'Experiments',
    code: '10',
    formula: '∑(Bₜᵢ₊₁ − Bₜᵢ)² → T',
    description:
      'Refine nested partitions of the same path and watch quadratic variation approach elapsed time.',
    defaults: { T: 1, steps: 1024 },
    fields: [],
  },
  {
    id: 'stationarity',
    name: 'OU stationarity',
    group: 'Experiments',
    code: '11',
    formula: 'X∞ ∼ N(μ, σ²/2θ)',
    description:
      'Explore relaxation toward the stationary Gaussian distribution.',
    defaults: { T: 10, x0: 2 },
    fields: ['theta', 'mu', 'sigma', 'x0'],
  },
] as const;
export type ModelId = (typeof models)[number]['id'];
export type Params = Record<string, number | number[][]> & {
  seed: number;
  paths: number;
  steps: number;
  T: number;
  barrier: number;
};
export const defaults: Params = {
  seed: 48291,
  paths: 10000,
  steps: 512,
  T: 1,
  barrier: 1,
  theta: 2,
  mu: 0,
  sigma: 1,
  x0: 0,
  lambda: 5,
  alpha: 0.7,
  beta: 1.2,
  jump_mean: 0,
  jump_sigma: 1,
  Q: [
    [-2, 1.5, 0.5],
    [1, -2, 1],
    [0.5, 1.5, -2],
  ],
};
export function parametersFor(id: string): Params {
  return { ...defaults, ...models.find((m) => m.id === id)?.defaults };
}
export const fieldInfo: Record<
  string,
  { label: string; min: number; max: number; step: number }
> = {
  paths: { label: 'Realizations', min: 1, max: 1000000, step: 1000 },
  steps: { label: 'Grid steps', min: 16, max: 4096, step: 16 },
  T: { label: 'Horizon · T', min: 0.01, max: 1000, step: 0.5 },
  barrier: { label: 'Barrier · a', min: 0.01, max: 20, step: 0.1 },
  theta: { label: 'Reversion · θ', min: 0.01, max: 100, step: 0.1 },
  mu: { label: 'Mean / baseline · μ', min: -20, max: 100, step: 0.1 },
  sigma: { label: 'Volatility · σ', min: 0, max: 20, step: 0.1 },
  x0: { label: 'Initial state · x₀', min: -20, max: 20, step: 1 },
  lambda: { label: 'Arrival rate · λ', min: 0.01, max: 100, step: 0.5 },
  alpha: { label: 'Excitation · α', min: 0, max: 20, step: 0.1 },
  beta: { label: 'Decay · β', min: 0.01, max: 20, step: 0.1 },
  jump_mean: { label: 'Jump mean', min: -20, max: 20, step: 0.1 },
  jump_sigma: { label: 'Jump deviation', min: 0, max: 20, step: 0.1 },
};
export type Series = {
  t: number[];
  y: number[];
  intensity?: number[];
  events?: number[];
  hit?: number | null;
};
export type Metric = {
  key: string;
  label: string;
  value: number;
  theory?: number;
  se?: number;
  note?: string;
};
export type Simulation = {
  model: string;
  seed: number;
  paths: number;
  steps: number;
  T: number;
  algorithm: string;
  series: Series[];
  histogram: { x: number; count: number; density: number }[];
  metrics: Metric[];
  diagnostics?: Metric[];
  occupation?: number[];
  stationary?: number[];
  qv?: { partitions: number; value: number; theory: number }[];
  warnings: string[];
  elapsed_ms: number;
};
export type ExchangeState = {
  time: number;
  events: number;
  flow: string;
  intensity: number;
  mid: number;
  spread: number;
  imbalance: number;
  bids: { price: number; quantity: number }[];
  asks: { price: number; quantity: number }[];
  tape: {
    time: number;
    type: string;
    side: string;
    price: number;
    quantity: number;
  }[];
  trades: { time: number; side: string; price: number; quantity: number }[];
  history: { time: number; mid: number; intensity: number }[];
  maker: {
    enabled: boolean;
    inventory: number;
    cash: number;
    pnl: number;
    fills: number;
    max_inventory: number;
    spread_capture: number;
  };
  warnings: string[];
};
export const format = (n: number | undefined, digits = 4) =>
  n === undefined || !Number.isFinite(n)
    ? '—'
    : n.toLocaleString('en-US', {
        minimumFractionDigits: digits,
        maximumFractionDigits: digits,
      });
