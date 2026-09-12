#include "stochlab/exchange.hpp"
#include "stochlab/rng.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace stochlab {

void test_exchange() {
  int checks = 0;
  const auto require = [&checks](bool condition, const std::string &message) {
    ++checks;
    if (!condition)
      throw std::runtime_error("Exchange test: " + message);
  };
  const auto close = [&require](double observed, double expected, double tolerance,
                                const std::string &message) {
    require(std::isfinite(observed) && std::abs(observed - expected) <= tolerance, message);
  };
  const auto rejects = [&require](auto &&operation, const std::string &message) {
    bool threw = false;
    try {
      operation();
    } catch (const std::exception &) {
      threw = true;
    }
    require(threw, message);
  };

  {
    OrderBook book;
    const auto first = book.limit(Side::sell, 10001, 5);
    const auto second = book.limit(Side::sell, 10001, 7, true);
    const auto third = book.limit(Side::sell, 10002, 9);
    book.limit(Side::buy, 9999, 4);
    const auto take = book.market(Side::buy, 8);
    require(take.executed == 8 && take.resting == 0 && take.fills.size() == 2,
            "market order matches across FIFO orders with a partial fill");
    require(take.fills[0].resting_id == first.id && take.fills[0].quantity == 5 &&
                take.fills[1].resting_id == second.id && take.fills[1].quantity == 3,
            "older order fills first at the same price");
    require(!book.order(first.id) && book.order(second.id)->quantity == 4 &&
                book.order(third.id)->quantity == 9,
            "filled IDs disappear and remainders persist");
    require(take.fills[1].resting_maker && !take.fills[0].resting_maker,
            "fills preserve resting owner identity");
    require(book.cancel(second.id, 2) == 2 && book.order(second.id)->quantity == 2,
            "partial cancellation retains queue position");
    const auto sweep = book.limit(Side::buy, 10002, 20);
    require(sweep.executed == 11 && sweep.resting == 9 && sweep.fills.size() == 2,
            "marketable limit sweeps eligible levels and rests its remainder");
    require(sweep.fills[0].price == 10001 && sweep.fills[1].price == 10002,
            "execution uses each resting order price, not the incoming limit price");
    require(book.best_bid() == 10002 && !book.best_ask(),
            "best prices update when a side is consumed");
    require(book.cancel(sweep.id) == 9 && book.cancel(sweep.id) == 0,
            "complete cancellation removes ID once");
    require(book.valid(), "matching and cancellation retain every book invariant");
    const auto sell = book.market(Side::sell, 20);
    require(sell.executed == 4 && sell.resting == 0 && book.size() == 0,
            "market residual is discarded rather than posted as a limit");
    require(book.market(Side::buy, 3).executed == 0 && book.valid(), "empty-book market is valid");
    rejects([&] { book.limit(Side::buy, 0, 1); }, "zero price rejected");
    rejects([&] { book.limit(Side::buy, 100, -1); }, "negative quantity rejected");
    rejects([&] { book.market(static_cast<Side>(4), 1); }, "invalid side rejected");
    rejects([&] { book.cancel(1, -2); }, "negative cancellation rejected");
  }

  {
    OrderBook book;
    const auto early = book.limit(Side::buy, 10000, 10);
    const auto middle = book.limit(Side::buy, 10000, 10);
    const auto late = book.limit(Side::buy, 10000, 10);
    require(book.cancel(middle.id) == 10, "middle queue cancellation works");
    require(book.cancel(early.id, 4) == 4, "partial cancellation works at queue head");
    const auto fill = book.market(Side::sell, 8);
    require(fill.fills.size() == 2 && fill.fills[0].resting_id == early.id &&
                fill.fills[0].quantity == 6 && fill.fills[1].resting_id == late.id &&
                fill.fills[1].quantity == 2,
            "cancelled quantity does not disturb FIFO survivors");
    OrderBook moved(std::move(book));
    require(moved.valid() && moved.cancel(late.id) == 8 && moved.size() == 0,
            "moving the book preserves indexed FIFO iterators");
  }

  {
    MakerLedger ledger;
    ledger.record(Side::buy, 9999, 5, 10000);
    require(ledger.inventory == 5 && ledger.cash_ticks == -49995 && ledger.fills == 1,
            "maker buy debits exact integer cash and credits inventory");
    close(ledger.pnl(10000), .05, 1e-12, "mark-to-market after maker purchase");
    ledger.record(Side::sell, 10001, 3, 10000);
    require(ledger.inventory == 2 && ledger.cash_ticks == -19992 && ledger.max_inventory == 5,
            "maker sell credits exact cash and debits inventory");
    close(ledger.pnl(10000), .08, 1e-12, "mark-to-market includes remaining inventory");
    close(ledger.capture_ticks, 8, 1e-12, "spread-capture diagnostic uses pre-event midpoint");
    ledger.record(Side::sell, 10002, 2, 10001);
    require(ledger.inventory == 0 && ledger.cash_ticks == 12 && ledger.fills == 3,
            "completed round trip realizes the integer cash balance");
    close(ledger.pnl(10100), .12, 1e-12, "flat inventory P&L is independent of mark");
    MakerLedger overflow;
    overflow.cash_ticks = std::numeric_limits<std::int64_t>::max();
    rejects([&] { overflow.record(Side::sell, 1, 1, 1); }, "cash overflow rejected");
  }

  {
    Rng rng(48291);
    OrderBook book;
    for (int event = 0; event < 15000; ++event) {
      const Side side = rng.integer(2) == 0 ? Side::buy : Side::sell;
      const Quantity quantity = 1 + static_cast<Quantity>(rng.integer(20));
      const auto action = rng.integer(4);
      if (action < 2) {
        const auto result =
            book.limit(side, 9990 + static_cast<PriceTicks>(rng.integer(21)), quantity);
        require(result.executed + result.resting == quantity, "limit conserves submitted quantity");
      } else if (action == 2) {
        const auto result = book.market(side, quantity);
        Quantity filled = 0;
        for (const auto &fill : result.fills)
          filled += fill.quantity;
        require(result.executed == filled && result.executed <= quantity && result.resting == 0,
                "market execution equals the sum of actual fills");
      } else if (book.size()) {
        const auto order = *book.indexed_order(static_cast<std::size_t>(rng.integer(book.size())));
        require(book.cancel(order.id) == order.quantity,
                "random cancellation removes exact remaining size");
      }
      require(book.valid(), "randomized FIFO, index, depth, positivity and uncrossed invariants");
    }
  }

  for (const std::string flow : {"poisson", "hawkes"}) {
    const json config{{"seed", 42}, {"flow", flow}, {"maker", true}};
    Exchange batch(config), chunks(config);
    const auto zero = batch.snapshot();
    require(batch.advance(0) == zero, "zero events is an exact no-op");
    const auto a = batch.advance(12000);
    chunks.advance(1999);
    chunks.advance(5000);
    const auto b = chunks.advance(5001);
    require(a == b, flow + " is reproducible and independent of advance batch sizes");
    require(batch.valid() && chunks.valid(), flow + " exchange state invariants");
    require(a.at("events") == 12000 && a.at("time").get<double>() > 0,
            flow + " simulation advances event count and time");
    require(a.at("history").size() == 256 && a.at("tape").size() <= 32 &&
                a.at("trades").size() <= 32 && a.at("bids").size() <= 12 &&
                a.at("asks").size() <= 12,
            "snapshot arrays obey bounded transport contract");
    const auto maker = a.at("maker");
    require(maker.at("fills").get<int>() > 0 && maker.at("max_inventory").get<int>() <= 100,
            "maker has actual fills with bounded inventory");
    close(maker.at("pnl"),
          maker.at("cash").get<double>() +
              maker.at("inventory").get<double>() * a.at("mid").get<double>(),
          1e-8, "exchange mark-to-market identity");
    double previous = -1;
    for (const auto &point : a.at("history")) {
      require(point.at("time").get<double>() > previous, "history clock is strictly increasing");
      previous = point.at("time");
    }
    double tape_time = std::numeric_limits<double>::infinity();
    for (const auto &entry : a.at("tape")) {
      require(entry.at("time").get<double>() <= tape_time, "tape is newest first");
      tape_time = entry.at("time");
    }
    rejects([&] { batch.advance(-1); }, "negative events rejected");
    rejects([&] { batch.advance(250001); }, "oversized batch rejected");
  }

  {
    Exchange plain({{"seed", 953}, {"maker", false}, {"rate", 20}});
    const auto value = plain.advance(120000);
    require(plain.valid(), "120000 Poisson events retain bounded book and history");
    const double t = value.at("time");
    close(t, 6000, 6 * std::sqrt(120000.0) / 20,
          "Poisson nth-arrival time follows Erlang mean within six SE");
    const auto maker = value.at("maker");
    require(!maker.at("enabled").get<bool>() && maker.at("inventory") == 0 &&
                maker.at("cash") == 0 && maker.at("fills") == 0,
            "disabled maker never receives fills");
    Exchange hawkes(
        {{"seed", 953}, {"flow", "hawkes"}, {"rate", 20}, {"alpha", .7}, {"beta", 1.2}});
    const auto clustered = hawkes.advance(120000);
    require(hawkes.valid(), "120000 Hawkes events retain bounded book and history");
    const double empirical = 120000.0 / clustered.at("time").get<double>();
    close(empirical, 48, 3.84,
          "Hawkes long-run rate agrees with mu/(1-alpha/beta) within 8 percent");
    require(clustered.at("intensity").get<double>() >= 20.7,
            "Hawkes snapshot intensity includes the latest event excitation");
    std::cout << "  exchange arrival rates: Poisson " << 120000.0 / t << " (theory 20), Hawkes "
              << empirical << " (theory 48)\n";
  }

  for (const std::uint32_t seed : {0U, 42U, std::numeric_limits<std::uint32_t>::max()}) {
    Exchange integer_seed({{"seed", seed}});
    Exchange numeric_seed({{"seed", static_cast<double>(seed)}});
    require(integer_seed.advance(16) == numeric_seed.advance(16),
            "integer and integer-valued floating JSON seeds produce identical exchanges");
  }

  for (const json &config :
       {json{{"seed", -1}}, json{{"seed", 1.5}}, json{{"seed", 4294967296.0}}, json{{"seed", "42"}},
        json{{"seed", true}}, json{{"seed", std::numeric_limits<double>::infinity()}},
        json{{"seed", std::numeric_limits<double>::quiet_NaN()}}, json{{"rate", 0}},
        json{{"rate", "20"}}, json{{"beta", 0}}, json{{"alpha", 1.2}}, json{{"skew", -1}},
        json{{"flow", "invalid"}}, json{{"maker", 1}},
        json{{"rate", std::numeric_limits<double>::infinity()}}}) {
    rejects([&] { Exchange invalid(config); }, "invalid configuration rejected");
  }
  rejects([] { Exchange invalid(json::array()); }, "non-object configuration rejected");
  std::cout << "  exchange: " << checks << " assertions passed\n";
}

} // namespace stochlab
