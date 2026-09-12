# Mathematical methods and numerical guarantees

StochLab uses the same C++ implementations in the native executable and in
WebAssembly. Results are simulated observations. Theoretical values are identified
separately and are never substituted for empirical statistics.

## Reproducibility and Monte Carlo

`Rng` uses an explicit SplitMix64 stream, open-interval uniforms, Box–Muller
Gaussian draws with a cached second variate, and inverse-transform exponential
waiting times. Integer draws use rejection to avoid modulo bias. This generator
is suitable for this simulation workbench; it is not cryptographic and this
project does not claim a comprehensive random-number test battery.

Each request creates its own RNG from a 32-bit unsigned seed. Identical parameters
and seed reproduce identical results in the same build/runtime, apart from
`elapsed_ms`. The integer stream is specified independently of the C++ standard
library. Small cross-platform differences in transcendental functions are possible;
bit-for-bit floating-point equivalence across native and browser builds is not a
public guarantee. Changing a grid parameter can change subsequent random draws.
Execution is serial, so thread scheduling does not affect reproducibility.

Shared infrastructure includes input validation, Welford sample moments, Monte
Carlo standard errors, bounded output, and terminal histograms. Variance uses the
unbiased sample estimator with denominator `paths - 1`. For a single realization,
variance and standard error are returned as zero with a warning that they are not
estimable. A reported `se` is a one-standard-error diagnostic, not a guaranteed
confidence interval. A rough 95% normal interval is estimate ± 1.96 SE when its
asymptotic approximation is appropriate. Rare-event probabilities require special
care; first-passage SE uses the known Bernoulli probability, which avoids a false
zero SE when no crossing is observed.

Every requested realization contributes to aggregate statistics and the histogram.
At most 24 continuous paths or 8 event paths cross the JSON boundary. For models
with exact marginal distributions, undisplayed realizations are sampled directly
from those marginals instead of allocating full trajectories. Rendered paths are
included in the aggregate. This keeps a million-realization first-passage request
linear in the number of samples rather than samples × display steps.

Histogram bins report center `x`, count, and density = count / (sample count × bin
width). Small integer count/state supports use unit-width bins. Other histograms
use 40 bins spanning the observations. Histograms show terminal values, including
in QV mode; QV values have their own diagnostics.

## Symmetric random walk

With independent signs εₖ ∈ {−1,+1}, each with probability 1/2, the plotted walk is

```
X(kT/n) = sqrt(T/n) Σ[j=1…k] εⱼ.
E[X(T)] = 0; Var[X(T)] = T.
```

The engine generates every Bernoulli increment using integer RNG bits. Display
points connect consecutive lattice values. Increasing `steps` illustrates
diffusive scaling: the endpoint distribution tends toward N(0,T). The finite walk
is not itself Gaussian. Barrier statistics here concern grid observations and do
not claim a continuous Brownian first-passage formula.

## Brownian motion, maxima, and first passage

Standard Brownian motion starts at zero and has independent increments
`B(t+Δ) − B(t) ~ N(0,Δ)`. Displayed paths use these exact increments on a uniform
grid. Line segments between the sampled points are a rendering convention, not an
exact continuous Brownian trajectory.

For an upper barrier `a ≥ 0`, define `τₐ = inf{t ≥ 0 : B(t) = a}` and
`M(T) = sup{B(t) : 0 ≤ t ≤ T}`. The analytic comparisons are

```
B(T) ~ N(0,T)
P(τₐ ≤ T) = P(M(T) ≥ a) = 2[1 − Φ(a / sqrt(T))]
                            = erfc(a / sqrt(2T))
E[M(T)] = sqrt(2T/π).
```

