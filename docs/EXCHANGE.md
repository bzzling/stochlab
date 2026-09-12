# Synthetic event-driven exchange

StochLab maintains a real, simplified limit-order book. Prices change because orders
match, arrive, or cancel. There is no price SDE behind the exchange view. This is
an inspectable stochastic system, with deliberately synthetic order-flow rules;
it is not calibrated to a venue and is not a model of trading profitability.

## Matching and storage

`OrderBook` stores individual orders in FIFO lists inside ordered price levels.
An ID index points directly to each resting order. An additional dense ID vector
supports random cancellations without scanning the book.

- Prices are positive integer ticks; one tick is 0.01 currency units.
- Quantities are positive integers. A limit order matches the best eligible
  opposing price, then the oldest resting order at that price.
- Trades execute at the resting order's price. A marketable limit can sweep
  multiple levels and rests any unfilled remainder at its limit price.
- A market order consumes available opposing depth and discards its remainder.
- Full and partial cancellations remove remaining quantity. Partial cancellation
  preserves the order's position in the queue.
- Empty levels and filled IDs are removed immediately. A valid book has no
  crossed best prices, no nonpositive quantity, and exact agreement between the
  FIFO queues, ID index, and aggregate level quantities.

Exchange snapshots include up to 12 levels per side, best first. Imbalance uses
the displayed depth: `(bid quantity - ask quantity) / (bid quantity + ask quantity)`.
Midpoint and spread use the best bid and ask. The tape and trade list show at
most 32 records, newest first; history contains the most recent 256 event
samples in chronological order. The history's intensity is the **post-event**
conditional intensity. Memory does not grow with the length of the simulation.

## Arrival clock and order flow

The Poisson mode samples independent exponential interarrival times with rate
`rate`. Event count is the number of external stochastic arrival opportunities.

Hawkes mode uses

```text
lambda(t) = rate + sum(alpha * exp(-beta * (t - t_i)))
```

with Ogata thinning. After every accepted event the excitation increases by
`alpha`; between candidates it decays exactly. Candidates are drawn from an
exponential distribution whose rate is the current upper intensity, and are
accepted with the ratio of decayed intensity to the upper intensity. Rejected
candidates advance the internal time without modifying the book. The initial
excitation is zero, so the process has a startup transient. In the stable regime,
the long-run mean arrival rate is `rate / (1 - alpha / beta)`.

The same seeded `Rng` implementation powers the stochastic core and the exchange.
The clock, order selection, placement, and quantities all consume that stream.
Calling `advance(12000)` produces the same final snapshot as successive calls
for 1999, 5000, and 5001 events. Returning a snapshot consumes no random draws.
Floating-point event times are strictly increasing, with a next-representable-time
guard against loss of precision in exceptionally long runs.

Each accepted event independently selects a buy or sell side with equal probability
and one event type:

| Type   | Probability | Rule                                                                                                                            |
| ------ | ----------: | ------------------------------------------------------------------------------------------------------------------------------- |
| Limit  |        0.52 | Quantity uniformly 1–24; 8% join the opposite best price, 20% improve own best by one tick, and 72% rest at or behind own best. |
| Market |        0.30 | Quantity uniformly 1–24; match immediately, discarding unfilled quantity.                                                       |
| Cancel |        0.18 | Select an external resting order ID uniformly and cancel its remainder.                                                         |

The passive placement distance is `min(20, floor(-2.5 log U))` ticks. Orders can
still execute when a one-tick improvement reaches the opposite best. The market
tape reports executed quantity and the final execution price; the trade list
preserves each constituent fill. Limit tape quantity is the submitted quantity.
A cancellation opportunity with no external resting order changes only the clock.

Parameters are finite and validated: `rate` is in [0.01, 10000], `alpha` in
[0, 1000], `beta` in [0.001, 1000], and `alpha < beta`. The last condition enforces
stability; `alpha / beta >= 0.95` adds a near-critical warning in Hawkes mode.
Seeds are unsigned 32-bit integers. Each `advance` call accepts 0–250,000 events.

