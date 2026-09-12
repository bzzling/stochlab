# StochLab v1 — project status

**Status: complete and deployed.** Verified on 2026-09-12.

**Production:** [stochlab.brandonling.ca](https://stochlab.brandonling.ca)

**Source:** [github.com/bzzling/stochlab](https://github.com/bzzling/stochlab)

**Cloudflare Worker:** `stochlab`

**Deployment version:** `6fff55d6-0bf9-4246-8a54-adb4c9aac0de`

**Verified implementation revision:** `a1fffe1` (this deployment report and refreshed screenshots are committed afterward).

## Delivered architecture

C++20 owns all stochastic algorithms, RNG, Monte Carlo accumulation, histograms,
order matching and maker accounting. CMake builds the native CLI and test runner.
Emscripten 4.0.16 compiles the same library into an ES module plus WebAssembly.

The browser invokes `sl_request` inside a dedicated Web Worker. Requests and
bounded results cross a JSON C ABI; C++ exceptions become readable errors without
destroying the worker. Native/WASM parity tests cover all models and both exchange
arrival modes. There is no duplicate JavaScript stochastic engine.

Astro 7.3.2 prerenders 16 pages, including all process/experiment deep links, the
exchange, static methods, native documentation and 404. React 19.3 hydrates only
the experiment or exchange island. Canvas draws dense paths and live histories;
SVG draws histograms, event rasters, occupation comparisons and nested QV.
IBM Plex fonts are self-hosted. The final WASM binary is 287,919 bytes; the complete
static distribution is approximately 1 MiB on disk.

Cloudflare Workers serves static assets without a simulation backend, database,
accounts, secrets in bundles, or custom DNS changes. The loader and WASM revalidate
to avoid mixing stale and new engine builds.

## Models and experiments

| Implemented model          | Numerical method                                                                            | Experiments / inspection                                                     |
| -------------------------- | ------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| Symmetric random walk      | Every Bernoulli increment under diffusive scaling                                           | Steps, paths, terminal distribution                                          |
| Brownian motion            | Exact Gaussian grid increments and exact endpoint/bridge maxima                             | Paths, running maximum, barriers, terminal law                               |
| First passage / reflection | Exact continuous-maximum Monte Carlo; displayed-polyline reflection                         | Empirical/theory probability, path selection, crossing inspection            |
| Quadratic variation        | Actual nested partitions; exact Gaussian projection and gamma/chi-square aggregate sampling | Mean/variance validation and pathwise refinement                             |
| Ornstein–Uhlenbeck         | Exact Gaussian transitions, stable expm1 drift/variance                                     | Mean reversion, initial state, finite-time vs stationary moments             |
| Poisson                    | Exact exponential interarrival times                                                        | Counts and exact event rasters                                               |
| Compound Poisson           | Exact arrivals with independent Gaussian jump marks                                         | Signed jumps and terminal moments                                            |
| Generic finite-state CTMC  | Exact holding times/state transitions, occupation integration, pivoted stationary solver    | Editable Q, absorbing/reducible cases, occupation vs stationary law          |
| Exponential Hawkes         | Ogata adaptive thinning, scalar exponential excitation                                      | Intensity jumps/decay, clusters, finite-horizon theory, stable-regime checks |

Each request uses an explicit SplitMix64 RNG stream, open-interval uniforms,
Box–Muller normals and inverse exponential waits. Execution is serial and seeded.
Same-build output repeats apart from timing. Floating-point differences across
platform math libraries remain possible; parity is tested with tolerance.

All requested realizations contribute to statistics and histograms. At most 24
continuous or 8 event paths cross the boundary. Undisplayed samples use exact
marginal laws when available. Event and random-walk workloads have explicit caps,
and aborted requests never masquerade as complete aggregates.

See [MATHEMATICS.md](docs/MATHEMATICS.md) for definitions, derivations, defaults,
validation tolerances and primary references.

## Exchange and market maker

The book stores individual FIFO resting orders in ordered price levels, with an
ID index and dense cancellation index. It implements marketable/resting limits,
market sweeps, partial fills, full/partial cancellations, resting-price execution,
and bid/ask depth. Cash and price ticks use integer arithmetic. Midpoint, spread,
imbalance, trades and event tape derive from the actual book.

Poisson and Hawkes clocks both drive external event opportunities. Their marks
choose limit orders (52%), market orders (30%) and cancellations (18%); buy/sell
sides are symmetric. Initial liquidity is seeded near 100.00. Labeled reservoir
replenishment and capacity evictions keep the two-sided book bounded at 4,096
orders. These artificial mechanisms are explicit model assumptions.

The maker posts ordinary passive quotes around an inventory-adjusted reservation
price. Quotes refresh every eight events or when one is filled; replacement loses
FIFO priority. Quote size is at most eight units and inventory is capped at ±100.
All fills are actual matches against its orders. The UI reports inventory, cash,
marked-to-midpoint P&L, fills, maximum absolute inventory and spread capture.

P&L = cash + inventory × midpoint. Spread capture is a diagnostic against the
pre-event midpoint, not realized profit. No fees, latency, hidden liquidity,
margin, calibration, forecast signal or real trading connection is modeled.
There is no claim that this strategy is profitable in real markets.

See [EXCHANGE.md](docs/EXCHANGE.md) for the complete mechanism and assumptions.

## Validation performed

Fresh native configure/build, unit tests, statistical checks, WASM compilation,
Astro production build, integration tests and live production tests all passed.

| Check                                      | Final result                                                              |
| ------------------------------------------ | ------------------------------------------------------------------------- |
| npm clean installation                     | 479 packages installed; audit reported 0 vulnerabilities                  |
| C++20 Release build                        | Pass; Apple Clang 16.0.0, macOS arm64                                     |
| CTest                                      | 4/4 targets pass: invariants/exchange, statistics, valid CLI, invalid CLI |
| Statistical validations                    | 18/18 empirical-vs-theory checks pass                                     |
| Exchange suite                             | 30,005 assertions pass                                                    |
| AddressSanitizer + UBSan                   | All 4 CTest targets pass in Debug sanitizer build                         |
| Native/WASM + command/geometry integration | 18/18 tests pass                                                          |
| Astro static analysis                      | 0 errors, 0 warnings, 0 hints                                             |
| Local Chromium browser suite               | 20/20 tests pass                                                          |
| Production Chromium browser suite          | 20/20 tests pass in 7.3 seconds; custom domain                            |
| Formatting / diff checks                   | Prettier, clang-format and git diff checks pass                           |
| Documented native examples                 | 11 commands executed successfully                                         |
| Cloudflare deploy                          | Successful; version above                                                 |
| Live WASM                                  | HTTP 200, application/wasm, explicit revalidation header                  |
| Deep links / 404                           | Process route HTTP 200; unknown route HTTP 404                            |
| Material production console errors         | None observed on the tested routes                                        |

The browser suite checks every model from a fresh deep link, a million-path worker
request, keyboard reseeding, commands/history focus, deterministic reruns,
reflection selection, JSON download, Hawkes/CTMC error recovery, exchange
start/pause/step/reset, Hawkes flow switching, static documentation and responsive
390/768/1280px layouts. Documentation screenshots were captured from actual
production simulation results.

Representative current numerical results (full output:
[docs/validation.txt](docs/validation.txt)):

| Quantity                       | Empirical |   Theory |
| ------------------------------ | --------: | -------: |
| Brownian terminal mean, T=1.5  |  0.003070 |        0 |
| Brownian terminal variance     |  1.498197 |      1.5 |
| Continuous first passage       |  0.413858 | 0.414216 |
| Mean QV, T=2                   |  1.999504 |        2 |
| OU stationary variance         |  0.188597 | 0.188235 |
| Poisson count mean             |  7.002233 |        7 |
| Compound Poisson variance      |  2.550866 | 2.541500 |
| CTMC finite-time occupation    |  0.335456 | 0.335556 |
| Hawkes finite-time event count |  9.273233 | 9.242935 |
| Hawkes terminal intensity      |  1.294621 | 1.294239 |

Most tolerances use five or six sampling standard errors; CTMC uses a documented
finite-time comparison and explicit tolerance. These tests are regression
evidence, not a proof or an exhaustive RNG certification.

Independent adversarial review found and fixed:

1. Exchange CLI rejection of integral seeds represented as JSON floating numbers.
2. OU small-time drift cancellation at θT = 10⁻¹².
3. Integration testing found and fixed a missing exception-handling compile flag
   on the WASM C ABI target.

Two additional independent exchange stress campaigns processed two million and
six million events across extreme rates, near-critical flow, maker toggles and
inventory-skew settings with no failed book/accounting invariants.

## Measured native benchmarks

Measured after the final build and browser suites completed, on the local macOS
arm64 host with Apple Clang 16.0.0, C++20 Release, seed 42. One warmup followed by
the median of three runs. No JSON text serialization is included. Measurements
are machine-specific and the host is not a controlled benchmark appliance.

| Workload                        |                 Work per run |    Median |
| ------------------------------- | ---------------------------: | --------: |
| Brownian grid generation        | 10,240,000 actual increments | 89.451 ms |
| Exact first-passage Monte Carlo |       1,000,000 realizations | 21.663 ms |
| Poisson event simulation        |     1,000 realizations, T=20 |  0.555 ms |
| Hawkes event simulation         |     1,000 realizations, T=20 |  0.938 ms |
| CTMC event simulation           |     1,000 realizations, T=20 |  0.538 ms |
| Exchange, Poisson flow          |               100,000 events | 21.945 ms |
| Exchange, Hawkes flow           |               100,000 events | 22.532 ms |

Raw measurements: [docs/benchmarks.json](docs/benchmarks.json). Grid generation is
about 114.5 million increments/second in this run. First-passage throughput uses
exact endpoint/maximum sampling and is not a claim about generating a million
full paths. Event benchmarks include bounded result construction.

Reproduce with `make bench` or `./build/stochlab bench --json`.

## Local usage and developer commands

```sh
make native
./build/stochlab experiment first-passage --barrier 1 --paths 1000000 --seed 42
./build/stochlab simulate hawkes --mu .2 --alpha .7 --beta 1.1 --T 30
./build/stochlab exchange --events 100000 --flow hawkes --maker on --seed 42
make test
make validate
make bench

source /path/to/emsdk/emsdk_env.sh
npm ci
npm run build:wasm
npm run dev

npm run build
npm test
npm run test:browser
npm run deploy
```

The README contains pinned SDK setup, all model examples, sanitizer commands,
browser shortcuts, build architecture and deployment instructions.
The source repository preserves the implementation history on `main` at
[bzzling/stochlab](https://github.com/bzzling/stochlab). The workbench links directly
to the source, mathematical reference, exchange specification and measured results.
Its native CLI page documents cloning, building, testing and deployment.

The custom-domain deployment passed all 20 Chromium tests, including fresh WASM
loads, all process routes, exchange operation and mobile layouts. Canonical URLs,
GitHub navigation, source documentation and HTTPS were separately verified.
Public GitHub access returns HTTP 200 and `main` contains the implementation history.

At verification time, the local ISP resolver still cached the earlier nonexistent
hostname. The browser suite used a temporary local TLS tunnel to the address
returned by public DNS, preserving the production hostname and certificate
verification. No system DNS settings or application code were changed. An initial
tunnel run blocked Cloudflare's existing analytics script; allowing that asset
resolved the console errors and the full suite then passed.

Production uses the `stochlab.brandonling.ca` custom domain. The main
`brandonling.ca` homepage and its Worker are unchanged. workers.dev and preview
URLs are disabled in the checked-in Wrangler configuration. Deployments use
Wrangler explicitly; GitHub pushes do not automatically deploy.

## Known limitations and future extensions

No required v1 feature remains incomplete. Deliberate limits:

- Rendered Brownian paths and crossing markers are sampled/polyline geometry.
  The UI does not claim to return exact continuous hitting times.
- Exact marginal sampling avoids retaining every full trajectory. The export
  contains bounded paths and full-sample summaries, not every realization.
- RNG scheduling is serial. Cross-platform transcendental results need not be
  bit-identical.
- CTMCs support 1–16 states. Nonunique or ill-conditioned stationary comparisons
  are omitted with a warning.
- Hawkes is univariate, exponential-kernel, empty-history, and restricted to
  stable parameters.
- The exchange's symmetric synthetic flow, liquidity reservoir, capacity bound
  and simple maker omit real market microstructure and trading costs.
- No accounts, cloud persistence, data feeds, multithreading or real execution.
- Browser verification used Chromium and the Codex in-app browser; other browser
  engines were not separately certified.

Sensible extensions are reproducible parallel substreams, typed-array result
transport for larger visible ensembles, continuous first-hit samplers, additional
browser-engine coverage and multivariate marked Hawkes order flow.