The reflection principle maps the portion after the first continuous hitting time
to `2a − B(t)`. The browser illustrates this geometry on its sampled path and
labels the grid crossing. It cannot locate the true continuous hitting time from
finite samples alone. A barrier at zero is hit at time zero. Lower barriers and
nonzero Brownian drift are outside the current API.
The [University of Chicago Brownian-motion notes](https://www.stat.uchicago.edu/~lalley/Courses/312/BrownianMotion312.pdf)
provide the reflection-principle derivation.

Crucially, aggregate crossing estimates do **not** use the rendered grid crossing
count. For a Brownian bridge from `x` to `y` over an interval Δ, the conditional
maximum can be sampled as

```
M = (x + y + sqrt((y − x)² − 2Δ log U)) / 2,  U ~ Uniform(0,1).
```

This follows by inverting
`P(M ≤ m | x,y) = 1 − exp[−2(m−x)(m−y)/Δ]` for `m ≥ max(x,y)`.
For rendered paths, the engine samples a conditional maximum on every interval;
the maximum of those values includes the between-grid excursions. For other
realizations it draws `B(T)` and one conditional maximum over `[0,T]`. The resulting
terminal/maximum pair has its exact joint distribution. The joint law is discussed
in [Per Mykland’s Brownian maximum notes](https://galton.uchicago.edu/~mykland/345A08/390Lect7Aut08.pdf).

`series.hit` is the first **grid index** with `B(kT/n) ≥ a`, or null if there is none.
An exact aggregate crossing may occur even when that path has no grid hit. The API
does not return an exact continuous hitting-time sample. Reflection and hit markers
must therefore be read as a sampled-path visualization.

## Quadratic variation

For a uniform partition of size n,

```
QVₙ = Σ[k=1…n] (B(kT/n) − B((k−1)T/n))²
QVₙ ~ (T/n) χ²ₙ
E[QVₙ] = T; Var[QVₙ] = 2T²/n.
```

`qv` output contains squared-increment sums on nested partitions of the first
rendered path. Powers of two that divide `steps` are used, followed by the full
partition. A single path’s sequence need not converge monotonically.

QV mode also computes aggregate QV moments. Displayed samples use the actual
increments. For undisplayed samples, orthogonal projection of the n independent
Gaussian increments gives the exact joint representation

```
B(T) = sqrt(T) Z
QVₙ = B(T)²/n + (T/n) V
Z ~ N(0,1), V ~ χ²(n−1), independent.
```

The residual term is zero for n=1. A Marsaglia–Tsang gamma sampler generates the
chi-square residual in constant expected work. This maintains the joint endpoint
and QV law without allocating all n increments. QV mode does not report an aggregate
maximum: its sampled residual is not claimed to encode a full joint maximum/QV law.

## Ornstein–Uhlenbeck and stationarity

The model is

```
dX(t) = θ(μ − X(t))dt + σ dB(t), θ > 0.
```

The exact transition is

```
X(t+Δ) = μ + (X(t) − μ)e^(−θΔ)
         + σ sqrt((1 − e^(−2θΔ))/(2θ)) Z.
```

The implementation uses `expm1` for accurate small-interval variances. It uses no
Euler time-step approximation. The deterministic initial condition is `x0`.
The finite-horizon terminal moments are

```
E[X(T)] = μ + (x0 − μ)e^(−θT)
Var[X(T)] = σ²(1 − e^(−2θT))/(2θ).
```

For θ>0 the stationary law is `N(μ, σ²/(2θ))`. Both finite-horizon and stationary
comparisons are returned. A warning identifies horizons shorter than five
relaxation times `1/θ`. Stationarity mode defaults to `T=20, x0=3` to show relaxation
from a displaced starting point. Undisplayed endpoints use the exact transition
across the whole horizon. Zero volatility is allowed and gives the exact
mean-reverting ODE solution.

## Poisson and compound Poisson

For a homogeneous Poisson process of rate λ≥0, independent waiting times are
`Exp(λ)`. The engine advances directly from event to event; λ=0 produces no events.
There is no time-bucket approximation. Exact event times are retained in displayed
paths, with paired pre-/post-jump points to draw a right-continuous step function.

```
N(T) ~ Poisson(λT)
E[N(T)] = Var[N(T)] = λT.
```

For compound Poisson, independent marks `J ~ N(jump_mean, jump_sigma²)` are summed
at these same event times:

```
S(T) = Σ[i=1…N(T)] Jᵢ
E[S(T)] = λT E[J]
Var[S(T)] = λT E[J²] = λT(jump_sigma² + jump_mean²).
```

Normal marks can be negative; the compound process need not be nondecreasing.
The separate event count always is. Setting `jump_sigma=0` yields constant marks.
All realizations are simulated with event-time loops, including those not displayed.

## Generic finite-state CTMC

`Q` is a 1–16 state square generator matrix. Off-diagonal rates must be nonnegative,
diagonals nonpositive, and each row must sum to zero within a relative floating-point
tolerance. Within roundoff, diagonals are normalized to minus the off-diagonal sum.
The initial state is the integer parameter `x0`.

In state i the engine draws `Exp(−Qᵢᵢ)` and then chooses j≠i with probability
`Qᵢⱼ/(−Qᵢᵢ)`. A zero exit rate is absorbing. Occupation accumulates the actual time
spent in each state, including the final truncated holding interval, and is divided
by total simulated time. Events and state changes are exact; the display grid has
no effect on this simulation.

A stationary comparison solves `πQ=0, Σπᵢ=1` using scaled, partially pivoted
Gauss–Jordan elimination. A unique solution is returned even for a reducible chain
with a single closed class. Multiple closed classes produce a singular system;
very ill-conditioned systems are also declined. The UI reports that no unique,
reliable comparison is available. A finite CTMC always has at least one stationary
distribution; an empty comparison does not mean that none exists.

Occupation samples include the initial transient. Their stationary discrepancy is
not purely Monte Carlo error for finite T. Validation uses an independent exact
finite-time occupation formula for a two-state chain, as well as the stationary
solve and conservation of total occupation.

## Exponential-kernel Hawkes

The convention is

```
λ(t) = μ + Σ[tᵢ<t] α exp[−β(t−tᵢ)]
branching ratio n = α/β.
```

Here α is the **jump in intensity**, not the integrated kernel mass. The workbench
requires μ≥0, α≥0, β>0, and α<β. It rejects critical/supercritical configurations
rather than offering a stationary interpretation for them, and warns when n>0.9.
This is an application scope restriction; α≥β does not by itself imply that every
finite-horizon linear Hawkes model explodes in finite time.

Ogata adaptive thinning maintains a scalar excitation value. Its current intensity
is an upper bound until the next accepted event because the excitation decays
between events. A candidate wait is exponential at that bound, excitation decays
to the candidate time, and the event is accepted with probability current intensity
/ bound. Both rejected and accepted proposals refresh the bound. Accepted events
increase excitation by α. This is an exact event-time algorithm, up to floating-point
arithmetic; it does not approximate events with Bernoulli time buckets.
The method follows [Ogata, _On Lewis’ Simulation Method for Point Processes_ (1981)](https://bemlar.ism.ac.jp/zhuang/Refs/Refs/ogata1981ieee.pdf).

The process starts with empty history, `λ(0)=μ`. With d=β−α>0,

```
E[λ(T)] = μ + (μα/d)(1 − e^(−dT))
E[N(T)] = (μβ/d)T − (μα/d²)(1 − e^(−dT))
stationary mean intensity = μ/(1 − α/β).
```

The code evaluates the expected count using a small-argument series near criticality
to avoid cancellation. Count and terminal-intensity comparisons use the
**empty-history finite-horizon** formulas. The stationary intensity is shown as a
separate long-run diagnostic. It is not multiplied by T and mislabeled as exact
finite-horizon count theory.

Displayed intensity includes the exact pre-/post-event jumps and additional samples
of exponential decay on the requested grid. Lines connecting decay samples are a
plotting approximation. Each intensity array aligns with the path’s time array;
`events` provides the exact event times for raster plots.

## API defaults and resource limits

Common defaults: `seed=42`, `paths=2000`, `steps=256`. Default T is 1 for Brownian,
first passage, reflection, QV, random walk and OU; 20 for stationarity; and 10 for
Poisson, compound Poisson, CTMC and Hawkes.

| Parameter  |           Default | Accepted range             |
| ---------- | ----------------: | -------------------------- |
| paths      |              2000 | integer 1…1,000,000        |
| steps      |               256 | integer 1…8192             |
| T          |   model-dependent | 0.000001…10,000            |
| seed       |                42 | integer 0…4,294,967,295    |
| barrier    |                 1 | 0…1,000,000; upper barrier |
| theta      |                 2 | 0.000001…10,000            |
| mu, OU     |                 0 | −1,000,000…1,000,000       |
| x0, OU     | 0; stationarity 3 | −1,000,000…1,000,000       |
| sigma      |                 1 | 0…10,000                   |
| lambda     |                 3 | 0…10,000                   |
| jump_mean  |                 1 | −1,000,000…1,000,000       |
| jump_sigma |               0.5 | 0…10,000                   |
| mu, Hawkes |               0.5 | 0…10,000                   |
| alpha      |               0.7 | 0…10,000 and alpha < beta  |
| beta       |               1.2 | 0.000001…10,000            |
| x0, CTMC   |                 0 | integer state index        |

Default `Q = [[−2,2,0],[1,−3,2],[0,1,−1]]`. Generator entries have magnitude at most
10,000. Inputs must be numeric and finite; integer fields reject fractional values.
Invalid parameters throw an exception which the public request boundary turns into
an error response.

Random walk work is limited to 60 million increments, using a 64-bit check on both
native and WASM targets. Event models reject requests whose expected work (a
conservative maximum-rate bound for CTMC) exceeds 4 million events. Actual event
work is capped at 5 million, a displayed path at 12,000 events, and Hawkes proposals
at 20 million. These caps abort with an error; partial/truncated aggregates are never
silently returned. An event interval too small to advance the floating-point clock
also produces an explicit error. Exact Brownian, QV, and OU marginal sampling does not require a
paths × steps work cap.

## Validation

`test_processes()` checks RNG reference/determinism, output and histogram invariants,
finite and invalid inputs, resource limits, constant-size random-walk increments,
zero-noise OU, zero-rate processes, exact Poisson counts, deterministic compound
marks, absorbing/reducible CTMC behavior, Hawkes jumps/decay, and QV computed directly
from the returned path. Every model is tested for seeded repeatability, excluding
only elapsed time.

`validate_processes()` uses fixed seeds and explicit tolerances for 18 comparisons:
Brownian terminal mean/variance, first passage and maximum; QV mean/variance; scaled
random-walk mean/variance; OU stationary mean/variance; Poisson mean/variance;
compound Poisson mean/variance; CTMC stationary solve/finite-horizon occupation;
and Hawkes transient count/terminal intensity. It prints actual empirical, theoretical,
and tolerance values and throws on failure.

Most tolerances are five or six sampling standard errors using known moments.
Hawkes uses empirical Monte Carlo standard errors. CTMC occupation uses an explicit
0.007 tolerance at T=100 across 1,000 realizations; its exact stationary solve is
checked to 10⁻¹². These are deterministic regression checks, not formal proofs or
an exhaustive statistical certification. Run the native validation command for
measurements from the current compiler and machine; do not substitute stale results
for a fresh validation run.