## Finite liquidity model

The initial book has 12 levels on each side around 100.00, with best external
prices 99.98 and 100.02 and seeded random quantities of 20–59 per level.

Two explicit maintenance rules keep the synthetic experiment usable indefinitely:

1. When either side becomes empty, a liquidity reservoir posts three levels with
   quantities 25–49, two ticks away from the opposite best and further outward.
   The event tape labels these orders `replenish`.
2. If resting order count exceeds 4096 at an event boundary, external orders are
   evicted uniformly until the bound holds. The tape labels removals `capacity`.

These maintenance actions occur at the current event time; they are not additional
Poisson or Hawkes events. They affect price dynamics and are part of this synthetic
model. Buy prices are capped below the maximum sell tick and sell prices above
the minimum buy tick, so the liquidity reservoir can always maintain an uncrossed,
two-sided book. These artificial price bounds are far outside the initial market.

## Inventory-skew maker

When enabled, one agent posts ordinary FIFO limit orders with maker ownership.
It has no privileged execution priority. Every recorded fill corresponds to an
actual matched resting order. Its quotes are post-only: each bid remains below
the external best ask and each ask above the external best bid.

On initialization, every eight external events, or when either quote disappears,
the agent cancels its surviving quotes and computes

```text
reservation = external midpoint in ticks - skew * inventory
bid = floor(reservation - 1)
ask = ceil(reservation + 1)
```

Prices are clipped to remain passive against the external book. `skew`, in ticks
per inventory unit, is configurable in [0, 5], with default 0.05. A positive
inventory shifts both quotes downward. Each quote has at most eight units; its
quantity is limited so inventory cannot exceed ±100 units. At the inventory
limit the quote that could increase exposure is omitted. Quote replacement loses
FIFO priority. The agent has no signal about future order flow.

Buying `q` units at integer price `p` adds `q` to inventory and subtracts `p*q`
from integer cash. Selling reverses those signs. The output converts cash to
currency units only at the JSON boundary:

```text
marked-to-market P&L = cash + inventory * current midpoint
```

The maker starts with zero inventory and zero cash; cash may be negative. `fills`
counts execution records, not units or parent orders. `max_inventory` is the
largest absolute inventory seen. `spread_capture` is the sum of signed quantity
times `(pre-event midpoint - execution price)` in currency units. This diagnostic
can be negative and is not realized profit; it does not measure subsequent
adverse price movement. Inventory marked at midpoint is not a liquidation value.

There are no fees, rebates, latency, queue-ahead estimates, margin requirements,
multiple agent strategies, auctions, hidden liquidity, price impact calibration,
or real exchange data. Hawkes excitation is univariate and shared by all event
types; it does not separately model signed order-flow excitation. The maker
demonstrates the interaction between inventory control and stochastic flow.

## Validation

The native `test_exchange()` suite covers explicit FIFO fills, resting-price
execution, multi-level sweeps, market residuals, full and partial cancellation,
cancelled queue survivors, moved-book iterators, maker integer cash and inventory,
round-trip P&L, overflow rejection, and invalid parameters. A seeded 15,000-event
randomized matching test checks book invariants after every operation. Integration
tests verify chunk-independent reproducibility, snapshot limits and ordering,
actual maker fills, inventory limits, and the mark-to-market identity for both
arrival modes.

Long-run tests process 120,000 events in each mode and validate internal storage
and book invariants. The Poisson nth-arrival time is checked against the Erlang
mean within six standard errors. The Hawkes observed event rate is checked
against its stationary mean with an 8% tolerance that allows startup and
clustering. The seed-953 run observed rates of approximately 20.0341 versus 20
and 48.1895 versus 48. These are measured validation results, not forecast or
benchmark claims. The exchange suite passed 30,005 assertions under both an
optimized native build and AddressSanitizer/UndefinedBehaviorSanitizer.

Run the repository's native test target to execute these checks. Benchmark
results and complete build commands are maintained in the root documentation.
