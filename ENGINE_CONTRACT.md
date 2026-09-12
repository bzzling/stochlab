# Engine boundary

All stochastic computation lives in C++20. `stochlab::simulate(const json&)`
returns JSON; `stochlab::Exchange` owns a persistent event-driven exchange.
`json` aliases `nlohmann::json` from the vendored `stochlab/json.hpp`.

## Simulation request

`{model, seed, paths, steps, T, barrier, theta, mu, sigma, x0, lambda,
alpha, beta, jump_mean, jump_sigma, Q}`. Fields have sensible model defaults.
Models: `random-walk`, `brownian`, `first-passage`, `reflection`, `qv`, `ou`,
`stationarity`, `poisson`, `compound-poisson`, `ctmc`, `hawkes`.
Seed is a uint32. Validate finite parameters, integer counts, resource bounds.

## Simulation response

```
{
  model, seed, paths, steps, T,
  algorithm: string,
  series: [{t: number[], y: number[], intensity?: number[], hit?: number|null}],
  histogram: [{x: number, count: number, density: number}],
  metrics: [{key: string, label: string, value: number, theory?: number,
             se?: number, note?: string}],
  diagnostics?: [{key: string, label: string, value: number}],
  occupation?: number[], stationary?: number[],
  qv?: [{partitions: number, value: number, theory: number}],
  warnings: string[], elapsed_ms: number
}
```

Only up to 24 continuous or 8 event paths cross the boundary. Aggregate statistics use
the full requested sample count. Time/value arrays include t=0 and t=T. Exact
event times remain in jump series. `hit` is the first _grid_ crossing index for
continuous rendered paths (including an interpolated barrier point is optional;
document this). First-passage aggregate must account for between-grid crossings
(e.g. exact joint terminal/maximum sampling), with discretization explained.
`qv` contains nested partitions of the first Brownian path. Model math,
defaults, limitations and validation belong in docs/MATHEMATICS.md.

## Exchange

Header `core/include/stochlab/exchange.hpp` exposes:

```
namespace stochlab {
using json = nlohmann::json;
class Exchange {
public:
  explicit Exchange(const json& config);
  ~Exchange();
  json advance(int events); // processes events and returns snapshot
  json snapshot() const;
private: // implementation defined by exchange owner
};
void test_exchange(); // independent tests invoked by native test runner
}
```

Constructor config: `{seed:42, flow:"poisson"|"hawkes", maker:true,
rate:20, alpha:0.7, beta:1.2, skew:0.05}`. Normalized currency tick = 0.01,
initial price approximately 100.00; integer price ticks/accounting internally.
Snapshot schema:

```
{time, events, flow, intensity, mid, spread, imbalance,
 bids:[{price,quantity}], asks:[{price,quantity}],
 tape:[{time,type,side,price,quantity}],
 trades:[{time,side,price,quantity}],
 history:[{time,mid,intensity}],
 maker:{enabled,inventory,cash,pnl,fills,max_inventory,spread_capture},
 warnings: string[]}
```

Depth best-first both sides; tape/trades newest-first, history chronological.
At most 12 depth levels, 32 tape/trades and 256 history points. The maker's
fills must arise from its actual resting orders. Tests cover FIFO matching,
partial fills, cancellation, book invariants, accounting, deterministic flow.
Document simplifications in docs/EXCHANGE.md.

## C ABI (integration owner)

`const char* sl_request(const char*)` accepts
`{action:"simulate", ...params}`, `{action:"exchange-init", ...config}`,
`{action:"exchange-step", events:10}`, `{action:"exchange-snapshot"}`.
Returns a static JSON string, valid until next request. Exceptions become
`{error:string}`. A Web Worker calls this function, parses the bounded result,
and sends data to the React island. No JavaScript stochastic implementation.
