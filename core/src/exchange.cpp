#include "stochlab/exchange.hpp"
#include "stochlab/rng.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace stochlab {
namespace {
constexpr PriceTicks max_price = 1'000'000'000;
constexpr Quantity max_quantity = 1'000'000;
constexpr std::size_t max_orders = 4096;
constexpr Quantity inventory_limit = 100;
constexpr Quantity quote_quantity = 8;
constexpr double tick = 0.01;

void validate_side(Side side) {
  if (side != Side::buy && side != Side::sell)
    throw std::invalid_argument("Order side must be buy or sell");
}

void validate_quantity(Quantity quantity) {
  if (quantity <= 0 || quantity > max_quantity)
    throw std::invalid_argument("Order quantity must be in [1, 1000000]");
}

const char *side_name(Side side) { return side == Side::buy ? "buy" : "sell"; }
Side opposite(Side side) { return side == Side::buy ? Side::sell : Side::buy; }

double parameter(const json &config, const char *key, double fallback, double minimum,
                 double maximum) {
  if (!config.contains(key))
    return fallback;
  if (!config.at(key).is_number())
    throw std::invalid_argument(std::string(key) + " must be numeric");
  const double value = config.at(key).get<double>();
  if (!std::isfinite(value) || value < minimum || value > maximum)
    throw std::invalid_argument(std::string(key) + " is outside its allowed range");
  return value;
}

std::uint32_t seed_parameter(const json &config) {
  if (!config.is_object())
    throw std::invalid_argument("Exchange config must be an object");
  if (!config.contains("seed"))
    return 42;
  const auto &seed = config.at("seed");
  if (!seed.is_number())
    throw std::invalid_argument("seed must be an unsigned 32-bit integer");
  const double value = seed.get<double>();
  if (!std::isfinite(value) || value != std::floor(value) || value < 0 ||
      value > std::numeric_limits<std::uint32_t>::max())
    throw std::invalid_argument("seed must be an unsigned 32-bit integer");
  return static_cast<std::uint32_t>(value);
}

bool maker_parameter(const json &config) {
  if (!config.contains("maker"))
    return true;
  if (!config.at("maker").is_boolean())
    throw std::invalid_argument("maker must be a boolean");
  return config.at("maker").get<bool>();
}

template <typename T> void push_bounded(std::deque<T> &values, T value, std::size_t count) {
  values.push_back(std::move(value));
  if (values.size() > count)
    values.pop_front();
}
} // namespace

Submission OrderBook::limit(Side side, PriceTicks price, Quantity quantity, bool maker) {
  if (price <= 0 || price > max_price)
    throw std::invalid_argument("Limit price must be in [1, 1000000000] ticks");
  return submit(side, price, quantity, maker);
}

Submission OrderBook::market(Side side, Quantity quantity, bool maker) {
  return submit(side, std::nullopt, quantity, maker);
}

