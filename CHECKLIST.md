# StochLab v1 completion checklist

- [x] Inspect repository/environment; establish architecture and engine contract
- [x] C++20 seeded core: walk, Brownian, OU, Poisson, compound, CTMC, Hawkes
- [x] First passage, reflection, quadratic variation and stationarity experiments
- [x] Native invariants, statistical validation, exchange/accounting tests
- [x] Native CLI, real argument parsing, measured benchmark suite
- [x] Emscripten C ABI and WASM integration/parity tests
- [x] Astro static shell and docs; interactive React workbench
- [x] Functional commands, keyboard actions, accessible responsive controls
- [x] Event-driven exchange with FIFO matching, cancellations, Poisson/Hawkes flow
- [x] Live depth/tape/plots and inspectable inventory-skew market maker
- [x] Browser functional tests; error, loading, resize and visual polish pass
- [x] Formatting, static checks, abandoned-code audit
- [x] Accurate README, math/exchange documentation, screenshots, PROJECT_STATUS
- [x] Clean installation, native build/tests/validation/bench, WASM and web build
- [x] Cloudflare deployment, production WASM/deep-link/interaction/console checks

## Decisions

The initial workspace was empty and had no repository or existing build.
Sources are delivered in `outputs/stochlab`. C++20 with a pinned vendored JSON
header; CMake native builds; Emscripten modular ES module; dedicated Web Worker;
Astro with React interactive islands and hand-drawn Canvas/SVG plots.
Cloudflare static hosting; no backend, accounts, persistence or live market data.
Visual thesis: a charcoal scientific workstation, precision grid, ivory text,
muted teal trajectories and amber barriers, with compact indexed navigation.

## Final verification

Completed 2026-09-12. Native Release and sanitizer suites pass; 18 statistical
comparisons, 18 integration tests, and 20 local plus 20 production browser tests
pass. Cloudflare production: https://stochlab.brandonling22.workers.dev .
See PROJECT_STATUS.md for evidence, measured benchmarks and deliberate limits.
