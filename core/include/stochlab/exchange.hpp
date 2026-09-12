#pragma once

#include "stochlab/json.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace stochlab {

using json = nlohmann::json;
using PriceTicks = std::int64_t;
using Quantity = std::int64_t;
enum class Side { buy, sell };

struct RestingOrder {
  std::uint64_t id;
  Side side;
  PriceTicks price;
  Quantity quantity;
  bool maker;
};

struct Fill {
  std::uint64_t aggressor_id;
  std::uint64_t resting_id;
  Side aggressor_side;
  PriceTicks price;
  Quantity quantity;
  bool aggressor_maker;
  bool resting_maker;
};

struct Submission {
  std::uint64_t id;
  Quantity executed;
  Quantity resting;
  std::vector<Fill> fills;
};

// Price-time priority, integer ticks, resting-price execution. Market residuals
// are discarded. This class models matching independently of stochastic flow.
class OrderBook {
public:
  OrderBook() = default;
  OrderBook(const OrderBook &) = delete;
  OrderBook &operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) noexcept = default;
  OrderBook &operator=(OrderBook &&) noexcept = default;
  Submission limit(Side side, PriceTicks price, Quantity quantity, bool maker = false);
  Submission market(Side side, Quantity quantity, bool maker = false);
  Quantity cancel(std::uint64_t id, Quantity quantity = 0);
  std::optional<RestingOrder> order(std::uint64_t id) const;
  std::optional<RestingOrder> indexed_order(std::size_t index) const;
  std::optional<PriceTicks> best_bid() const;
  std::optional<PriceTicks> best_ask() const;
  std::vector<std::pair<PriceTicks, Quantity>> depth(Side side, std::size_t count) const;
  std::size_t size() const { return index_.size(); }
  bool valid() const;

private:
  struct Level {
    Quantity quantity = 0;
    std::list<RestingOrder> orders;
  };
  using Levels = std::map<PriceTicks, Level>;
  struct Location {
    Side side;
    PriceTicks price;
    std::list<RestingOrder>::iterator order;
    std::size_t index;
  };
  Levels bids_;
  Levels asks_;
  std::unordered_map<std::uint64_t, Location> locations_;
  std::vector<std::uint64_t> index_;
  std::uint64_t next_id_ = 1;
  Submission submit(Side side, std::optional<PriceTicks> price, Quantity quantity, bool maker);
  void erase_location(std::uint64_t id);
};

// All cash and inventory arithmetic is integral. Capture is a diagnostic
// against the pre-event mid, not realized profit or a trading-performance claim.
struct MakerLedger {
  Quantity inventory = 0;
  std::int64_t cash_ticks = 0;
  std::uint64_t fills = 0;
  Quantity max_inventory = 0;
  double capture_ticks = 0;
  void record(Side side, PriceTicks price, Quantity quantity, double reference_mid_ticks);
  double pnl(double mid_ticks) const;
};

class Exchange {
public:
  explicit Exchange(const json &config);
  ~Exchange();
  Exchange(Exchange &&) noexcept;
  Exchange &operator=(Exchange &&) noexcept;
  Exchange(const Exchange &) = delete;
  Exchange &operator=(const Exchange &) = delete;
  json advance(int events);
  json snapshot() const;
  bool valid() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

void test_exchange();

} // namespace stochlab