Submission OrderBook::submit(Side side, std::optional<PriceTicks> price, Quantity quantity,
                             bool maker) {
  validate_side(side);
  validate_quantity(quantity);
  if (next_id_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Order identifier space exhausted");
  const auto id = next_id_++;
  Submission result{id, 0, 0, {}};
  auto &opposing = side == Side::buy ? asks_ : bids_;
  Quantity remaining = quantity;
  while (remaining > 0 && !opposing.empty()) {
    auto level = side == Side::buy ? opposing.begin() : std::prev(opposing.end());
    if (price && (side == Side::buy ? level->first > *price : level->first < *price))
      break;
    auto &resting = level->second.orders.front();
    const Quantity traded = std::min(remaining, resting.quantity);
    result.fills.push_back({id, resting.id, side, resting.price, traded, maker, resting.maker});
    remaining -= traded;
    resting.quantity -= traded;
    level->second.quantity -= traded;
    result.executed += traded;
    if (resting.quantity == 0) {
      erase_location(resting.id);
      level->second.orders.pop_front();
      if (level->second.orders.empty())
        opposing.erase(level);
    }
  }
  if (price && remaining > 0) {
    auto &own = side == Side::buy ? bids_ : asks_;
    auto &level = own[*price];
    level.orders.push_back({id, side, *price, remaining, maker});
    level.quantity += remaining;
    locations_.emplace(id, Location{side, *price, std::prev(level.orders.end()), index_.size()});
    index_.push_back(id);
    result.resting = remaining;
  }
  return result;
}

void OrderBook::erase_location(std::uint64_t id) {
  auto found = locations_.find(id);
  const auto index = found->second.index;
  const auto last_id = index_.back();
  index_[index] = last_id;
  locations_.at(last_id).index = index;
  index_.pop_back();
  locations_.erase(found);
}

Quantity OrderBook::cancel(std::uint64_t id, Quantity quantity) {
  if (quantity < 0)
    throw std::invalid_argument("Cancellation quantity cannot be negative");
  const auto found = locations_.find(id);
  if (found == locations_.end())
    return 0;
  auto &own = found->second.side == Side::buy ? bids_ : asks_;
  auto level = own.find(found->second.price);
  auto order = found->second.order;
  const Quantity cancelled = quantity == 0 ? order->quantity : std::min(quantity, order->quantity);
  order->quantity -= cancelled;
  level->second.quantity -= cancelled;
  if (order->quantity == 0) {
    erase_location(id);
    level->second.orders.erase(order);
    if (level->second.orders.empty())
      own.erase(level);
  }
  return cancelled;
}

std::optional<RestingOrder> OrderBook::order(std::uint64_t id) const {
  auto found = locations_.find(id);
  if (found == locations_.end())
    return std::nullopt;
  return *found->second.order;
}

std::optional<RestingOrder> OrderBook::indexed_order(std::size_t index) const {
  return index < index_.size() ? order(index_[index]) : std::nullopt;
}

std::optional<PriceTicks> OrderBook::best_bid() const {
  return bids_.empty() ? std::nullopt : std::optional<PriceTicks>(bids_.rbegin()->first);
}

std::optional<PriceTicks> OrderBook::best_ask() const {
  return asks_.empty() ? std::nullopt : std::optional<PriceTicks>(asks_.begin()->first);
}

std::vector<std::pair<PriceTicks, Quantity>> OrderBook::depth(Side side, std::size_t count) const {
  validate_side(side);
  std::vector<std::pair<PriceTicks, Quantity>> result;
  if (side == Side::buy) {
    for (auto it = bids_.rbegin(); it != bids_.rend() && result.size() < count; ++it)
      result.emplace_back(it->first, it->second.quantity);
  } else {
    for (auto it = asks_.begin(); it != asks_.end() && result.size() < count; ++it)
      result.emplace_back(it->first, it->second.quantity);
  }
  return result;
}

bool OrderBook::valid() const {
  if (locations_.size() != index_.size())
    return false;
  if (best_bid() && best_ask() && *best_bid() >= *best_ask())
    return false;
  std::size_t count = 0;
  for (const Side side : {Side::buy, Side::sell}) {
    const auto &levels = side == Side::buy ? bids_ : asks_;
    for (const auto &[price, level] : levels) {
      if (price <= 0 || level.orders.empty())
        return false;
      Quantity quantity = 0;
      std::uint64_t previous_id = 0;
      for (const auto &item : level.orders) {
        if (item.quantity <= 0 || item.price != price || item.side != side ||
            item.id <= previous_id)
          return false;
        const auto found = locations_.find(item.id);
        if (found == locations_.end() || found->second.side != side ||
            found->second.price != price || found->second.order->id != item.id ||
            found->second.index >= index_.size() || index_[found->second.index] != item.id)
          return false;
        quantity += item.quantity;
        previous_id = item.id;
        ++count;
      }
      if (quantity != level.quantity)
        return false;
    }
  }
  return count == index_.size();
}

void MakerLedger::record(Side side, PriceTicks price, Quantity quantity,
                         double reference_mid_ticks) {
  validate_side(side);
  validate_quantity(quantity);
  if (price <= 0 || price > max_price || !std::isfinite(reference_mid_ticks) ||
      reference_mid_ticks <= 0 || reference_mid_ticks > static_cast<double>(max_price))
    throw std::invalid_argument("Invalid maker fill price or reference mid");
  const Quantity signed_quantity = side == Side::buy ? quantity : -quantity;
  const std::int64_t cost = price * signed_quantity;
  const auto low = std::numeric_limits<std::int64_t>::min();
  const auto high = std::numeric_limits<std::int64_t>::max();
  if ((cost > 0 && cash_ticks < low + cost) || (cost < 0 && cash_ticks > high + cost) ||
      (signed_quantity > 0 && inventory > high - signed_quantity) ||
      (signed_quantity < 0 && inventory <= low - signed_quantity))
    throw std::overflow_error("Maker ledger integer capacity exceeded");
  cash_ticks -= cost;
  inventory += signed_quantity;
  max_inventory = std::max(max_inventory, std::abs(inventory));
  capture_ticks +=
      static_cast<double>(signed_quantity) * (reference_mid_ticks - static_cast<double>(price));
  ++fills;
}

double MakerLedger::pnl(double mid_ticks) const {
  return (static_cast<double>(cash_ticks) + static_cast<double>(inventory) * mid_ticks) * tick;
}

struct Exchange::Impl {
  struct TapeEntry {
    double time;
    std::string type;
    Side side;
    double price;
    Quantity quantity;
  };
  struct TradeEntry {
    double time;
    Side side;
    PriceTicks price;
    Quantity quantity;
  };
  struct HistoryEntry {
    double time;
    double mid;
    double intensity;
  };
  Rng rng;
  OrderBook book;
  MakerLedger ledger;
  std::string flow;
  bool maker_enabled;
  double rate;
  double alpha;
  double beta;
  double skew;
  double time = 0;
  double excitation = 0;
  std::uint64_t events = 0;
  PriceTicks last_price = 10000;
  std::uint64_t bid_quote = 0;
  std::uint64_t ask_quote = 0;
  std::deque<TapeEntry> tape;
  std::deque<TradeEntry> trades;
  std::deque<HistoryEntry> history;
  std::vector<std::string> warnings;

  explicit Impl(const json &config)
      : rng(seed_parameter(config)), flow(config.value("flow", std::string("poisson"))),
        maker_enabled(maker_parameter(config)), rate(parameter(config, "rate", 20, .01, 10000)),
        alpha(parameter(config, "alpha", .7, 0, 1000)),
        beta(parameter(config, "beta", 1.2, .001, 1000)),
        skew(parameter(config, "skew", .05, 0, 5)) {
    if (flow != "poisson" && flow != "hawkes")
      throw std::invalid_argument("flow must be poisson or hawkes");
    if (alpha >= beta)
      throw std::invalid_argument("Stable Hawkes flow requires alpha < beta");
    if (flow == "hawkes" && alpha / beta >= .95)
      warnings.push_back("Near-critical Hawkes flow: long clustered bursts and slow convergence.");
    for (int level = 0; level < 12; ++level) {
      book.limit(Side::buy, 9998 - level, 20 + static_cast<Quantity>(rng.integer(40)));
      book.limit(Side::sell, 10002 + level, 20 + static_cast<Quantity>(rng.integer(40)));
    }
    refresh_quotes();
    remember();
  }

  double mid_ticks() const {
    auto bid = book.best_bid();
    auto ask = book.best_ask();
    if (bid && ask)
      return (static_cast<double>(*bid) + static_cast<double>(*ask)) * .5;
    if (bid)
      return static_cast<double>(*bid) + 1;
    if (ask)
      return static_cast<double>(*ask) - 1;
    return static_cast<double>(last_price);
  }

  double intensity() const { return rate + (flow == "hawkes" ? excitation : 0); }

  void add_tape(std::string type, Side side, double price, Quantity quantity) {
    push_bounded(tape, TapeEntry{time, std::move(type), side, price, quantity}, 32);
  }

  void record_fills(const Submission &submission, double reference) {
    for (const auto &fill : submission.fills) {
      last_price = fill.price;
      push_bounded(trades, TradeEntry{time, fill.aggressor_side, fill.price, fill.quantity}, 32);
      if (fill.resting_maker)
        ledger.record(opposite(fill.aggressor_side), fill.price, fill.quantity, reference);
      if (fill.aggressor_maker)
        ledger.record(fill.aggressor_side, fill.price, fill.quantity, reference);
    }
  }

  void refresh_quotes() {
    if (!maker_enabled)
      return;
    book.cancel(bid_quote);
    book.cancel(ask_quote);
    bid_quote = 0;
    ask_quote = 0;
    const double mid = mid_ticks();
    const double reservation = mid - skew * static_cast<double>(ledger.inventory);
    const PriceTicks best_bid = book.best_bid().value_or(static_cast<PriceTicks>(mid) - 2);
    const PriceTicks best_ask = book.best_ask().value_or(static_cast<PriceTicks>(mid) + 2);
    // Post-only protection: these quotes cannot take liquidity or self-trade.
    const auto bid = std::clamp<PriceTicks>(
        std::min<PriceTicks>(best_ask - 1, static_cast<PriceTicks>(std::floor(reservation - 1))), 1,
        max_price - 1);
    const auto ask = std::clamp<PriceTicks>(
        std::max<PriceTicks>(best_bid + 1, static_cast<PriceTicks>(std::ceil(reservation + 1))), 2,
        max_price);
    const Quantity buy_quantity = std::min(quote_quantity, inventory_limit - ledger.inventory);
    const Quantity sell_quantity = std::min(quote_quantity, inventory_limit + ledger.inventory);
    if (buy_quantity > 0)
      bid_quote = book.limit(Side::buy, bid, buy_quantity, true).id;
    if (sell_quantity > 0)
      ask_quote = book.limit(Side::sell, ask, sell_quantity, true).id;
  }

  std::optional<RestingOrder> random_external_order() {
    if (book.size() == 0)
      return std::nullopt;
    // At most two maker orders exist. Handle the all-maker case explicitly,
    // then rejection sampling is uniform over external resting order IDs.
    if (book.size() <= 2) {
      std::optional<RestingOrder> first, second;
      for (std::size_t index = 0; index < book.size(); ++index) {
        auto order = book.indexed_order(index);
        if (order && !order->maker) {
          if (!first)
            first = order;
          else
            second = order;
        }
      }
      if (second)
        return rng.integer(2) == 0 ? first : second;
      return first;
    }
    for (;;) {
      auto order = book.indexed_order(static_cast<std::size_t>(rng.integer(book.size())));
      if (order && !order->maker)
        return order;
    }
  }

  void maintain_book() {
    // A finite synthetic liquidity reservoir avoids an absorbing empty book.
    // These replenishments are visible and documented, not stochastic arrivals.
    if (!book.best_bid()) {
      const auto anchor = book.best_ask().value_or(last_price + 2);
      for (int level = 0; level < 3; ++level) {
        const auto price = std::clamp<PriceTicks>(anchor - 2 - level, 1, max_price - 1);
        const Quantity quantity = 25 + static_cast<Quantity>(rng.integer(25));
        record_fills(book.limit(Side::buy, price, quantity), mid_ticks());
        add_tape("replenish", Side::buy, static_cast<double>(price) * tick, quantity);
      }
    }
    if (!book.best_ask()) {
      const auto anchor = book.best_bid().value_or(last_price - 2);
      for (int level = 0; level < 3; ++level) {
        const auto price = std::clamp<PriceTicks>(anchor + 2 + level, 2, max_price);
        const Quantity quantity = 25 + static_cast<Quantity>(rng.integer(25));
        record_fills(book.limit(Side::sell, price, quantity), mid_ticks());
        add_tape("replenish", Side::sell, static_cast<double>(price) * tick, quantity);
      }
    }
    while (book.size() > max_orders) {
      auto order = random_external_order();
      if (!order)
        break;
      book.cancel(order->id);
      add_tape("capacity", order->side, static_cast<double>(order->price) * tick, order->quantity);
    }
  }

  void next_time() {
    if (flow == "poisson") {
      const double next = time + rng.exponential(rate);
      time = std::max(next, std::nextafter(time, std::numeric_limits<double>::infinity()));
      return;
    }
    // Ogata thinning with an exponentially decaying dominating intensity.
    while (true) {
      const double upper = rate + excitation;
      const double wait = rng.exponential(upper);
      const double next =
          std::max(time + wait, std::nextafter(time, std::numeric_limits<double>::infinity()));
      excitation *= std::exp(-beta * (next - time));
      time = next;
      if (rng.uniform() * upper <= rate + excitation) {
        excitation += alpha;
        return;
      }
    }
  }

  void remember() {
    push_bounded(history, HistoryEntry{time, mid_ticks() * tick, intensity()}, 256);
  }

  void step() {
    next_time();
    const double reference = mid_ticks();
    const Side side = rng.integer(2) == 0 ? Side::buy : Side::sell;
    const double kind = rng.uniform();
    const Quantity quantity = 1 + static_cast<Quantity>(rng.integer(24));
    if (kind < .52) {
      const PriceTicks best = side == Side::buy ? *book.best_bid() : *book.best_ask();
      const PriceTicks other = side == Side::buy ? *book.best_ask() : *book.best_bid();
      const double placement = rng.uniform();
      const auto distance =
          static_cast<PriceTicks>(std::min(20.0, std::floor(-2.5 * std::log(rng.uniform()))));
      PriceTicks price;
      if (placement < .08)
        price = other; // Marketable limit order.
      else if (placement < .28)
        price = best + (side == Side::buy ? 1 : -1);
      else
        price = best + (side == Side::buy ? -distance : distance);
      price = std::clamp<PriceTicks>(price, side == Side::buy ? 1 : 2,
                                     side == Side::buy ? max_price - 1 : max_price);
      auto result = book.limit(side, price, quantity);
      record_fills(result, reference);
      add_tape("limit", side, static_cast<double>(price) * tick, quantity);
    } else if (kind < .82) {
      auto result = book.market(side, quantity);
      record_fills(result, reference);
      const double price =
          result.fills.empty() ? reference : static_cast<double>(result.fills.back().price);
      add_tape("market", side, price * tick, result.executed);
    } else {
      auto order = random_external_order();
      if (order) {
        book.cancel(order->id);
        add_tape("cancel", order->side, static_cast<double>(order->price) * tick, order->quantity);
      }
    }
    ++events;
    maintain_book();
    if (maker_enabled && (events % 8 == 0 || !book.order(bid_quote) || !book.order(ask_quote))) {
      refresh_quotes();
      maintain_book();
    }
    remember();
  }

  json snapshot() const {
    json bids = json::array(), asks = json::array();
    Quantity bid_quantity = 0, ask_quantity = 0;
    for (auto [price, quantity] : book.depth(Side::buy, 12)) {
      bids.push_back({{"price", static_cast<double>(price) * tick}, {"quantity", quantity}});
      bid_quantity += quantity;
    }
    for (auto [price, quantity] : book.depth(Side::sell, 12)) {
      asks.push_back({{"price", static_cast<double>(price) * tick}, {"quantity", quantity}});
      ask_quantity += quantity;
    }
    json tape_json = json::array(), trades_json = json::array(), history_json = json::array();
    for (auto it = tape.rbegin(); it != tape.rend(); ++it)
      tape_json.push_back({{"time", it->time},
                           {"type", it->type},
                           {"side", side_name(it->side)},
                           {"price", it->price},
                           {"quantity", it->quantity}});
    for (auto it = trades.rbegin(); it != trades.rend(); ++it)
      trades_json.push_back({{"time", it->time},
                             {"side", side_name(it->side)},
                             {"price", static_cast<double>(it->price) * tick},
                             {"quantity", it->quantity}});
    for (const auto &item : history)
      history_json.push_back(
          {{"time", item.time}, {"mid", item.mid}, {"intensity", item.intensity}});
    const double spread = book.best_bid() && book.best_ask()
                              ? static_cast<double>(*book.best_ask() - *book.best_bid()) * tick
                              : 0;
    const auto total = bid_quantity + ask_quantity;
    return {
        {"time", time},
        {"events", events},
        {"flow", flow},
        {"intensity", intensity()},
        {"mid", mid_ticks() * tick},
        {"spread", spread},
        {"imbalance",
         total ? static_cast<double>(bid_quantity - ask_quantity) / static_cast<double>(total) : 0},
        {"bids", std::move(bids)},
        {"asks", std::move(asks)},
        {"tape", std::move(tape_json)},
        {"trades", std::move(trades_json)},
        {"history", std::move(history_json)},
        {"maker",
         {{"enabled", maker_enabled},
          {"inventory", ledger.inventory},
          {"cash", static_cast<double>(ledger.cash_ticks) * tick},
          {"pnl", ledger.pnl(mid_ticks())},
          {"fills", ledger.fills},
          {"max_inventory", ledger.max_inventory},
          {"spread_capture", ledger.capture_ticks * tick}}},
        {"warnings", warnings}};
  }
};

Exchange::Exchange(const json &config) : impl_(std::make_unique<Impl>(config)) {}
Exchange::~Exchange() = default;
Exchange::Exchange(Exchange &&) noexcept = default;
Exchange &Exchange::operator=(Exchange &&) noexcept = default;

json Exchange::advance(int events) {
  if (events < 0 || events > 250000)
    throw std::invalid_argument("Exchange step must contain 0 to 250000 events");
  for (int event = 0; event < events; ++event)
    impl_->step();
  return impl_->snapshot();
}

json Exchange::snapshot() const { return impl_->snapshot(); }

bool Exchange::valid() const {
  if (!impl_ || !impl_->book.valid() || impl_->book.size() > max_orders ||
      !impl_->book.best_bid() || !impl_->book.best_ask() || !std::isfinite(impl_->time) ||
      !std::isfinite(impl_->intensity()) || std::abs(impl_->ledger.inventory) > inventory_limit ||
      impl_->tape.size() > 32 || impl_->trades.size() > 32 || impl_->history.size() > 256)
    return false;
  double previous = -1;
  for (const auto &point : impl_->history) {
    if (point.time <= previous || !std::isfinite(point.mid) || point.mid <= 0)
      return false;
    previous = point.time;
  }
  return true;
}

} // namespace stochlab
